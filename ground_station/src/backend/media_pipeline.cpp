#include "media_pipeline.hpp"

#include <windows.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

namespace pip_link::backend::media {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

template <typename T>
void release(T*& pointer) noexcept {
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

std::string hresult_text(const char* action, HRESULT result) {
    std::ostringstream stream;
    stream << action << " (HRESULT=0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result) << ')';
    return stream.str();
}

std::wstring utf16(const std::filesystem::path& path) {
    return path.wstring();
}

std::wstring quote_argument(const std::wstring& value) {
    std::wstring quoted{L"\""};
    std::size_t slashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(character);
            slashes = 0;
            continue;
        }
        quoted.append(slashes, L'\\');
        slashes = 0;
        quoted.push_back(character);
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::string win32_error(const char* action) {
    std::ostringstream stream;
    stream << action << " (Win32=" << GetLastError() << ')';
    return stream.str();
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &value);
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y%m%d_%H%M%S");
    return stream.str();
}

void nv12_to_bgra(const std::uint8_t* input, int width, int height, int stride,
                  std::vector<std::uint8_t>& output) {
    output.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    const std::uint8_t* uv_plane = input + static_cast<std::size_t>(stride) * height;
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* y_row = input + static_cast<std::size_t>(y) * stride;
        const std::uint8_t* uv_row = uv_plane + static_cast<std::size_t>(y / 2) * stride;
        for (int x = 0; x < width; ++x) {
            const int luminance = std::max(0, static_cast<int>(y_row[x]) - 16);
            const int u = static_cast<int>(uv_row[x & ~1]) - 128;
            const int v = static_cast<int>(uv_row[(x & ~1) + 1]) - 128;
            const int c = 298 * luminance;
            const int red = (c + 409 * v + 128) >> 8;
            const int green = (c - 100 * u - 208 * v + 128) >> 8;
            const int blue = (c + 516 * u + 128) >> 8;
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width + x) * 4;
            output[offset] = static_cast<std::uint8_t>(std::clamp(blue, 0, 255));
            output[offset + 1] = static_cast<std::uint8_t>(std::clamp(green, 0, 255));
            output[offset + 2] = static_cast<std::uint8_t>(std::clamp(red, 0, 255));
            output[offset + 3] = 255;
        }
    }
}

class BitReader final {
public:
    explicit BitReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool bit(std::uint32_t& value) {
        if (offset_ >= bytes_.size() * 8) return false;
        value = (bytes_[offset_ / 8] >> (7U - offset_ % 8)) & 1U;
        ++offset_;
        return true;
    }

    bool bits(int count, std::uint32_t& value) {
        value = 0;
        for (int index = 0; index < count; ++index) {
            std::uint32_t current = 0;
            if (!bit(current)) return false;
            value = (value << 1U) | current;
        }
        return true;
    }

    bool unsigned_exp_golomb(std::uint32_t& value) {
        int leading_zeroes = 0;
        std::uint32_t current = 0;
        while (leading_zeroes < 31) {
            if (!bit(current)) return false;
            if (current != 0) break;
            ++leading_zeroes;
        }
        std::uint32_t suffix = 0;
        if (leading_zeroes > 0 && !bits(leading_zeroes, suffix)) return false;
        value = ((1U << leading_zeroes) - 1U) + suffix;
        return true;
    }

