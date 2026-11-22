#include <windows.h>
#include <Xinput.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "oracles/d3d11_renderer.hpp"
#include "oracles/game_runner.hpp"
#include "oracles/input_bindings.hpp"
#include "oracles/waveout_audio.hpp"

#pragma comment(lib, "xinput9_1_0.lib")
#pragma comment(lib, "dxgi.lib")

namespace oracles::generated {
std::size_t ages_block_count();
std::size_t seasons_block_count();
std::uint16_t ages_step(oracles::Runtime&, std::uint16_t, std::uint16_t);
std::uint16_t seasons_step(oracles::Runtime&, std::uint16_t, std::uint16_t);
}

namespace {
constexpr wchar_t kLauncherClassName[] = L"OraclesPCRecompLauncher";
constexpr wchar_t kGameClassName[] = L"OraclesPCRecompGame";
constexpr int ID_GAME = 1001;
constexpr int ID_GPU = 1002;
constexpr int ID_REFRESH_GPUS = 1003;
constexpr int ID_ASPECT = 1004;
constexpr int ID_WINDOW_MODE = 1005;
constexpr int ID_LAUNCH = 1006;
constexpr int ID_FPS = 1007;
constexpr int ID_CONTROLLER = 1008;
constexpr int ID_BIND_BASE = 1100;
constexpr wchar_t kAutomaticGpu[] = L"Automatic (Windows default adapter)";
constexpr std::array<const wchar_t*, 8> kActionNames{
    L"D-pad Up", L"D-pad Down", L"D-pad Left", L"D-pad Right",
    L"A", L"B", L"Start", L"Select"
};

struct GpuChoice {
    IDXGIAdapter1* adapter{};
    std::wstring description;
};

struct AppState {
    HWND launcher_window{};
    HWND game_window{};
    HFONT ui_font{};
    std::vector<HWND> launcher_controls;
    std::array<HWND, 8> binding_buttons{};
    HWND game_combo{};
    HWND gpu_combo{};
    HWND aspect_combo{};
    HWND window_mode_combo{};
    HWND fps_combo{};
    HWND controller_combo{};
    HWND launch_button{};
    HWND refresh_button{};
    std::vector<GpuChoice> gpus;

    std::array<unsigned, 8> key_bindings = oracles::input::kDefaultVirtualKeys;
    std::array<bool, 8> key_down{};
    std::uint8_t joypad_pressed{};
    XINPUT_STATE controller{};
    bool controller_connected{};
    int selected_controller{-1}; // -1 = first connected XInput slot
    int fps_limit{60};           // 0 = uncapped presentation (native game timing remains fixed)
    double next_simulation_time{};
    double next_present_time{};
    bool frame_pending{};
    bool timer_resolution_active{};
    oracles::WaveOutAudio audio_output;

    bool widescreen{};
    bool fullscreen{};
    bool launching{};
    int capture_action{-1};
    int selected_game{};
    int selected_gpu_combo_index{};

    std::unique_ptr<oracles::D3D11Renderer> renderer;
    std::unique_ptr<oracles::GameRunner> runner;
    std::wstring last_launch_error;
    std::wstring active_game;
    std::wstring renderer_status;
} g;

double monotonic_seconds() {
    static const double frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return frequency > 0.0 ? static_cast<double>(counter.QuadPart) / frequency : 0.0;
}

std::filesystem::path executable_directory() {
    wchar_t buffer[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return std::filesystem::path(std::wstring(buffer, length)).parent_path();
}

std::filesystem::path settings_path() {
    const auto directory = executable_directory() / L"config";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory / L"settings.ini";
}

std::wstring read_setting(const wchar_t* section, const wchar_t* key,
                          const wchar_t* fallback) {
    wchar_t value[512]{};
    GetPrivateProfileStringW(section, key, fallback, value,
        static_cast<DWORD>(std::size(value)), settings_path().c_str());
    return value;
}

unsigned read_key_setting(const wchar_t* key, unsigned fallback) {
    const UINT value = GetPrivateProfileIntW(L"Keyboard", key,
        static_cast<INT>(fallback), settings_path().c_str());
    return value <= 0xFFFFu ? value : fallback;
}

void write_setting(const wchar_t* section, const wchar_t* key,
                   const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), settings_path().c_str());
}

oracles::DisplayAspect selected_aspect() {
    return g.widescreen ? oracles::DisplayAspect::Widescreen16x9
                        : oracles::DisplayAspect::Aspect4x3;
}

