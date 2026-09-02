param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '3.0.1',
    [string]$FfmpegExecutable = '',
    [string]$FfmpegLicense = ''
)

$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param([string]$File, [string[]]$Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File failed with exit code $LASTEXITCODE"
    }
}

$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$packageRoot = Join-Path $repository 'out\package'
$stage = Join-Path $packageRoot "staging\PIP-Link-v$Version-win64"
$archive = Join-Path $packageRoot "PIP-Link-v$Version-win64.zip"

if ([string]::IsNullOrWhiteSpace($FfmpegExecutable)) {
    $ffmpegCommand = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if ($null -eq $ffmpegCommand) {
        throw 'ffmpeg.exe was not found. Pass -FfmpegExecutable with its full path.'
    }
    $FfmpegExecutable = $ffmpegCommand.Source
}
$FfmpegExecutable = [System.IO.Path]::GetFullPath($FfmpegExecutable)
if (-not (Test-Path -LiteralPath $FfmpegExecutable -PathType Leaf)) {
    throw "ffmpeg.exe does not exist: $FfmpegExecutable"
}

if ([string]::IsNullOrWhiteSpace($FfmpegLicense)) {
    $FfmpegLicense = Join-Path (Split-Path (Split-Path $FfmpegExecutable)) 'LICENSE.txt'
}
$FfmpegLicense = [System.IO.Path]::GetFullPath($FfmpegLicense)
if (-not (Test-Path -LiteralPath $FfmpegLicense -PathType Leaf)) {
    throw "FFmpeg license does not exist: $FfmpegLicense"
}

$dotnetCommand = Get-Command dotnet.exe -ErrorAction SilentlyContinue
$dotnet = if ($null -ne $dotnetCommand) {
    $dotnetCommand.Source
} else {
    'C:\Program Files\dotnet\dotnet.exe'
}
if (-not (Test-Path -LiteralPath $dotnet -PathType Leaf)) {
    throw 'The .NET SDK is required to build the WiX MSI.'
}

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
$packageRootPrefix = [System.IO.Path]::GetFullPath($packageRoot).TrimEnd('\') + '\'
$stageFullPath = [System.IO.Path]::GetFullPath($stage)
if (-not $stageFullPath.StartsWith($packageRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean a staging directory outside $packageRootPrefix"
}
if (Test-Path -LiteralPath $stageFullPath) {
    Remove-Item -LiteralPath $stageFullPath -Recurse -Force
}

Push-Location $repository
try {
    Invoke-Checked cmake @('--preset', 'release')
    Invoke-Checked cmake @('--build', '--preset', 'release', '-j', '26')
    Invoke-Checked ctest @('--test-dir', 'out/build/release', '--output-on-failure')
    Invoke-Checked cmake @('--install', 'out/build/release', '--prefix', $stageFullPath)

    Copy-Item -LiteralPath $FfmpegExecutable -Destination (Join-Path $stageFullPath 'ffmpeg.exe')
    Copy-Item -LiteralPath $FfmpegLicense -Destination (Join-Path $stageFullPath 'FFmpeg-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $repository 'LICENSE') -Destination (Join-Path $stageFullPath 'LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $repository 'README.md') -Destination $stageFullPath

    Compress-Archive -Path (Join-Path $stageFullPath '*') -DestinationPath $archive -Force

    $env:DOTNET_CLI_HOME = Join-Path $repository 'out\dotnet-home'
    $env:NUGET_PACKAGES = Join-Path $env:DOTNET_CLI_HOME '.nuget\packages'
    # WiX does not track arbitrary payload files as MSBuild inputs. Clean first so
    # a changed executable, README, font, or FFmpeg binary cannot leave a stale MSI.
    Invoke-Checked $dotnet @(
        'clean',
        (Join-Path $PSScriptRoot 'PIP-Link.wixproj'),
        '--configuration', 'Release'
    )
    Invoke-Checked $dotnet @(
        'restore',
        (Join-Path $PSScriptRoot 'PIP-Link.wixproj'),
        '--configfile',
        (Join-Path $PSScriptRoot 'NuGet.Config')
    )
    Invoke-Checked $dotnet @(
        'build',
        (Join-Path $PSScriptRoot 'PIP-Link.wixproj'),
        '--configuration', 'Release',
        '--no-restore',
        "-p:ProductVersion=$Version",
        "-p:PayloadDirectory=$stageFullPath"
    )
    $builtMsi = Join-Path $PSScriptRoot "obj\Release\PIP-Link-v$Version-x64.msi"
    if (-not (Test-Path -LiteralPath $builtMsi -PathType Leaf)) {
        throw "WiX did not produce the expected MSI: $builtMsi"
    }
    Copy-Item -LiteralPath $builtMsi -Destination $packageRoot -Force
} finally {
    Pop-Location
}

Write-Host "Portable package: $archive"
Write-Host "MSI package:      $(Join-Path $packageRoot "PIP-Link-v$Version-x64.msi")"