    bool signed_exp_golomb(std::int32_t& value) {
        std::uint32_t code = 0;
        if (!unsigned_exp_golomb(code)) return false;
        value = (code & 1U) != 0 ? static_cast<std::int32_t>((code + 1U) / 2U)
                                 : -static_cast<std::int32_t>(code / 2U);
        return true;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

bool skip_scaling_list(BitReader& reader, int size) {
    int last_scale = 8;
    int next_scale = 8;
    for (int index = 0; index < size; ++index) {
        if (next_scale != 0) {
            std::int32_t delta = 0;
            if (!reader.signed_exp_golomb(delta)) return false;
            next_scale = (last_scale + delta + 256) % 256;
        }
        last_scale = next_scale == 0 ? last_scale : next_scale;
    }
    return true;
}

std::optional<std::pair<UINT32, UINT32>> h264_dimensions(
    std::span<const std::uint8_t> encoded) {
    std::size_t nal_start = std::string::npos;
    for (std::size_t index = 0; index + 4 < encoded.size(); ++index) {
        const bool three = encoded[index] == 0 && encoded[index + 1] == 0 &&
                           encoded[index + 2] == 1;
        const bool four = !three && encoded[index] == 0 && encoded[index + 1] == 0 &&
                          encoded[index + 2] == 0 && encoded[index + 3] == 1;
        if (!three && !four) continue;
        const std::size_t header = index + (three ? 3 : 4);
        if (header < encoded.size() && (encoded[header] & 0x1FU) == 7) {
            nal_start = header + 1;
            break;
        }
    }
    if (nal_start == std::string::npos) return {};
    std::size_t nal_end = encoded.size();
    for (std::size_t index = nal_start; index + 3 < encoded.size(); ++index) {
        if (encoded[index] == 0 && encoded[index + 1] == 0 &&
            (encoded[index + 2] == 1 ||
             (encoded[index + 2] == 0 && encoded[index + 3] == 1))) {
            nal_end = index;
            break;
        }
    }
    std::vector<std::uint8_t> rbsp;
    rbsp.reserve(nal_end - nal_start);
    int zeroes = 0;
    for (std::size_t index = nal_start; index < nal_end; ++index) {
        const std::uint8_t byte = encoded[index];
        if (zeroes >= 2 && byte == 3) {
            zeroes = 0;
            continue;
        }
        rbsp.push_back(byte);
        zeroes = byte == 0 ? zeroes + 1 : 0;
    }
    BitReader reader(rbsp);
    std::uint32_t profile = 0;
    std::uint32_t ignored = 0;
    if (!reader.bits(8, profile) || !reader.bits(8, ignored) ||
        !reader.bits(8, ignored) || !reader.unsigned_exp_golomb(ignored)) return {};
    std::uint32_t chroma_format = 1;
    const bool high_profile = profile == 100 || profile == 110 || profile == 122 ||
                              profile == 244 || profile == 44 || profile == 83 ||
                              profile == 86 || profile == 118 || profile == 128 ||
                              profile == 138 || profile == 139 || profile == 134 ||
                              profile == 135;
    if (high_profile) {
        if (!reader.unsigned_exp_golomb(chroma_format)) return {};
        if (chroma_format == 3 && !reader.bit(ignored)) return {};
        if (!reader.unsigned_exp_golomb(ignored) ||
            !reader.unsigned_exp_golomb(ignored) || !reader.bit(ignored)) return {};
        std::uint32_t scaling = 0;
        if (!reader.bit(scaling)) return {};
        if (scaling != 0) {
            const int lists = chroma_format == 3 ? 12 : 8;
            for (int index = 0; index < lists; ++index) {
                std::uint32_t present = 0;
                if (!reader.bit(present)) return {};
                if (present != 0 && !skip_scaling_list(reader, index < 6 ? 16 : 64)) return {};
            }
        }
    }
    if (!reader.unsigned_exp_golomb(ignored)) return {};
    std::uint32_t picture_order = 0;
    if (!reader.unsigned_exp_golomb(picture_order)) return {};
    if (picture_order == 0) {
        if (!reader.unsigned_exp_golomb(ignored)) return {};
    } else if (picture_order == 1) {
        if (!reader.bit(ignored)) return {};
        std::int32_t signed_value = 0;
        if (!reader.signed_exp_golomb(signed_value) ||
            !reader.signed_exp_golomb(signed_value)) return {};
        std::uint32_t count = 0;
        if (!reader.unsigned_exp_golomb(count)) return {};
        for (std::uint32_t index = 0; index < count; ++index) {
            if (!reader.signed_exp_golomb(signed_value)) return {};
        }
    }
    if (!reader.unsigned_exp_golomb(ignored) || !reader.bit(ignored)) return {};
    std::uint32_t width_mbs = 0;
    std::uint32_t height_map_units = 0;
    if (!reader.unsigned_exp_golomb(width_mbs) ||
        !reader.unsigned_exp_golomb(height_map_units)) return {};
    std::uint32_t frame_only = 0;
    if (!reader.bit(frame_only)) return {};
    if (frame_only == 0 && !reader.bit(ignored)) return {};
    if (!reader.bit(ignored)) return {};
    std::uint32_t crop = 0;
    if (!reader.bit(crop)) return {};
    std::uint32_t crop_left = 0;
    std::uint32_t crop_right = 0;
    std::uint32_t crop_top = 0;
    std::uint32_t crop_bottom = 0;
    if (crop != 0 && (!reader.unsigned_exp_golomb(crop_left) ||
                      !reader.unsigned_exp_golomb(crop_right) ||
                      !reader.unsigned_exp_golomb(crop_top) ||
                      !reader.unsigned_exp_golomb(crop_bottom))) return {};
    const UINT32 sub_width = chroma_format == 1 || chroma_format == 2 ? 2U : 1U;
    const UINT32 sub_height = chroma_format == 1 ? 2U : 1U;
    const UINT32 crop_unit_x = chroma_format == 0 ? 1U : sub_width;
    const UINT32 crop_unit_y = chroma_format == 0
                                   ? 2U - frame_only
                                   : sub_height * (2U - frame_only);
    const UINT32 width = (width_mbs + 1U) * 16U -
                         (crop_left + crop_right) * crop_unit_x;
    const UINT32 height = (2U - frame_only) * (height_map_units + 1U) * 16U -
                          (crop_top + crop_bottom) * crop_unit_y;
    if (width == 0 || height == 0 || width > 8192 || height > 8192) return {};
    return std::pair{width, height};
}

}  // namespace

class FrameDecoder::Impl final {
public:
    Impl() {
        com_result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        mf_result_ = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                       CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&wic_factory_)))) {
            wic_available_ = true;
        }
    }

    ~Impl() {
        stop_ffmpeg_decoder();
        release(h264_decoder_);
        release(h264_output_type_);
        release(wic_factory_);
        if (SUCCEEDED(mf_result_)) MFShutdown();
        if (SUCCEEDED(com_result_)) CoUninitialize();
    }

    bool decode_jpeg(std::span<const std::uint8_t> encoded, DecodedFrame& output,
                     std::string& error) {
        if (!wic_available_) {
            error = "Windows Imaging Component 初始化失败";
            return false;
        }
        IWICStream* stream = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* source = nullptr;
        IWICFormatConverter* converter = nullptr;
        HRESULT result = wic_factory_->CreateStream(&stream);
        if (SUCCEEDED(result)) {
            result = stream->InitializeFromMemory(
                const_cast<BYTE*>(encoded.data()), static_cast<DWORD>(encoded.size()));
        }
        if (SUCCEEDED(result)) {
            result = wic_factory_->CreateDecoderFromStream(
                stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        }
        if (SUCCEEDED(result)) result = decoder->GetFrame(0, &source);
        if (SUCCEEDED(result)) result = wic_factory_->CreateFormatConverter(&converter);
        if (SUCCEEDED(result)) {
            result = converter->Initialize(source, GUID_WICPixelFormat32bppBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0.0,
                                           WICBitmapPaletteTypeCustom);
        }
        UINT width = 0;
        UINT height = 0;
        if (SUCCEEDED(result)) result = converter->GetSize(&width, &height);
        if (SUCCEEDED(result) && width > 0 && height > 0 && width <= 8192 && height <= 8192) {
            output.width = static_cast<int>(width);
            output.height = static_cast<int>(height);
            output.bgra.resize(static_cast<std::size_t>(width) * height * 4);
            result = converter->CopyPixels(nullptr, width * 4,
                                           static_cast<UINT>(output.bgra.size()),
                                           output.bgra.data());
        } else if (SUCCEEDED(result)) {
            result = E_INVALIDARG;
        }
        release(converter);
        release(source);
        release(decoder);
        release(stream);
        if (FAILED(result)) {
            error = hresult_text("JPEG 解码失败", result);
            return false;
        }
        return true;
    }

    bool initialize_h264(std::span<const std::uint8_t> encoded, std::string& error) {
        if (h264_decoder_ != nullptr) return true;
        if (FAILED(mf_result_)) {
            error = hresult_text("Media Foundation 初始化失败", mf_result_);
            return false;
        }
        MFT_REGISTER_TYPE_INFO input_info{MFMediaType_Video, MFVideoFormat_H264};
        IMFActivate** activations = nullptr;
        UINT32 activation_count = 0;
        HRESULT result = MFTEnumEx(
            MFT_CATEGORY_VIDEO_DECODER,
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
            &input_info, nullptr, &activations, &activation_count);
        if (SUCCEEDED(result) && activation_count == 0) result = MF_E_TOPO_CODEC_NOT_FOUND;
        if (SUCCEEDED(result)) {
            result = activations[0]->ActivateObject(IID_PPV_ARGS(&h264_decoder_));
        }
        for (UINT32 index = 0; index < activation_count; ++index) release(activations[index]);
        CoTaskMemFree(activations);
        IMFMediaType* input_type = nullptr;
        if (SUCCEEDED(result)) result = MFCreateMediaType(&input_type);
        if (SUCCEEDED(result)) result = input_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(result)) {
            result = input_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264_ES);
        }
        if (SUCCEEDED(result)) {
            result = input_type->SetUINT32(MF_MT_INTERLACE_MODE,
                                           MFVideoInterlace_Progressive);
        }
        if (SUCCEEDED(result)) {
            if (const auto dimensions = h264_dimensions(encoded)) {
                result = MFSetAttributeSize(input_type, MF_MT_FRAME_SIZE,
                                            dimensions->first, dimensions->second);
            }
        }
        if (SUCCEEDED(result)) result = h264_decoder_->SetInputType(0, input_type, 0);
        release(input_type);
        if (SUCCEEDED(result)) result = select_h264_output();
        if (SUCCEEDED(result)) {
            h264_decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
            h264_decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        }
        if (FAILED(result)) {
            release(h264_decoder_);
            error = hresult_text("H.264 解码器初始化失败", result);
            return false;
        }
        return true;
    }

    HRESULT select_h264_output() {
        release(h264_output_type_);
        IMFMediaType* fallback = nullptr;
        for (DWORD index = 0;; ++index) {
            IMFMediaType* candidate = nullptr;
            const HRESULT result = h264_decoder_->GetOutputAvailableType(0, index, &candidate);
            if (result == MF_E_NO_MORE_TYPES) break;
            if (FAILED(result)) {
                release(fallback);
                return result;
            }
            GUID subtype{};
            candidate->GetGUID(MF_MT_SUBTYPE, &subtype);
            if (subtype == MFVideoFormat_NV12) {
                release(fallback);
                const HRESULT set_result = h264_decoder_->SetOutputType(0, candidate, 0);
                if (SUCCEEDED(set_result)) h264_output_type_ = candidate;
                else release(candidate);
                return set_result;
            }
            if (fallback == nullptr) fallback = candidate;
            else release(candidate);
        }
        if (fallback == nullptr) return MF_E_INVALIDMEDIATYPE;
        release(fallback);
        return MF_E_INVALIDMEDIATYPE;
    }

    bool decode_h264(std::span<const std::uint8_t> encoded, DecodedFrame& output,
                     std::string& error) {
        if (!initialize_h264(encoded, error)) return false;
        IMFMediaBuffer* input_buffer = nullptr;
        IMFSample* input_sample = nullptr;
        HRESULT result = MFCreateMemoryBuffer(static_cast<DWORD>(encoded.size()), &input_buffer);
        BYTE* destination = nullptr;
        if (SUCCEEDED(result)) result = input_buffer->Lock(&destination, nullptr, nullptr);
        if (SUCCEEDED(result)) {
            std::copy(encoded.begin(), encoded.end(), destination);
            input_buffer->Unlock();
            result = input_buffer->SetCurrentLength(static_cast<DWORD>(encoded.size()));
        }
        if (SUCCEEDED(result)) result = MFCreateSample(&input_sample);
        if (SUCCEEDED(result)) result = input_sample->AddBuffer(input_buffer);
        if (SUCCEEDED(result)) {
            input_sample->SetSampleTime(sample_time_);
            input_sample->SetSampleDuration(333333);
            sample_time_ += 333333;
            result = h264_decoder_->ProcessInput(0, input_sample, 0);
        }
        release(input_sample);
        release(input_buffer);
        if (FAILED(result)) {
            error = hresult_text("H.264 输入失败", result);
            return false;
        }

        for (int attempt = 0; attempt < 4; ++attempt) {
            const char* failed_action = "查询 H.264 输出信息失败";
            MFT_OUTPUT_STREAM_INFO info{};
            result = h264_decoder_->GetOutputStreamInfo(0, &info);
            if (FAILED(result)) break;
            IMFSample* sample = nullptr;
            IMFMediaBuffer* buffer = nullptr;
            if ((info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
                failed_action = "创建 H.264 输出样本失败";
                result = MFCreateSample(&sample);
                if (SUCCEEDED(result)) {
                    const DWORD capacity = std::max<DWORD>(info.cbSize, 32U * 1024U * 1024U);
                    result = MFCreateMemoryBuffer(capacity, &buffer);
                }
                if (SUCCEEDED(result)) result = sample->AddBuffer(buffer);
            }
            MFT_OUTPUT_DATA_BUFFER output_data{};
            output_data.dwStreamID = 0;
            output_data.pSample = sample;
            DWORD status = 0;
            if (SUCCEEDED(result)) {
                failed_action = "H.264 ProcessOutput 失败";
                result = h264_decoder_->ProcessOutput(0, 1, &output_data, &status);
            }
            if (output_data.pEvents != nullptr) output_data.pEvents->Release();
            if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
                release(buffer);
                release(sample);
                failed_action = "切换 H.264 输出格式失败";
                result = select_h264_output();
                if (FAILED(result)) break;
                continue;
            }
            if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                release(buffer);
                release(sample);
                return true;
            }
            if (FAILED(result)) {
                release(buffer);
                release(sample);
                break;
            }
            IMFSample* result_sample = output_data.pSample;
            IMFMediaBuffer* contiguous = nullptr;
            failed_action = "合并 H.264 输出缓冲区失败";
            result = result_sample->ConvertToContiguousBuffer(&contiguous);
            BYTE* bytes = nullptr;
            DWORD length = 0;
            failed_action = "锁定 H.264 输出缓冲区失败";
            if (SUCCEEDED(result)) result = contiguous->Lock(&bytes, nullptr, &length);
            UINT32 width = 0;
            UINT32 height = 0;
            if (SUCCEEDED(result)) {
                failed_action = "读取 H.264 输出尺寸失败";
                result = MFGetAttributeSize(h264_output_type_, MF_MT_FRAME_SIZE,
                                            &width, &height);
            }
            LONG stride = static_cast<LONG>(width);
            if (SUCCEEDED(result)) {
                h264_output_type_->GetUINT32(MF_MT_DEFAULT_STRIDE,
                                             reinterpret_cast<UINT32*>(&stride));
                if (stride < static_cast<LONG>(width)) stride = static_cast<LONG>(width);
                const std::size_t required = static_cast<std::size_t>(stride) * height * 3 / 2;
                if (width == 0 || height == 0 || length < required) {
                    failed_action = "H.264 输出缓冲区尺寸无效";
                    result = E_UNEXPECTED;
                }
            }
            if (SUCCEEDED(result)) {
                output.width = static_cast<int>(width);
                output.height = static_cast<int>(height);
                nv12_to_bgra(bytes, output.width, output.height, stride, output.bgra);
            }
            if (bytes != nullptr) contiguous->Unlock();
            release(contiguous);
            if (output_data.pSample != sample) release(output_data.pSample);
            release(buffer);
            release(sample);
            if (FAILED(result)) {
                error = hresult_text(failed_action, result);
                return false;
            }
            return true;
        }
        error = hresult_text("查询 H.264 输出信息失败", result);
        return false;
    }

    bool start_ffmpeg_decoder(std::span<const std::uint8_t> encoded, std::string& error) {
        if (ffmpeg_process_.hProcess != nullptr) return true;
        const auto dimensions = h264_dimensions(encoded);
        if (!dimensions) {
            error = "H.264 码流中没有可用的 SPS 尺寸信息";
            return false;
        }
        ffmpeg_width_ = static_cast<int>(dimensions->first);
        ffmpeg_height_ = static_cast<int>(dimensions->second);
        wchar_t executable[MAX_PATH]{};
        if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, executable, nullptr) == 0) {
            error = "Windows H.264 解码器不可用，且 PATH 中未找到 ffmpeg.exe";
            return false;
        }
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE stdin_read = nullptr;
        HANDLE stdout_write = nullptr;
        if (!CreatePipe(&stdin_read, &ffmpeg_stdin_, &security, 1024 * 1024) ||
            !CreatePipe(&ffmpeg_stdout_, &stdout_write, &security, 1024 * 1024)) {
            if (stdin_read != nullptr) CloseHandle(stdin_read);
            if (stdout_write != nullptr) CloseHandle(stdout_write);
            stop_ffmpeg_decoder();
            error = win32_error("无法创建 H.264 解码管道");
            return false;
        }
        SetHandleInformation(ffmpeg_stdin_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(ffmpeg_stdout_, HANDLE_FLAG_INHERIT, 0);
        HANDLE null_output = CreateFileW(L"NUL", GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        std::wstring command = quote_argument(executable) +
            L" -hide_banner -loglevel error -flags low_delay -probesize 32"
            L" -analyzeduration 0 -f h264 -i pipe:0 -an"
            L" -f rawvideo -pix_fmt bgra pipe:1";
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = stdin_read;
        startup.hStdOutput = stdout_write;
        startup.hStdError = null_output;
        const BOOL created = CreateProcessW(
            executable, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &ffmpeg_process_);
        CloseHandle(stdin_read);
        CloseHandle(stdout_write);
        if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
        if (!created) {
            error = win32_error("无法启动 FFmpeg H.264 解码器");
            stop_ffmpeg_decoder();
            return false;
        }
        return true;
    }

    void stop_ffmpeg_decoder() {
        if (ffmpeg_stdin_ != nullptr) {
            CloseHandle(ffmpeg_stdin_);
            ffmpeg_stdin_ = nullptr;
        }
        if (ffmpeg_stdout_ != nullptr) {
            CloseHandle(ffmpeg_stdout_);
            ffmpeg_stdout_ = nullptr;
        }
        if (ffmpeg_process_.hProcess != nullptr) {
            if (WaitForSingleObject(ffmpeg_process_.hProcess, 1000) == WAIT_TIMEOUT) {
                TerminateProcess(ffmpeg_process_.hProcess, 1);
                WaitForSingleObject(ffmpeg_process_.hProcess, 500);
            }
            CloseHandle(ffmpeg_process_.hThread);
            CloseHandle(ffmpeg_process_.hProcess);
            ffmpeg_process_ = {};
        }
        ffmpeg_bytes_.clear();
        ffmpeg_offset_ = 0;
        ffmpeg_width_ = 0;
        ffmpeg_height_ = 0;
    }

    bool decode_h264_ffmpeg(std::span<const std::uint8_t> encoded,
                            DecodedFrame& output, std::string& error) {
        if (!start_ffmpeg_decoder(encoded, error)) return false;
        std::size_t offset = 0;
        while (offset < encoded.size()) {
            DWORD written = 0;
            const DWORD count = static_cast<DWORD>(std::min<std::size_t>(
                encoded.size() - offset, std::numeric_limits<DWORD>::max()));
            if (!WriteFile(ffmpeg_stdin_, encoded.data() + offset, count, &written, nullptr) ||
                written == 0) {
                error = win32_error("FFmpeg H.264 输入失败");
                stop_ffmpeg_decoder();
                return false;
            }
            offset += written;
        }
        const std::size_t frame_size = static_cast<std::size_t>(ffmpeg_width_) *
                                       ffmpeg_height_ * 4;
        const auto deadline = Clock::now() + 80ms;
        std::array<std::uint8_t, 256 * 1024> buffer{};
        while (Clock::now() < deadline && ffmpeg_bytes_.size() - ffmpeg_offset_ < frame_size) {
            DWORD available = 0;
            if (!PeekNamedPipe(ffmpeg_stdout_, nullptr, 0, nullptr, &available, nullptr)) {
                error = win32_error("FFmpeg H.264 输出失败");
                stop_ffmpeg_decoder();
                return false;
            }
            if (available == 0) {
                if (WaitForSingleObject(ffmpeg_process_.hProcess, 0) == WAIT_OBJECT_0) {
                    error = "FFmpeg H.264 解码进程意外退出";
                    stop_ffmpeg_decoder();
                    return false;
                }
                std::this_thread::sleep_for(1ms);
                continue;
            }
            DWORD read = 0;
            const DWORD count = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            if (!ReadFile(ffmpeg_stdout_, buffer.data(), count, &read, nullptr) || read == 0) {
                error = win32_error("读取 FFmpeg H.264 输出失败");
                stop_ffmpeg_decoder();
                return false;
            }
            if (ffmpeg_offset_ > 0 && ffmpeg_offset_ * 2 >= ffmpeg_bytes_.size()) {
                ffmpeg_bytes_.erase(
                    ffmpeg_bytes_.begin(),
                    ffmpeg_bytes_.begin() + static_cast<std::ptrdiff_t>(ffmpeg_offset_));
                ffmpeg_offset_ = 0;
            }
            ffmpeg_bytes_.insert(ffmpeg_bytes_.end(), buffer.begin(), buffer.begin() + read);
        }
        if (ffmpeg_bytes_.size() - ffmpeg_offset_ < frame_size) return true;
        output.width = ffmpeg_width_;
        output.height = ffmpeg_height_;
        const auto begin = ffmpeg_bytes_.begin() +
                           static_cast<std::ptrdiff_t>(ffmpeg_offset_);
        output.bgra.assign(begin, begin + static_cast<std::ptrdiff_t>(frame_size));
        ffmpeg_offset_ += frame_size;
        if (ffmpeg_offset_ == ffmpeg_bytes_.size()) {
            ffmpeg_bytes_.clear();
            ffmpeg_offset_ = 0;
        }
        return true;
    }

    bool decode_h264_with_fallback(std::span<const std::uint8_t> encoded,
                                   DecodedFrame& output, std::string& error) {
        if (use_ffmpeg_) return decode_h264_ffmpeg(encoded, output, error);
        if (h264_probe_bytes_.size() + encoded.size() <= 8U * 1024U * 1024U) {
            h264_probe_bytes_.insert(h264_probe_bytes_.end(), encoded.begin(), encoded.end());
        }
        std::string media_foundation_error;
        if (decode_h264(encoded, output, media_foundation_error)) {
            if (!output.bgra.empty()) {
                h264_probe_bytes_.clear();
                mf_empty_outputs_ = 0;
                return true;
            }
            if (++mf_empty_outputs_ < 3) return true;
        }
        use_ffmpeg_ = true;
        release(h264_decoder_);
        release(h264_output_type_);
        const std::span<const std::uint8_t> initial = h264_probe_bytes_.empty()
            ? encoded : std::span<const std::uint8_t>{h264_probe_bytes_};
        if (!decode_h264_ffmpeg(initial, output, error)) {
            error = media_foundation_error + "; " + error;
            return false;
        }
        h264_probe_bytes_.clear();
        return true;
    }

    void reset() {
        if (h264_decoder_ != nullptr) {
            h264_decoder_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
            h264_decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        }
        sample_time_ = 0;
        stop_ffmpeg_decoder();
        use_ffmpeg_ = false;
        mf_empty_outputs_ = 0;
        h264_probe_bytes_.clear();
    }

    void set_h264_preference(int preference) noexcept {
        h264_preference_ = std::clamp(preference, 0, 2);
    }

private:
    friend class FrameDecoder;
    HRESULT com_result_{E_FAIL};
    HRESULT mf_result_{E_FAIL};
    IWICImagingFactory* wic_factory_{nullptr};
    bool wic_available_{false};
    IMFTransform* h264_decoder_{nullptr};
    IMFMediaType* h264_output_type_{nullptr};
    LONGLONG sample_time_{};
    bool use_ffmpeg_{};
    std::atomic_int h264_preference_{};
    int mf_empty_outputs_{};
    std::vector<std::uint8_t> h264_probe_bytes_;
    HANDLE ffmpeg_stdin_{nullptr};
    HANDLE ffmpeg_stdout_{nullptr};
    PROCESS_INFORMATION ffmpeg_process_{};
    int ffmpeg_width_{};
    int ffmpeg_height_{};
    std::vector<std::uint8_t> ffmpeg_bytes_;
    std::size_t ffmpeg_offset_{};
};