std::wstring key_name(unsigned virtual_key) {
    switch (virtual_key) {
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_RETURN: return L"Enter";
    case VK_RSHIFT: return L"Right Shift";
    case VK_LSHIFT: return L"Left Shift";
    case VK_SPACE: return L"Space";
    case VK_TAB: return L"Tab";
    case VK_ESCAPE: return L"Escape";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"Page Up";
    case VK_NEXT: return L"Page Down";
    case VK_LCONTROL: return L"Left Ctrl";
    case VK_RCONTROL: return L"Right Ctrl";
    case VK_LMENU: return L"Left Alt";
    case VK_RMENU: return L"Right Alt";
    default: break;
    }
    if (virtual_key >= 'A' && virtual_key <= 'Z')
        return std::wstring(1, static_cast<wchar_t>(virtual_key));
    if (virtual_key >= '0' && virtual_key <= '9')
        return std::wstring(1, static_cast<wchar_t>(virtual_key));
    if (virtual_key >= VK_F1 && virtual_key <= VK_F24) {
        return L"F" + std::to_wstring(virtual_key - VK_F1 + 1);
    }

    const UINT scan = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC);
    wchar_t name[96]{};
    const LONG key_data = static_cast<LONG>(scan << 16);
    if (scan && GetKeyNameTextW(key_data, name, static_cast<int>(std::size(name))) > 0)
        return name;
    std::wstringstream stream;
    stream << L"VK 0x" << std::hex << virtual_key;
    return stream.str();
}

void update_binding_button(std::size_t index) {
    if (index >= g.binding_buttons.size() || !g.binding_buttons[index]) return;
    const std::wstring label = g.capture_action == static_cast<int>(index)
        ? L"Press a key..." : key_name(g.key_bindings[index]);
    SetWindowTextW(g.binding_buttons[index], label.c_str());
}

void update_all_binding_buttons() {
    for (std::size_t i = 0; i < g.binding_buttons.size(); ++i)
        update_binding_button(i);
}

void clear_keyboard_state() {
    g.key_down.fill(false);
    g.joypad_pressed = 0;
}

unsigned normalize_virtual_key(WPARAM key, LPARAM key_data) {
    const unsigned value = static_cast<unsigned>(key);
    const unsigned scan = static_cast<unsigned>((static_cast<unsigned long long>(key_data) >> 16) & 0xFFu);
    const bool extended = (static_cast<unsigned long long>(key_data) & (1ull << 24)) != 0;
    if (value == VK_SHIFT) return scan == 0x36u ? VK_RSHIFT : VK_LSHIFT;
    if (value == VK_CONTROL) return extended ? VK_RCONTROL : VK_LCONTROL;
    if (value == VK_MENU) return extended ? VK_RMENU : VK_LMENU;
    return value;
}

void update_keyboard(unsigned virtual_key, bool down) {
    for (std::size_t i = 0; i < g.key_bindings.size(); ++i) {
        if (g.key_bindings[i] == virtual_key) g.key_down[i] = down;
    }
}

void poll_controller() {
    // Sample the physical key levels alongside WM_KEYDOWN/WM_KEYUP. The
    // emulator advances a frame on this UI thread; if a frame takes long
    // enough for both key messages to queue, event-only state can miss a
    // short press. The high bit of GetAsyncKeyState is the current down state.
    // Poll only while the game window is foreground so background keys never
    // leak into gameplay.
    if (g.game_window && GetForegroundWindow() == g.game_window) {
        for (std::size_t i = 0; i < g.key_bindings.size(); ++i) {
            const SHORT state = GetAsyncKeyState(static_cast<int>(g.key_bindings[i]));
            g.key_down[i] = (state & 0x8000) != 0;
        }
    } else {
        g.key_down.fill(false);
    }
    g.joypad_pressed = oracles::input::joypad_mask_from_actions(g.key_down);
    XINPUT_STATE state{};
    g.controller_connected = false;
    const int first_slot = g.selected_controller >= 0 ? g.selected_controller : 0;
    const int last_slot = g.selected_controller >= 0 ? g.selected_controller + 1 : XUSER_MAX_COUNT;
    for (int slot = first_slot; slot < last_slot; ++slot) {
        if (XInputGetState(static_cast<DWORD>(slot), &state) == ERROR_SUCCESS) {
            g.controller_connected = true;
            break;
        }
    }
    if (!g.controller_connected) return;

    g.controller = state;
    const auto buttons = state.Gamepad.wButtons;
    if (buttons & XINPUT_GAMEPAD_A) g.joypad_pressed |= 0x01;
    if (buttons & XINPUT_GAMEPAD_B) g.joypad_pressed |= 0x02;
    if (buttons & XINPUT_GAMEPAD_BACK) g.joypad_pressed |= 0x04;
    if (buttons & XINPUT_GAMEPAD_START) g.joypad_pressed |= 0x08;
    if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) g.joypad_pressed |= 0x10;
    if (buttons & XINPUT_GAMEPAD_DPAD_LEFT) g.joypad_pressed |= 0x20;
    if (buttons & XINPUT_GAMEPAD_DPAD_UP) g.joypad_pressed |= 0x40;
    if (buttons & XINPUT_GAMEPAD_DPAD_DOWN) g.joypad_pressed |= 0x80;
}

