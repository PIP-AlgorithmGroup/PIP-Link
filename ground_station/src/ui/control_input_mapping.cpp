#include "pip_link/ui/control_input_mapping.hpp"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace pip_link::ui {
namespace {

constexpr std::array<KeyVisual, 71> key_visuals{{
    {ImGuiKey_Escape, 0, "ESC"}, {ImGuiKey_F1, 1, "F1"},
    {ImGuiKey_F2, 2, "F2"}, {ImGuiKey_F3, 3, "F3"},
    {ImGuiKey_F4, 4, "F4"}, {ImGuiKey_F5, 5, "F5"}, {ImGuiKey_F6, 6, "F6"},
    {ImGuiKey_F7, 7, "F7"}, {ImGuiKey_F8, 8, "F8"},
    {ImGuiKey_F9, 9, "F9"}, {ImGuiKey_F10, 10, "F10"},
    {ImGuiKey_F11, 11, "F11"}, {ImGuiKey_F12, 12, "F12"},
    {ImGuiKey_GraveAccent, 13, "`"}, {ImGuiKey_1, 14, "1"},
    {ImGuiKey_2, 15, "2"}, {ImGuiKey_3, 16, "3"},
    {ImGuiKey_4, 17, "4"}, {ImGuiKey_5, 18, "5"},
    {ImGuiKey_6, 19, "6"}, {ImGuiKey_7, 20, "7"},
    {ImGuiKey_8, 21, "8"}, {ImGuiKey_9, 22, "9"},
    {ImGuiKey_0, 23, "0"}, {ImGuiKey_Minus, 24, "-"},
    {ImGuiKey_Equal, 25, "="}, {ImGuiKey_Backspace, 26, "BS"},
    {ImGuiKey_Tab, 27, "TAB"}, {ImGuiKey_Q, 28, "Q"},
    {ImGuiKey_W, 29, "W"}, {ImGuiKey_E, 30, "E"},
    {ImGuiKey_R, 31, "R"}, {ImGuiKey_T, 32, "T"},
    {ImGuiKey_Y, 33, "Y"}, {ImGuiKey_U, 34, "U"},
    {ImGuiKey_I, 35, "I"}, {ImGuiKey_O, 36, "O"},
    {ImGuiKey_P, 37, "P"}, {ImGuiKey_LeftBracket, 38, "["},
    {ImGuiKey_RightBracket, 39, "]"}, {ImGuiKey_Backslash, 40, "\\"},
    {ImGuiKey_CapsLock, 41, "CAPS"}, {ImGuiKey_A, 42, "A"},
    {ImGuiKey_S, 43, "S"}, {ImGuiKey_D, 44, "D"},
    {ImGuiKey_F, 45, "F"}, {ImGuiKey_G, 46, "G"},
    {ImGuiKey_H, 47, "H"}, {ImGuiKey_J, 48, "J"},
    {ImGuiKey_K, 49, "K"}, {ImGuiKey_L, 50, "L"},
    {ImGuiKey_Semicolon, 51, ";"}, {ImGuiKey_Apostrophe, 52, "'"},
    {ImGuiKey_Enter, 53, "ENT"}, {ImGuiKey_LeftShift, 54, "LSHF"},
    {ImGuiKey_Z, 55, "Z"}, {ImGuiKey_X, 56, "X"},
    {ImGuiKey_C, 57, "C"}, {ImGuiKey_V, 58, "V"},
    {ImGuiKey_B, 59, "B"}, {ImGuiKey_N, 60, "N"},
    {ImGuiKey_M, 61, "M"}, {ImGuiKey_Comma, 62, ","},
    {ImGuiKey_Period, 63, "."}, {ImGuiKey_Slash, 64, "/"},
    {ImGuiKey_RightShift, 65, "RSHF"}, {ImGuiKey_LeftCtrl, 66, "LCTL"},
    {ImGuiKey_LeftAlt, 67, "LALT"}, {ImGuiKey_Space, 68, "SPC"},
    {ImGuiKey_RightAlt, 69, "RALT"}, {ImGuiKey_RightCtrl, 70, "RCTL"},
}};

constexpr std::array<int, 7> action_protocol_bits{29, 43, 42, 44, 54, 30, 45};
constexpr std::array<int, 7> action_default_keys{
    ImGuiKey_W, ImGuiKey_S, ImGuiKey_A, ImGuiKey_D,
    ImGuiKey_LeftShift, ImGuiKey_E, ImGuiKey_F,
};

void set_protocol_bit(backend::ControlInput& input, int bit) noexcept {
    input.keyboard[static_cast<std::size_t>(bit / 8)] |=
        static_cast<std::uint8_t>(1U << (bit % 8));
}

}  // namespace

std::span<const KeyVisual> protocol_key_visuals() noexcept {
    return key_visuals;
}

void map_physical_key(backend::ControlInput& input, int physical_key,
                      const std::array<int, 11>& bindings) noexcept {
    for (std::size_t index = 0; index < action_protocol_bits.size(); ++index) {
        if (bindings[index + 4] == physical_key) {
            set_protocol_bit(input, action_protocol_bits[index]);
            return;
        }
    }
    for (const int default_key : action_default_keys) {
        if (default_key == physical_key) return;
    }
    for (const auto& visual : key_visuals) {
        if (visual.imgui_key == physical_key) {
            set_protocol_bit(input, visual.protocol_bit);
            return;
        }
    }
}

}  // namespace pip_link::ui