FrameDecoder::FrameDecoder() : impl_(std::make_unique<Impl>()) {}
FrameDecoder::~FrameDecoder() = default;

bool FrameDecoder::decode(std::uint8_t codec, std::span<const std::uint8_t> encoded,
                          DecodedFrame& output, std::string& error) {
    if (encoded.empty()) {
        error = "收到空视频帧";
        return false;
    }
    if (codec == 0) return impl_->decode_jpeg(encoded, output, error);
    if (impl_->h264_preference_ == 2) {
        impl_->use_ffmpeg_ = true;
        return impl_->decode_h264_ffmpeg(encoded, output, error);
    }
    if (impl_->h264_preference_ == 1) return impl_->decode_h264(encoded, output, error);
    return impl_->decode_h264_with_fallback(encoded, output, error);
}

void FrameDecoder::set_h264_preference(int preference) noexcept {
    impl_->set_h264_preference(preference);
}

void FrameDecoder::reset() { impl_->reset(); }

class StreamRecorder::Impl final {
public:
    ~Impl() { stop(); }

    bool start(const std::filesystem::path& directory, int format_index, int quality,
               int split_minutes, std::uint8_t codec, int frame_rate,
               std::string& error) {
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        stop_unlocked();
        std::error_code filesystem_error;
        std::filesystem::create_directories(directory, filesystem_error);
        if (filesystem_error) {
            error = "无法创建录像目录: " + filesystem_error.message();
            return false;
        }
        codec_ = codec;
        failed_ = false;
        error_.clear();
        const std::string suffix = timestamp();
        if (format_index == 2) {
            output_path_ = directory / ("pip_link_" + suffix + (codec == 0 ? ".mjpeg" : ".h264"));
            raw_file_.open(output_path_, std::ios::binary | std::ios::trunc);
            if (!raw_file_) {
                error = "无法创建原始码流文件";
                return false;
            }
        } else {
            wchar_t ffmpeg_path[MAX_PATH]{};
            if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, ffmpeg_path, nullptr) == 0) {
                error = "MP4/MKV 录像需要 ffmpeg.exe，请将其加入 PATH；原始码流不需要";
                return false;
            }
            const wchar_t* extension = format_index == 0 ? L".mp4" : L".mkv";
            std::wstring filename = L"pip_link_";
            filename.append(suffix.begin(), suffix.end());
            if (split_minutes > 0) filename += L"_%03d";
            filename += extension;
            output_path_ = directory / filename;
            SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
            HANDLE read_pipe = nullptr;
            HANDLE write_pipe = nullptr;
            if (!CreatePipe(&read_pipe, &write_pipe, &security, 1024 * 1024)) {
                error = win32_error("无法创建 FFmpeg 输入管道");
                return false;
            }
            stdin_write_.store(write_pipe);
            SetHandleInformation(write_pipe, HANDLE_FLAG_INHERIT, 0);
            HANDLE null_output = CreateFileW(L"NUL", GENERIC_WRITE,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            std::wstring command = quote_argument(ffmpeg_path);
            command += L" -hide_banner -loglevel error -y -probesize 32 -analyzeduration 0 -f ";
            command += codec == 0 ? L"mjpeg" : L"h264";
            command += L" -r " + std::to_wstring(std::clamp(frame_rate, 1, 240));
            command += L" -i pipe:0 -map 0:v:0 -an ";
            if (codec == 0) {
                const int crf = std::clamp(51 - quality / 2, 0, 51);
                command += L"-c:v libx264 -preset veryfast -tune zerolatency -crf " +
                           std::to_wstring(crf) + L" ";
            } else {
                command += L"-c:v copy ";
            }
            if (split_minutes > 0) {
                command += L"-f segment -segment_time " +
                           std::to_wstring(split_minutes * 60) +
                           L" -reset_timestamps 1 ";
            }
            command += quote_argument(utf16(output_path_));
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = read_pipe;
            startup.hStdOutput = null_output;
            startup.hStdError = null_output;
            std::vector<wchar_t> mutable_command(command.begin(), command.end());
            mutable_command.push_back(L'\0');
            const BOOL created = CreateProcessW(
                ffmpeg_path, mutable_command.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_);
            CloseHandle(read_pipe);
            if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
            if (!created) {
                CloseHandle(stdin_write_.exchange(nullptr));
                error = win32_error("无法启动 FFmpeg");
                return false;
            }
        }
        running_ = true;
        writer_ = std::thread([this] { writer_loop(); });
        return true;
    }

    void write(std::uint8_t codec, std::span<const std::uint8_t> frame) {
        if (!running_) return;
        if (codec != codec_) {
            fail("视频编码格式已变化，录像已停止");
            return;
        }
        std::lock_guard lock(mutex_);
        if (!running_) return;
        if (queue_.size() >= 120) queue_.pop_front();
        queue_.emplace_back(frame.begin(), frame.end());
        condition_.notify_one();
    }

    void writer_loop() {
        for (;;) {
            std::vector<std::uint8_t> frame;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return !running_ || !queue_.empty(); });
                if (queue_.empty() && !running_) {
                    writer_busy_ = false;
                    drained_.notify_all();
                    break;
                }
                frame = std::move(queue_.front());
                queue_.pop_front();
                writer_busy_ = true;
            }
            if (raw_file_) {
                raw_file_.write(reinterpret_cast<const char*>(frame.data()),
                                static_cast<std::streamsize>(frame.size()));
                if (!raw_file_) fail("写入原始码流失败");
            } else if (const HANDLE input = stdin_write_.load(); input != nullptr) {
                std::size_t offset = 0;
                while (offset < frame.size()) {
                    DWORD written = 0;
                    const DWORD count = static_cast<DWORD>(std::min<std::size_t>(
                        frame.size() - offset, std::numeric_limits<DWORD>::max()));
                    if (!WriteFile(input, frame.data() + offset, count, &written, nullptr) ||
                        written == 0) {
                        if (running_) fail(win32_error("FFmpeg 管道写入失败"));
                        break;
                    }
                    offset += written;
                }
            }
            {
                std::lock_guard lock(mutex_);
                writer_busy_ = false;
            }
            drained_.notify_all();
        }
    }

    void fail(std::string message) {
        std::lock_guard lock(mutex_);
        failed_ = true;
        error_ = std::move(message);
    }

    void stop() {
        std::lock_guard lifecycle_lock(lifecycle_mutex_);
        stop_unlocked();
    }

    void stop_unlocked() {
        running_ = false;
        condition_.notify_all();
        bool drained = true;
        {
            std::unique_lock lock(mutex_);
            drained = drained_.wait_for(lock, std::chrono::milliseconds(500),
                                        [this] { return queue_.empty() && !writer_busy_; });
            if (!drained) queue_.clear();
        }
        if (!drained && writer_.joinable()) {
            CancelSynchronousIo(writer_.native_handle());
            if (const HANDLE input = stdin_write_.exchange(nullptr); input != nullptr) {
                CloseHandle(input);
            }
        }
        if (writer_.joinable()) writer_.join();
        if (const HANDLE input = stdin_write_.exchange(nullptr); input != nullptr) {
            CloseHandle(input);
        }
        raw_file_.close();
        if (process_.hProcess != nullptr) {
            if (WaitForSingleObject(process_.hProcess, 5000) == WAIT_TIMEOUT) {
                TerminateProcess(process_.hProcess, 1);
                WaitForSingleObject(process_.hProcess, 1000);
            }
            CloseHandle(process_.hThread);
            CloseHandle(process_.hProcess);
            process_ = {};
        }
    }

    bool healthy() const {
        std::lock_guard lock(mutex_);
        return !failed_;
    }

    std::string last_error() const {
        std::lock_guard lock(mutex_);
        return error_;
    }

    std::atomic_bool running_{false};
    std::mutex lifecycle_mutex_;
    std::uint8_t codec_{};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::condition_variable drained_;
    std::deque<std::vector<std::uint8_t>> queue_;
    std::thread writer_;
    bool writer_busy_{};
    std::ofstream raw_file_;
    std::atomic<HANDLE> stdin_write_{nullptr};
    PROCESS_INFORMATION process_{};
    bool failed_{false};
    std::string error_;
    std::filesystem::path output_path_;
};