void release_gpus() {
    for (auto& gpu : g.gpus) {
        if (gpu.adapter) gpu.adapter->Release();
        gpu.adapter = nullptr;
    }
    g.gpus.clear();
}

void enumerate_gpus() {
    std::wstring previous = read_setting(L"Display", L"GPU", kAutomaticGpu);
    if (g.gpu_combo) {
        const LRESULT selected = SendMessageW(g.gpu_combo, CB_GETCURSEL, 0, 0);
        if (selected != CB_ERR) {
            wchar_t text[512]{};
            SendMessageW(g.gpu_combo, CB_GETLBTEXT, selected,
                         reinterpret_cast<LPARAM>(text));
            previous = text;
        }
    }
    release_gpus();

    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void**>(&factory)))) {
        for (UINT index = 0;; ++index) {
            IDXGIAdapter1* adapter = nullptr;
            const HRESULT result = factory->EnumAdapters1(index, &adapter);
            if (result == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(result) || !adapter) continue;
            DXGI_ADAPTER_DESC1 description{};
            if (SUCCEEDED(adapter->GetDesc1(&description)) &&
                !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                g.gpus.push_back({adapter, description.Description});
            } else {
                adapter->Release();
            }
        }
        factory->Release();
    }

    if (!g.gpu_combo) return;
    SendMessageW(g.gpu_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(g.gpu_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(kAutomaticGpu));
    int selected_index = 0;
    for (std::size_t i = 0; i < g.gpus.size(); ++i) {
        SendMessageW(g.gpu_combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(g.gpus[i].description.c_str()));
        if (g.gpus[i].description == previous) selected_index = static_cast<int>(i) + 1;
    }
    SendMessageW(g.gpu_combo, CB_SETCURSEL, selected_index, 0);
    g.selected_gpu_combo_index = selected_index;
}

