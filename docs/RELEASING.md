# Windows 打包与发布

本文档是 PIP-Link v3 Windows 发行的唯一流程说明。最终同时发布便携 ZIP 和仅当前用户安装的 MSI；二者都必须包含 `PIP-Link.exe`、FFmpeg、中文字体及相应许可证。

## 1. 发布前准备

构建机需要：

- CMake、Ninja，以及 MSVC 2022 或 MinGW-w64；
- .NET SDK 7 或更高版本，用于恢复 WiX 5 SDK 并生成 MSI；
- 可再分发的 64 位 `ffmpeg.exe` 及其对应许可证文件；
- Git；发布到 GitHub 时还需要已登录的 GitHub CLI `gh`。

发布版本必须同时满足：

- `CMakeLists.txt` 中的 `project(... VERSION 3.0.1)`；
- 构建脚本参数为 `-Version 3.0.1`；
- Git 标签为 `v3.0.1`；
- 文件名为 `PIP-Link-v3.0.1-win64.zip` 和 `PIP-Link-v3.0.1-x64.msi`。

## 2. 一键构建 ZIP 和 MSI

在仓库根目录打开 PowerShell：

```powershell
.\packaging\windows\build-release.ps1 `
  -Version 3.0.1 `
  -FfmpegExecutable "E:\ffmpeg\bin\ffmpeg.exe" `
  -FfmpegLicense "E:\ffmpeg\LICENSE.txt"
```

把两个 FFmpeg 路径替换为本机构建文件的真实路径。如果 `ffmpeg.exe` 已加入 `PATH`，可以省略 `-FfmpegExecutable`；脚本仍会验证许可证是否存在。若 PowerShell 阻止本地脚本，可只对当前进程放开：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
```

脚本依次执行 Release 配置与编译、全部 CTest、安装到临时目录、复制运行时依赖、压缩 ZIP、恢复 WiX 依赖并构建 MSI。任何一步失败都会终止，不会把不完整产物当作发行版。

最终产物位于：

```text
out/package/
├── PIP-Link-v3.0.1-win64.zip
└── PIP-Link-v3.0.1-x64.msi
```

## 3. 手工执行各阶段

需要排查某一阶段时，可按以下顺序手工执行。

### 3.1 编译与测试

```powershell
cmake --preset release
cmake --build --preset release -j 26
ctest --test-dir out/build/release --output-on-failure
```

Release 程序是 Windows GUI 子系统，启动时不会出现终端窗口。

### 3.2 构建便携目录

```powershell
$stage = "$PWD\out\package\staging\PIP-Link-v3.0.1-win64"
cmake --install out/build/release --prefix $stage
Copy-Item "E:\ffmpeg\bin\ffmpeg.exe" "$stage\ffmpeg.exe"
Copy-Item "E:\ffmpeg\LICENSE.txt" "$stage\FFmpeg-LICENSE.txt"
Copy-Item LICENSE "$stage\LICENSE.txt"
Copy-Item README.md $stage
```

便携目录的根目录必须直接包含主程序：

```text
PIP-Link-v3.0.1-win64/
├── PIP-Link.exe
├── ffmpeg.exe
├── README.md
├── LICENSE.txt
├── FFmpeg-LICENSE.txt
└── assets/
    ├── icon.bmp
    ├── icon.ico
    └── fonts/
        ├── NotoSansCJKsc-Regular.otf
        └── NotoSansCJK-LICENSE.txt
```

`settings.ini`、`audit.jsonl` 以及用户导出的诊断文件在程序首次运行后写入 `PIP-Link.exe` 所在目录，不应预先放进发行包。

### 3.3 生成 ZIP

```powershell
Compress-Archive `
  -Path "$stage\*" `
  -DestinationPath "$PWD\out\package\PIP-Link-v3.0.1-win64.zip" `
  -Force
```

### 3.4 生成 MSI

```powershell
$env:DOTNET_CLI_HOME = "$PWD\out\dotnet-home"
$env:NUGET_PACKAGES = "$env:DOTNET_CLI_HOME\.nuget\packages"

dotnet clean packaging\windows\PIP-Link.wixproj --configuration Release

dotnet restore packaging\windows\PIP-Link.wixproj `
  --configfile packaging\windows\NuGet.Config

dotnet build packaging\windows\PIP-Link.wixproj `
  --configuration Release `
  --no-restore `
  -p:ProductVersion=3.0.1 `
  -p:PayloadDirectory="$stage"

Copy-Item `
  packaging\windows\obj\Release\PIP-Link-v3.0.1-x64.msi `
  out\package\PIP-Link-v3.0.1-x64.msi `
  -Force
```

MSI 安装到 `%LOCALAPPDATA%\Programs\PIP-Link`，不要求管理员权限。安装向导提供中文许可页、安装目录、确认页和真实进度条；不要把只有“正在配置”而无进度反馈的旧 MSI 用作发行版。

安装包设置 `MSIFASTINSTALL=1`，避免 Windows Installer 在开始复制文件前同步创建系统还原点。对不修改系统目录和系统服务的仅当前用户安装而言，还原点没有必要，并可能让进度页长时间停留在 0%。

## 4. 发行验收

发布前逐项检查：

1. `ctest --test-dir out/build/release --output-on-failure` 全部通过；
2. 双击 ZIP 中的 `PIP-Link.exe`，无额外终端窗口且中文正常显示；
3. ZIP 与 MSI 均含 `ffmpeg.exe`、Noto Sans CJK SC 及两份第三方许可证；
4. MSI 全新安装、覆盖升级、卸载各执行一次，向导进度会变化且 60 秒内结束；
5. 安装后从开始菜单启动程序，确认程序目录可生成 `settings.ini` 和 `audit.jsonl`；
6. 录像、截图、连接、设置保存至少进行一次冒烟测试。

需要记录 MSI 详细日志时执行：

```powershell
msiexec /i .\out\package\PIP-Link-v3.0.1-x64.msi /l*v .\out\package\install.log
```

静默验收可执行：

```powershell
msiexec /i .\out\package\PIP-Link-v3.0.1-x64.msi /qn /norestart /l*v .\out\package\install-silent.log
```

返回码 `0` 表示成功；`1602` 表示用户取消，不是构建成功。

## 5. 提交、打标签和发布 GitHub Release

确认工作区、提交和远端分支正确后：

```powershell
git status
git push origin master
git tag -a v3.0.1 -m "PIP-Link v3.0.1"
git push origin v3.0.1
```

创建 Release 并上传两种安装形式：

```powershell
gh release create v3.0.1 `
  "out/package/PIP-Link-v3.0.1-x64.msi" `
  "out/package/PIP-Link-v3.0.1-win64.zip" `
  --verify-tag `
  --title "PIP-Link v3.0.1" `
  --generate-notes `
  --latest
```

最后检查远端信息和附件：

```powershell
gh release view v3.0.1 --json url,tagName,name,assets,publishedAt
```

已发布版本不要移动或重建同名标签。需要再次修复安装器时提升补丁版本，例如 `3.0.2`，重新执行整套流程。