StreamRecorder::StreamRecorder() : impl_(std::make_unique<Impl>()) {}
StreamRecorder::~StreamRecorder() = default;

bool StreamRecorder::start(const std::filesystem::path& directory, int format_index,
                           int quality, int split_minutes, std::uint8_t codec,
                           int frame_rate,
                           std::string& error) {
    return impl_->start(directory, format_index, quality, split_minutes, codec,
                        frame_rate, error);
}

void StreamRecorder::write(std::uint8_t codec, std::span<const std::uint8_t> frame) {
    impl_->write(codec, frame);
}

void StreamRecorder::stop() { impl_->stop(); }
bool StreamRecorder::active() const noexcept { return impl_->running_; }
bool StreamRecorder::healthy() const { return impl_->healthy(); }
std::string StreamRecorder::last_error() const { return impl_->last_error(); }
std::filesystem::path StreamRecorder::output_path() const { return impl_->output_path_; }

bool save_png(const std::filesystem::path& path, const DecodedFrame& frame,
              std::string& error) {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.bgra.size() != static_cast<std::size_t>(frame.width) * frame.height * 4) {
        error = "没有可保存的视频帧";
        return false;
    }
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* target = nullptr;
    IPropertyBag2* properties = nullptr;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
    const std::wstring wide_path = path.wstring();
    if (SUCCEEDED(result)) {
        result = stream->InitializeFromFilename(wide_path.c_str(), GENERIC_WRITE);
    }
    if (SUCCEEDED(result)) {
        result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    }
    if (SUCCEEDED(result)) result = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&target, &properties);
    if (SUCCEEDED(result)) result = target->Initialize(properties);
    if (SUCCEEDED(result)) {
        result = target->SetSize(static_cast<UINT>(frame.width),
                                 static_cast<UINT>(frame.height));
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result)) result = target->SetPixelFormat(&format);
    if (SUCCEEDED(result) && format != GUID_WICPixelFormat32bppBGRA) {
        result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    if (SUCCEEDED(result)) {
        result = target->WritePixels(static_cast<UINT>(frame.height),
                                     static_cast<UINT>(frame.width * 4),
                                     static_cast<UINT>(frame.bgra.size()),
                                     const_cast<BYTE*>(frame.bgra.data()));
    }
    if (SUCCEEDED(result)) result = target->Commit();
    if (SUCCEEDED(result)) result = encoder->Commit();
    release(properties);
    release(target);
    release(encoder);
    release(stream);
    release(factory);
    if (SUCCEEDED(com_result)) CoUninitialize();
    if (FAILED(result)) {
        error = hresult_text("PNG 截图保存失败", result);
        return false;
    }
    return true;
}

}  // namespace pip_link::backend::media