HWND add_control(HWND parent, const wchar_t* class_name, const wchar_t* caption,
                 DWORD style, int x, int y, int width, int height, int id = 0) {
    HWND control = CreateWindowExW(0, class_name, caption,
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent,
        id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (control) {
        if (g.ui_font) SendMessageW(control, WM_SETFONT,
                                    reinterpret_cast<WPARAM>(g.ui_font), TRUE);
        g.launcher_controls.push_back(control);
    }
    return control;
}

void create_launcher_controls(HWND hwnd) {
    g.ui_font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    add_control(hwnd, L"STATIC", L"Game:", 0, 48, 116, 120, 28);
    g.game_combo = add_control(hwnd, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 174, 110, 610, 220, ID_GAME);
    SendMessageW(g.game_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"The Legend of Zelda: Oracle of Ages"));
    SendMessageW(g.game_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"The Legend of Zelda: Oracle of Seasons"));
    const auto saved_game = read_setting(L"Game", L"Selection", L"Ages");
    g.selected_game = saved_game == L"Seasons" ? 1 : 0;
    SendMessageW(g.game_combo, CB_SETCURSEL, g.selected_game, 0);

    add_control(hwnd, L"STATIC", L"Graphics adapter:", 0, 48, 166, 120, 28);
    g.gpu_combo = add_control(hwnd, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 174, 160, 610, 260, ID_GPU);
    g.refresh_button = add_control(hwnd, L"BUTTON", L"Refresh", BS_PUSHBUTTON | WS_TABSTOP,
        798, 160, 104, 30, ID_REFRESH_GPUS);

    add_control(hwnd, L"STATIC", L"Display ratio:", 0, 48, 216, 120, 28);
    g.aspect_combo = add_control(hwnd, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 174, 210, 280, 180, ID_ASPECT);
    SendMessageW(g.aspect_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"4:3"));
    SendMessageW(g.aspect_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"16:9 widescreen"));
    g.widescreen = read_setting(L"Display", L"Aspect", L"4:3") == L"Widescreen";
    SendMessageW(g.aspect_combo, CB_SETCURSEL, g.widescreen ? 1 : 0, 0);

    add_control(hwnd, L"STATIC", L"Window mode:", 0, 488, 216, 110, 28);
    g.window_mode_combo = add_control(hwnd, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 600, 210, 302, 180, ID_WINDOW_MODE);
    SendMessageW(g.window_mode_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Windowed"));
    SendMessageW(g.window_mode_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Fullscreen (borderless)"));
    g.fullscreen = read_setting(L"Display", L"WindowMode", L"Windowed") == L"Fullscreen";
    SendMessageW(g.window_mode_combo, CB_SETCURSEL, g.fullscreen ? 1 : 0, 0);

    add_control(hwnd, L"STATIC", L"Frame limit:", 0, 48, 254, 120, 28);
    g.fps_combo = add_control(hwnd, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 174, 248, 280, 210, ID_FPS);
    constexpr std::array<int, 18> fps_choices{40, 50, 60, 72, 75, 90, 100, 120, 144, 165, 180, 200, 240, 300, 360, 480, 600, 0};
    const auto saved_fps = read_setting(L"Display", L"FPS", L"60");
    int fps_index = 2;
    for (std::size_t i = 0; i < fps_choices.size(); ++i) {
        const std::wstring label = fps_choices[i] == 0
            ? L"Uncapped" : std::to_wstring(fps_choices[i]) + L" FPS";
        SendMessageW(g.fps_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if ((fps_choices[i] == 0 && saved_fps == L"Uncapped") ||
            (fps_choices[i] != 0 && saved_fps == std::to_wstring(fps_choices[i])))
            fps_index = static_cast<int>(i);
    }
    SendMessageW(g.fps_combo, CB_SETCURSEL, fps_index, 0);

    add_control(hwnd, L"STATIC", L"XInput controller:", 0, 488, 254, 110, 28);
    g.controller_combo = add_control(hwnd, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP, 600, 248, 302, 210, ID_CONTROLLER);
    SendMessageW(g.controller_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto-detect (first connected)"));
    for (int slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        const std::wstring label = L"XInput Controller " + std::to_wstring(slot + 1);
        SendMessageW(g.controller_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    const auto saved_controller = read_setting(L"Input", L"XInputController", L"-1");
    try { g.selected_controller = std::clamp(std::stoi(saved_controller), -1, XUSER_MAX_COUNT - 1); }
    catch (...) { g.selected_controller = -1; }
    SendMessageW(g.controller_combo, CB_SETCURSEL, g.selected_controller + 1, 0);

    add_control(hwnd, L"STATIC", L"KEYBOARD BINDINGS  -  Click a button, then press the replacement key",
        0, 48, 286, 850, 28);

    const std::array<int, 8> key_setting_defaults{
        static_cast<int>(oracles::input::kDefaultVirtualKeys[0]),
        static_cast<int>(oracles::input::kDefaultVirtualKeys[1]),
        static_cast<int>(oracles::input::kDefaultVirtualKeys[2]),
        static_cast<int>(oracles::input::kDefaultVirtualKeys[3]),
        static_cast<int>(oracles::input::kDefaultVirtualKeys[4]),
        static_cast<int>(oracles::input::kDefaultVirtualKeys[5]),
        static_cast<int>(oracles::input::kDefaultVirtualKeys[6]),
        static_cast<int>(oracles::input::kDefaultVirtualKeys[7])
    };
    const std::array<const wchar_t*, 8> key_setting_names{
        L"Up", L"Down", L"Left", L"Right", L"A", L"B", L"Start", L"Select"
    };
    for (std::size_t i = 0; i < g.key_bindings.size(); ++i)
        g.key_bindings[i] = read_key_setting(key_setting_names[i],
                                             static_cast<unsigned>(key_setting_defaults[i]));

    constexpr int row_y[4]{340, 390, 440, 490};
    constexpr int left_label_x = 82;
    constexpr int left_button_x = 220;
    constexpr int right_label_x = 490;
    constexpr int right_button_x = 620;
    const std::array<std::size_t, 4> left_actions{0, 1, 2, 3};
    const std::array<std::size_t, 4> right_actions{4, 5, 6, 7};
    for (std::size_t row = 0; row < 4; ++row) {
        add_control(hwnd, L"STATIC", kActionNames[left_actions[row]], 0,
                    left_label_x, row_y[row] + 4, 120, 28);
        g.binding_buttons[left_actions[row]] = add_control(hwnd, L"BUTTON", L"",
            BS_PUSHBUTTON | WS_TABSTOP, left_button_x, row_y[row], 180, 34,
            ID_BIND_BASE + static_cast<int>(left_actions[row]));
        add_control(hwnd, L"STATIC", kActionNames[right_actions[row]], 0,
                    right_label_x, row_y[row] + 4, 110, 28);
        g.binding_buttons[right_actions[row]] = add_control(hwnd, L"BUTTON", L"",
            BS_PUSHBUTTON | WS_TABSTOP, right_button_x, row_y[row], 180, 34,
            ID_BIND_BASE + static_cast<int>(right_actions[row]));
    }
    update_all_binding_buttons();

    add_control(hwnd, L"STATIC",
        L"XInput controllers are supported automatically. Settings are saved when you launch.",
        0, 48, 552, 850, 26);
    add_control(hwnd, L"STATIC",
        L"Keyboard input goes directly to the game window after launch; no extra viewport click is required.",
        0, 48, 580, 850, 26);
    g.launch_button = add_control(hwnd, L"BUTTON", L"LAUNCH!", BS_DEFPUSHBUTTON | WS_TABSTOP,
        660, 620, 242, 64, ID_LAUNCH);

    enumerate_gpus();
}

void save_launcher_settings() {
    const LRESULT game = SendMessageW(g.game_combo, CB_GETCURSEL, 0, 0);
    const LRESULT gpu = SendMessageW(g.gpu_combo, CB_GETCURSEL, 0, 0);
    const LRESULT aspect = SendMessageW(g.aspect_combo, CB_GETCURSEL, 0, 0);
    const LRESULT window_mode = SendMessageW(g.window_mode_combo, CB_GETCURSEL, 0, 0);
    const LRESULT fps = SendMessageW(g.fps_combo, CB_GETCURSEL, 0, 0);
    const LRESULT controller = SendMessageW(g.controller_combo, CB_GETCURSEL, 0, 0);

    constexpr std::array<int, 8> fps_choices{40, 60, 120, 144, 165, 240, 360, 0};
    const int fps_index = fps == CB_ERR ? 2 : std::clamp(static_cast<int>(fps), 0, 17);
    g.fps_limit = fps_choices[static_cast<std::size_t>(fps_index)];
    g.selected_controller = controller == CB_ERR ? -1 :
        std::clamp(static_cast<int>(controller) - 1, -1, XUSER_MAX_COUNT - 1);

    g.selected_game = game == 1 ? 1 : 0;
    g.selected_gpu_combo_index = gpu == CB_ERR ? 0 : static_cast<int>(gpu);
    g.widescreen = aspect == 1;
    g.fullscreen = window_mode == 1;

    write_setting(L"Game", L"Selection", g.selected_game == 1 ? L"Seasons" : L"Ages");
    write_setting(L"Display", L"Aspect", g.widescreen ? L"Widescreen" : L"4:3");
    write_setting(L"Display", L"WindowMode", g.fullscreen ? L"Fullscreen" : L"Windowed");
    write_setting(L"Display", L"FPS", g.fps_limit == 0 ? L"Uncapped" : std::to_wstring(g.fps_limit));
    write_setting(L"Input", L"XInputController", std::to_wstring(g.selected_controller));
    if (g.selected_gpu_combo_index <= 0 ||
        static_cast<std::size_t>(g.selected_gpu_combo_index - 1) >= g.gpus.size()) {
        write_setting(L"Display", L"GPU", kAutomaticGpu);
        g.selected_gpu_combo_index = 0;
    } else {
        write_setting(L"Display", L"GPU",
            g.gpus[static_cast<std::size_t>(g.selected_gpu_combo_index - 1)].description);
    }

    const std::array<const wchar_t*, 8> names{
        L"Up", L"Down", L"Left", L"Right", L"A", L"B", L"Start", L"Select"
    };
    for (std::size_t i = 0; i < names.size(); ++i)
        write_setting(L"Keyboard", names[i], std::to_wstring(g.key_bindings[i]));
}

bool load_selected_game() {
    const bool ages = g.selected_game == 0;
    const auto rom_path = executable_directory() / L"GameData" /
        (ages ? L"OracleOfAges.gbc" : L"OracleOfSeasons.gbc");
    auto runner = std::make_unique<oracles::GameRunner>();
    std::string error;
    const auto native_step = ages ? oracles::generated::ages_step
                                  : oracles::generated::seasons_step;
    if (!runner->load(rom_path, native_step, error)) {
        g.last_launch_error = L"Unable to load " + rom_path.filename().wstring() + L".\n\n" +
            std::wstring(error.begin(), error.end());
        return false;
    }
    g.runner = std::move(runner);
    g.active_game = ages ? L"The Legend of Zelda: Oracle of Ages"
                         : L"The Legend of Zelda: Oracle of Seasons";
    return true;
}

bool initialize_game_renderer(HWND hwnd) {
    IDXGIAdapter1* selected_adapter = nullptr;
    if (g.selected_gpu_combo_index > 0) {
        const auto adapter_index = static_cast<std::size_t>(g.selected_gpu_combo_index - 1);
        if (adapter_index >= g.gpus.size()) {
            g.last_launch_error = L"The selected graphics adapter is no longer available. Refresh the GPU list and try again.";
            return false;
        }
        selected_adapter = g.gpus[adapter_index].adapter;
    }

    auto renderer = std::make_unique<oracles::D3D11Renderer>();
    std::wstring error;
    if (!renderer->initialize(hwnd, selected_adapter, selected_aspect(), error)) {
        g.last_launch_error = L"Direct3D 11 initialization failed:\n\n" + error;
        return false;
    }
    g.renderer_status = renderer->adapter_description();
    g.renderer = std::move(renderer);
    return true;
}

void run_game_to_frame() {
    if (!g.runner || !g.runner->running()) return;
    std::string error;
    const auto result = g.runner->run_slice(g.joypad_pressed, error);
    if (result == oracles::FrameRunStatus::Faulted) {
        const std::wstring message(error.begin(), error.end());
        SetWindowTextW(g.game_window, (L"Oracles PC Recomp - stopped: " + message).c_str());
        g.runner->stop();
        return;
    }
    const auto audio_samples = g.runner->runtime().take_audio_samples();
    g.audio_output.submit(audio_samples);
    if (result == oracles::FrameRunStatus::FrameReady && g.renderer) {
        std::wstring upload_error;
        const auto& pixels = g.runner->runtime().ppu.framebuffer();
        if (!g.renderer->submit_frame(pixels.data(), oracles::Ppu::kScreenWidth,
                                      oracles::Ppu::kScreenHeight, upload_error)) {
            SetWindowTextW(g.game_window,
                (L"Oracles PC Recomp - framebuffer upload failed: " + upload_error).c_str());
            g.runner->stop();
        } else {
            g.frame_pending = true;
        }
    }
}

void on_binding_capture(HWND hwnd, unsigned virtual_key) {
    if (g.capture_action < 0 || g.capture_action >= static_cast<int>(g.key_bindings.size()))
        return;
    if (virtual_key == VK_ESCAPE) {
        const int previous = g.capture_action;
        g.capture_action = -1;
        update_binding_button(static_cast<std::size_t>(previous));
        return;
    }
    // Do not bind system-reserved modifiers or Alt+F4. A single key is easier
    // to use consistently across keyboard layouts and does not steal OS keys.
    if (virtual_key == VK_MENU || virtual_key == VK_LMENU || virtual_key == VK_RMENU ||
        virtual_key == VK_F4 || virtual_key == VK_LWIN || virtual_key == VK_RWIN ||
        virtual_key == VK_APPS) {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    const auto action = static_cast<std::size_t>(g.capture_action);
    const unsigned old_key = g.key_bindings[action];
    for (std::size_t i = 0; i < g.key_bindings.size(); ++i) {
        if (i != action && g.key_bindings[i] == virtual_key)
            g.key_bindings[i] = old_key; // swap to keep every action unique
    }
    g.key_bindings[action] = virtual_key;
    g.capture_action = -1;
    update_all_binding_buttons();
    SetFocus(hwnd);
}

HWND create_game_window(HINSTANCE instance) {
    const DWORD style = g.fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    RECT bounds{0, 0, 960, 720};
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = bounds.right;
    int height = bounds.bottom;

    if (g.fullscreen) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        const HMONITOR target = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (GetMonitorInfoW(target, &monitor)) {
            x = monitor.rcMonitor.left;
            y = monitor.rcMonitor.top;
            width = monitor.rcMonitor.right - monitor.rcMonitor.left;
            height = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
        } else {
            x = 0;
            y = 0;
            width = GetSystemMetrics(SM_CXSCREEN);
            height = GetSystemMetrics(SM_CYSCREEN);
        }
    } else {
        if (g.widescreen) {
            bounds.right = 1280;
            bounds.bottom = 720;
        }
        AdjustWindowRectEx(&bounds, style, FALSE, WS_EX_APPWINDOW);
        width = bounds.right - bounds.left;
        height = bounds.bottom - bounds.top;
    }

    const wchar_t* title = g.selected_game == 0
        ? L"The Legend of Zelda: Oracle of Ages - Oracles PC Recomp"
        : L"The Legend of Zelda: Oracle of Seasons - Oracles PC Recomp";
    return CreateWindowExW(WS_EX_APPWINDOW, kGameClassName, title, style,
        x, y, width, height, nullptr, nullptr, instance, nullptr);
}

LRESULT CALLBACK launcher_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        g.launcher_window = hwnd;
        create_launcher_controls(hwnd);
        return 0;

    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        if (id == ID_REFRESH_GPUS && notification == BN_CLICKED) {
            enumerate_gpus();
            return 0;
        }
        if (id >= ID_BIND_BASE && id < ID_BIND_BASE + 8 && notification == BN_CLICKED) {
            g.capture_action = id - ID_BIND_BASE;
            update_binding_button(static_cast<std::size_t>(g.capture_action));
            // The top-level launcher receives the next WM_KEYDOWN; otherwise
            // the clicked child button would consume it as a button mnemonic.
            SetFocus(hwnd);
            return 0;
        }
        if (id == ID_LAUNCH && notification == BN_CLICKED) {
            if (g.capture_action >= 0) {
                g.capture_action = -1;
                update_all_binding_buttons();
            }
            save_launcher_settings();
            g.last_launch_error.clear();
            g.launching = true;
            HWND game = create_game_window(GetModuleHandleW(nullptr));
            if (!game) {
                g.launching = false;
                g.game_window = nullptr;
                g.runner.reset();
                g.renderer.reset();
                const std::wstring message = g.last_launch_error.empty()
                    ? L"The game window could not be created." : g.last_launch_error;
                MessageBoxW(hwnd, message.c_str(), L"Oracles PC Recomp", MB_ICONERROR | MB_OK);
                return 0;
            }
            g.game_window = game;
            ShowWindow(game, SW_SHOW);
            UpdateWindow(game);
            SetForegroundWindow(game);
            SetFocus(game);
            DestroyWindow(hwnd); // close launcher only after game initialization succeeded
            g.launching = false;
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (g.capture_action >= 0) {
            on_binding_capture(hwnd, normalize_virtual_key(wparam, lparam));
            return 0;
        }
        break;

    case WM_SYSKEYDOWN:
        if (g.capture_action >= 0 && wparam != VK_MENU &&
            (lparam & (1LL << 29)) == 0) {
            on_binding_capture(hwnd, normalize_virtual_key(wparam, lparam));
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, RGB(220, 230, 242));
        SetBkColor(dc, RGB(17, 23, 34));
        SetDCBrushColor(dc, RGB(17, 23, 34));
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        HBRUSH background = CreateSolidBrush(RGB(17, 23, 34));
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(128, 192, 255));
        HFONT title_font = CreateFontW(-31, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ old = SelectObject(dc, title_font);
        TextOutW(dc, 48, 28, L"ORACLES PC RECOMP", 17);
        SelectObject(dc, old);
        DeleteObject(title_font);
        SetTextColor(dc, RGB(184, 199, 218));
        constexpr wchar_t subtitle[] = L"Choose your game, graphics adapter, display and controls.";
        TextOutW(dc, 50, 70, subtitle, static_cast<int>(std::size(subtitle) - 1));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 75, 101));
        HGDIOBJ old_pen = SelectObject(dc, pen);
        MoveToEx(dc, 48, 96, nullptr);
        LineTo(dc, client.right - 48, 96);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g.ui_font) {
            DeleteObject(g.ui_font);
            g.ui_font = nullptr;
        }
        g.launcher_window = nullptr;
        if (!g.launching && !g.game_window) {
            release_gpus();
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK game_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        g.game_window = hwnd;
        if (!load_selected_game()) return -1;
        if (!initialize_game_renderer(hwnd)) {
            g.runner.reset();
            return -1;
        }
        g.audio_output.initialize();
        g.next_simulation_time = monotonic_seconds() + (1.0 / 59.7275);
        g.next_present_time = monotonic_seconds();
        g.timer_resolution_active = (timeBeginPeriod(1) == TIMERR_NOERROR);
        SetTimer(hwnd, 1, 1, nullptr);
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;

    case WM_KEYDOWN:
        update_keyboard(normalize_virtual_key(wparam, lparam), true);
        return 0;

    case WM_KEYUP:
        update_keyboard(normalize_virtual_key(wparam, lparam), false);
        return 0;

    case WM_SYSKEYDOWN:
        // Preserve Windows shortcuts (Alt+Tab, Alt+F4, etc.). Non-Alt system
        // keys are still accepted as configurable game buttons.
        if ((lparam & (1LL << 29)) == 0)
            update_keyboard(normalize_virtual_key(wparam, lparam), true);
        return DefWindowProcW(hwnd, message, wparam, lparam);

    case WM_SYSKEYUP:
        if ((lparam & (1LL << 29)) == 0)
            update_keyboard(normalize_virtual_key(wparam, lparam), false);
        return DefWindowProcW(hwnd, message, wparam, lparam);

    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE) {
            clear_keyboard_state();
        } else {
            // The game has no child controls, so the top-level render window
            // should regain keyboard focus whenever the user alt-tabs back.
            SetFocus(hwnd);
        }
        return 0;

    case WM_KILLFOCUS:
        clear_keyboard_state();
        return 0;

    case WM_TIMER: {
        poll_controller();
        const double now = monotonic_seconds();

        // Advance the emulated cartridge at its native ~59.7275 Hz frame
        // cadence, independently of the selected presentation limit. A
        // 120/240 FPS setting never accelerates the Game Boy CPU or PPU.
        if (now >= g.next_simulation_time) {
            g.next_simulation_time += 1.0 / 59.7275;
            if (now - g.next_simulation_time > 0.25)
                g.next_simulation_time = now + (1.0 / 59.7275);
            run_game_to_frame();
        }

        const double present_interval = g.fps_limit > 0
            ? 1.0 / static_cast<double>(g.fps_limit) : 0.0;
        if (g.frame_pending && now >= g.next_present_time && g.renderer) {
            std::wstring error;
            if (!g.renderer->render(error)) {
                SetWindowTextW(hwnd, (L"Oracles PC Recomp - renderer error: " + error).c_str());
                g.renderer.reset();
            } else {
                g.frame_pending = false;
                g.next_present_time = present_interval > 0.0 ? now + present_interval : now;
            }
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g.timer_resolution_active) {
            timeEndPeriod(1);
            g.timer_resolution_active = false;
        }
        g.audio_output.shutdown();
        clear_keyboard_state();
        g.runner.reset();
        g.renderer.reset();
        g.game_window = nullptr;
        if (!g.launching) {
            release_gpus();
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    WNDCLASSEXW launcher_class{};
    launcher_class.cbSize = sizeof(launcher_class);
    launcher_class.hInstance = instance;
    launcher_class.lpfnWndProc = launcher_proc;
    launcher_class.lpszClassName = kLauncherClassName;
    launcher_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    launcher_class.hbrBackground = nullptr;
    if (!RegisterClassExW(&launcher_class)) return 1;

    WNDCLASSEXW game_class{};
    game_class.cbSize = sizeof(game_class);
    game_class.hInstance = instance;
    game_class.lpfnWndProc = game_proc;
    game_class.lpszClassName = kGameClassName;
    game_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    game_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassExW(&game_class)) return 1;

    constexpr DWORD launcher_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                     WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT window_rect{0, 0, 950, 720};
    AdjustWindowRectEx(&window_rect, launcher_style, FALSE, WS_EX_APPWINDOW);
    HWND launcher = CreateWindowExW(WS_EX_APPWINDOW, kLauncherClassName,
        L"Oracles PC Recomp - Launcher", launcher_style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        nullptr, nullptr, instance, nullptr);
    if (!launcher) return 1;

    ShowWindow(launcher, show);
    UpdateWindow(launcher);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
