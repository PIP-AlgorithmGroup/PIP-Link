#include "media_pipeline.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    const std::filesystem::path output_directory = PIP_LINK_MEDIA_TEST_OUTPUT;
    std::error_code error_code;
    std::filesystem::remove_all(output_directory, error_code);
    std::filesystem::create_directories(output_directory, error_code);
    std::ifstream input(PIP_LINK_TEST_IMAGE, std::ios::binary);
    const std::vector<std::uint8_t> encoded{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (encoded.empty()) {
        std::cerr << "test image is unavailable\n";
        return 1;
    }

    pip_link::backend::media::FrameDecoder decoder;
    pip_link::backend::media::DecodedFrame frame;
    std::string error;
    if (!decoder.decode(0, encoded, frame, error) || frame.width <= 0 ||
        frame.height <= 0 || frame.bgra.empty()) {
        std::cerr << "WIC image decode failed: " << error << '\n';
        return 1;
    }
    if (argc > 1) {
        std::ifstream h264_input(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> h264{
            std::istreambuf_iterator<char>(h264_input), std::istreambuf_iterator<char>()};
        pip_link::backend::media::DecodedFrame h264_frame;
        bool h264_ok = !h264.empty();
        std::vector<std::size_t> access_unit_starts{0};
        bool saw_first_aud = false;
        for (std::size_t index = 0; index + 5 < h264.size(); ++index) {
            const bool start3 = h264[index] == 0 && h264[index + 1] == 0 &&
                                h264[index + 2] == 1;
            const bool start4 = !start3 && h264[index] == 0 && h264[index + 1] == 0 &&
                                h264[index + 2] == 0 && h264[index + 3] == 1;
            if (!start3 && !start4) continue;
            const std::size_t header = index + (start3 ? 3 : 4);
            if ((h264[header] & 0x1FU) == 9) {
                if (saw_first_aud) access_unit_starts.push_back(index);
                saw_first_aud = true;
            }
        }
        access_unit_starts.push_back(h264.size());
        for (std::size_t index = 0;
             h264_ok && h264_frame.bgra.empty() && index + 1 < access_unit_starts.size();
             ++index) {
            const std::size_t start = access_unit_starts[index];
            h264_ok = decoder.decode(
                1, std::span<const std::uint8_t>{h264}.subspan(
                       start, access_unit_starts[index + 1] - start),
                h264_frame, error);
        }
        if (!h264_ok ||
            h264_frame.width <= 0 || h264_frame.height <= 0 || h264_frame.bgra.empty()) {
            std::cerr << "H.264 decode failed: " << error << '\n';
            return 1;
        }
        pip_link::backend::media::StreamRecorder mp4_recorder;
        if (!mp4_recorder.start(output_directory, 0, 85, 0, 1, 30, error)) {
            std::cerr << "MP4 recorder start failed: " << error << '\n';
            return 1;
        }
        for (std::size_t index = 0; index + 1 < access_unit_starts.size(); ++index) {
            const std::size_t start = access_unit_starts[index];
            mp4_recorder.write(
                1, std::span<const std::uint8_t>{h264}.subspan(
                       start, access_unit_starts[index + 1] - start));
        }
        mp4_recorder.stop();
        if (!std::filesystem::exists(mp4_recorder.output_path()) ||
            std::filesystem::file_size(mp4_recorder.output_path()) == 0) {
            std::cerr << "MP4 recorder output is missing\n";
            return 1;
        }
    }
    const auto screenshot = output_directory / "frame.png";
    if (!pip_link::backend::media::save_png(screenshot, frame, error) ||
        !std::filesystem::exists(screenshot) || std::filesystem::file_size(screenshot) == 0) {
        std::cerr << "WIC PNG encode failed: " << error << '\n';
        return 1;
    }

    pip_link::backend::media::StreamRecorder recorder;
    if (!recorder.start(output_directory, 2, 85, 0, 0, 30, error)) {
        std::cerr << "raw recorder start failed: " << error << '\n';
        return 1;
    }
    recorder.write(0, encoded);
    recorder.stop();
    const auto recording = recorder.output_path();
    if (!std::filesystem::exists(recording) ||
        std::filesystem::file_size(recording) != encoded.size()) {
        std::cerr << "raw recorder output mismatch\n";
        return 1;
    }
    pip_link::backend::media::StreamRecorder codec_guard;
    if (!codec_guard.start(output_directory, 2, 85, 0, 0, 30, error)) {
        std::cerr << "codec guard recorder start failed: " << error << '\n';
        return 1;
    }
    codec_guard.write(1, encoded);
    if (codec_guard.healthy()) {
        std::cerr << "recorder ignored a stream codec change\n";
        return 1;
    }
    codec_guard.stop();
    std::filesystem::remove_all(output_directory, error_code);
    return 0;
}
