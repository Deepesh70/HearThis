#include "hearthis/audio_processor.h"
#include "hearthis/wasapi_capture.h"
#include "hearthis/api_client.h"
#include "hearthis/settings.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <atomic>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Control IDs
#define IDC_BTN_SETTINGS        100
#define IDC_COMBO_PROVIDER      101
#define IDC_BTN_IDENTIFY        102
#define IDC_BTN_BROWSER         103
#define IDC_BTN_GET_CREDS       104
#define IDC_BTN_YOUTUBE         105
#define IDC_BTN_COPY            106
#define IDC_BTN_THEME           107

// Segmented Toggle IDs
#define IDC_TOGGLE_SRC_SPEAKERS 110
#define IDC_TOGGLE_SRC_MIC      111
#define IDC_TOGGLE_DUR_5S       112
#define IDC_TOGGLE_DUR_10S      113

#define IDC_EDIT_ACR_HOST       201
#define IDC_EDIT_ACR_KEY        202
#define IDC_EDIT_ACR_SECRET     203
#define IDC_EDIT_AUDD_TOKEN     204

// Custom Windows Messages & Timers
#define WM_RECOGNITION_DONE     (WM_USER + 1)
#define TIMER_SCAN_PROGRESS     1

// Global UI State & Handles
HWND g_hwnd_main = nullptr;
HWND g_hwnd_title = nullptr;
HWND g_hwnd_btn_theme = nullptr;
HWND g_hwnd_btn_settings = nullptr;
HWND g_hwnd_provider = nullptr;
HWND g_hwnd_btn_get_creds = nullptr;

// Segmented Buttons
HWND g_hwnd_btn_src_speakers = nullptr;
HWND g_hwnd_btn_src_mic = nullptr;
HWND g_hwnd_btn_dur_5s = nullptr;
HWND g_hwnd_btn_dur_10s = nullptr;

HWND g_hwnd_btn_identify = nullptr;
HWND g_hwnd_status = nullptr;

// Card Action Buttons
HWND g_hwnd_btn_browser = nullptr;
HWND g_hwnd_btn_youtube = nullptr;
HWND g_hwnd_btn_copy = nullptr;

// Result Card Displays
HWND g_hwnd_song_title = nullptr;
HWND g_hwnd_song_artist = nullptr;
HWND g_hwnd_card_badge = nullptr;
HWND g_hwnd_subtitle = nullptr;

// Settings Header Labels & Edit Controls
HWND g_hwnd_lbl_provider = nullptr;
HWND g_hwnd_lbl_acr_host = nullptr;
HWND g_hwnd_edit_acr_host = nullptr;
HWND g_hwnd_lbl_acr_key = nullptr;
HWND g_hwnd_edit_acr_key = nullptr;
HWND g_hwnd_lbl_acr_secret = nullptr;
HWND g_hwnd_edit_acr_secret = nullptr;
HWND g_hwnd_lbl_audd_token = nullptr;
HWND g_hwnd_edit_audd_token = nullptr;

// Symmetrical Toggle Labels
HWND g_hwnd_lbl_src = nullptr;
HWND g_hwnd_lbl_dur = nullptr;

// Fonts
HFONT g_font_title = nullptr;
HFONT g_font_song_title = nullptr;
HFONT g_font_bold = nullptr;
HFONT g_font_regular = nullptr;
HFONT g_font_small = nullptr;

// GDI Brushes & Pens
HBRUSH g_bg_brush = nullptr;
HBRUSH g_input_brush = nullptr;
HBRUSH g_card_brush = nullptr;
HBRUSH g_accent_brush = nullptr;
HPEN g_border_pen = nullptr;
HPEN g_border_subtle_pen = nullptr;

enum class AppStatusState {
    Ready,
    Listening,
    Success,
    Error,
    Warning
};
AppStatusState g_status_state = AppStatusState::Ready;

COLORREF get_status_color(AppStatusState state, bool is_dark) {
    switch (state) {
    case AppStatusState::Listening:
        return is_dark ? RGB(56, 189, 248) : RGB(2, 132, 199);
    case AppStatusState::Success:
        return is_dark ? RGB(52, 211, 153) : RGB(5, 150, 105);
    case AppStatusState::Error:
        return is_dark ? RGB(248, 113, 113) : RGB(220, 38, 38);
    case AppStatusState::Warning:
        return is_dark ? RGB(251, 146, 60) : RGB(217, 119, 6);
    case AppStatusState::Ready:
    default:
        return is_dark ? RGB(156, 163, 175) : RGB(71, 85, 105);
    }
}

COLORREF g_status_color = RGB(156, 163, 175);
std::string g_current_youtube_url = "";
std::string g_current_track_string = "";
std::atomic<bool> g_cancel_scan{false};

// Worker thread synchronization & safe result delivery
std::mutex g_result_mutex;
hearthis::RecognitionResult g_last_result;
std::thread g_worker_thread;

// Animation & Collapsible Settings State
bool g_settings_expanded = false;
bool g_is_scanning = false;
float g_scan_progress = 0.0f;
int g_scan_target_seconds = 10;
std::chrono::steady_clock::time_point g_scan_start_time;

void update_theme_resources(bool is_dark) {
    for (HGDIOBJ obj : std::initializer_list<HGDIOBJ>{(HGDIOBJ)g_bg_brush, (HGDIOBJ)g_input_brush, (HGDIOBJ)g_card_brush, (HGDIOBJ)g_accent_brush, (HGDIOBJ)g_border_pen, (HGDIOBJ)g_border_subtle_pen}) {
        if (obj) DeleteObject(obj);
    }

    if (is_dark) {
        g_bg_brush = CreateSolidBrush(RGB(20, 21, 25));
        g_input_brush = CreateSolidBrush(RGB(28, 30, 36));
        g_card_brush = CreateSolidBrush(RGB(26, 28, 34));
        g_accent_brush = CreateSolidBrush(RGB(16, 185, 129));
        g_border_pen = CreatePen(PS_SOLID, 1, RGB(42, 45, 53));
        g_border_subtle_pen = CreatePen(PS_SOLID, 1, RGB(55, 60, 72));
    } else {
        g_bg_brush = CreateSolidBrush(RGB(245, 246, 248));
        g_input_brush = CreateSolidBrush(RGB(255, 255, 255));
        g_card_brush = CreateSolidBrush(RGB(255, 255, 255));
        g_accent_brush = CreateSolidBrush(RGB(16, 185, 129));
        g_border_pen = CreatePen(PS_SOLID, 1, RGB(226, 232, 240));
        g_border_subtle_pen = CreatePen(PS_SOLID, 1, RGB(203, 213, 225));
    }
}

using hearthis::AppSettings;
using hearthis::load_settings;
using hearthis::save_settings;

AppSettings g_settings;

void copy_to_clipboard(HWND hwnd, const std::string& text) {
    if (text.empty()) return;
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hGlob) {
            char* pData = static_cast<char*>(GlobalLock(hGlob));
            if (pData) {
                memcpy(pData, text.c_str(), text.size() + 1);
                GlobalUnlock(hGlob);
                SetClipboardData(CF_TEXT, hGlob);
            }
        }
        CloseClipboard();
    }
}

void update_provider_visibility(int provider_index) {
    bool is_acr = (provider_index == 0);
    int show_acr = (g_settings_expanded && is_acr) ? SW_SHOW : SW_HIDE;
    int show_audd = (g_settings_expanded && !is_acr) ? SW_SHOW : SW_HIDE;

    for (HWND h : {g_hwnd_lbl_acr_host, g_hwnd_edit_acr_host, g_hwnd_lbl_acr_key, g_hwnd_edit_acr_key, g_hwnd_lbl_acr_secret, g_hwnd_edit_acr_secret}) {
        ShowWindow(h, show_acr);
    }
    for (HWND h : {g_hwnd_lbl_audd_token, g_hwnd_edit_audd_token}) {
        ShowWindow(h, show_audd);
    }

    SetWindowTextA(g_hwnd_btn_get_creds, is_acr ? "Get ACRCloud Keys" : "Get AudD Token");
}

void apply_layout(HWND hwnd) {
    int w = 512;
    int h = g_settings_expanded ? 645 : 425;

    // Smoothly adjust window height
    SetWindowPos(hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);

    SetWindowPos(g_hwnd_btn_theme, nullptr, 314, 18, 62, 26, SWP_NOZORDER);
    SetWindowPos(g_hwnd_btn_settings, nullptr, 382, 18, 86, 26, SWP_NOZORDER);

    int show_settings = g_settings_expanded ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hwnd_lbl_provider, show_settings);
    ShowWindow(g_hwnd_provider, show_settings);
    ShowWindow(g_hwnd_btn_get_creds, show_settings);
    update_provider_visibility(g_settings.provider_index);

    int base_y = g_settings_expanded ? 280 : 74;

    // Symmetrical Segmented Toggles
    SetWindowPos(g_hwnd_lbl_src, nullptr, 28, base_y, 120, 16, SWP_NOZORDER);
    SetWindowPos(g_hwnd_btn_src_speakers, nullptr, 28, base_y + 20, 104, 28, SWP_NOZORDER);
    SetWindowPos(g_hwnd_btn_src_mic, nullptr, 134, base_y + 20, 104, 28, SWP_NOZORDER);

    SetWindowPos(g_hwnd_lbl_dur, nullptr, 258, base_y, 120, 16, SWP_NOZORDER);
    SetWindowPos(g_hwnd_btn_dur_5s, nullptr, 258, base_y + 20, 104, 28, SWP_NOZORDER);
    SetWindowPos(g_hwnd_btn_dur_10s, nullptr, 364, base_y + 20, 104, 28, SWP_NOZORDER);

    // Identify Button
    int btn_y = base_y + 60;
    SetWindowPos(g_hwnd_btn_identify, nullptr, 28, btn_y, 440, 38, SWP_NOZORDER);

    // Status Label
    int status_y = btn_y + 46;
    SetWindowPos(g_hwnd_status, nullptr, 28, status_y, 440, 18, SWP_NOZORDER);

    // Result Card Elements
    int card_y = status_y + 24;
    SetWindowPos(g_hwnd_card_badge, nullptr, 350, card_y + 12, 104, 18, SWP_NOZORDER);
    SetWindowPos(g_hwnd_song_title, nullptr, 44, card_y + 12, 300, 24, SWP_NOZORDER);
    SetWindowPos(g_hwnd_song_artist, nullptr, 44, card_y + 38, 400, 18, SWP_NOZORDER);

    int action_y = card_y + 68;
    SetWindowPos(g_hwnd_btn_browser, nullptr, 44, action_y, 115, 26, SWP_NOZORDER);
    SetWindowPos(g_hwnd_btn_youtube, nullptr, 167, action_y, 105, 26, SWP_NOZORDER);
    SetWindowPos(g_hwnd_btn_copy, nullptr, 280, action_y, 95, 26, SWP_NOZORDER);

    InvalidateRect(hwnd, nullptr, TRUE);
}

// Background recognition worker thread
void run_recognition_worker(HWND hwnd, AppSettings settings) {
    auto post_result = [hwnd](const hearthis::RecognitionResult& res) {
        {
            std::lock_guard<std::mutex> lock(g_result_mutex);
            g_last_result = res;
        }
        PostMessage(hwnd, WM_RECOGNITION_DONE, 0, 0);
    };

    hearthis::WasapiCapture capture;
    double duration = static_cast<double>(settings.duration_seconds);
    hearthis::CapturedAudio audio = capture.capture_audio(settings.audio_source, duration, &g_cancel_scan);

    if (!audio.success) {
        hearthis::RecognitionResult res;
        res.error_message = audio.error_message;
        post_result(res);
        return;
    }

    std::vector<float> conditioned = hearthis::condition_audio(
        audio.samples,
        audio.channels,
        audio.sample_rate,
        settings.audio_source
    );

    if (hearthis::is_silent(conditioned, -50.0f)) {
        hearthis::RecognitionResult res;
        res.error_message = (settings.audio_source == hearthis::AudioSource::Microphone)
            ? "Silence detected. Microphone input was below -50 dBFS."
            : "Silence detected. System playback was below -50 dBFS.";
        post_result(res);
        return;
    }

    std::vector<uint8_t> wav = hearthis::pcm_to_wav_bytes(conditioned, audio.sample_rate);
    hearthis::ApiClient client;
    hearthis::RecognitionResult result;

    if (settings.provider_index == 0) {
        hearthis::AcrCredentials creds;
        creds.host = settings.acr_host;
        creds.access_key = settings.acr_key;
        creds.access_secret = settings.acr_secret;
        result = client.recognize_acrcloud(wav, creds);
    } else {
        result = client.recognize_audd(wav, settings.audd_token);
    }

    post_result(result);
}

// Reusable declarative rounded button renderer
void draw_rounded_button(DRAWITEMSTRUCT* pDraw, COLORREF bg_col, COLORREF border_col, COLORREF text_col, HFONT font, int radius) {
    HBRUSH btnBrush = CreateSolidBrush(bg_col);
    HPEN btnPen = CreatePen(PS_SOLID, 1, border_col);
    HGDIOBJ oldBrush = SelectObject(pDraw->hDC, btnBrush);
    HGDIOBJ oldPen = SelectObject(pDraw->hDC, btnPen);
    RoundRect(pDraw->hDC, pDraw->rcItem.left, pDraw->rcItem.top, pDraw->rcItem.right, pDraw->rcItem.bottom, radius, radius);

    SetBkMode(pDraw->hDC, TRANSPARENT);
    SetTextColor(pDraw->hDC, text_col);
    SelectObject(pDraw->hDC, font);

    char text[64] = {0};
    GetWindowTextA(pDraw->hwndItem, text, sizeof(text));
    DrawTextA(pDraw->hDC, text, -1, &pDraw->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(pDraw->hDC, oldBrush);
    SelectObject(pDraw->hDC, oldPen);
    DeleteObject(btnBrush);
    DeleteObject(btnPen);
}

// Main Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd_main = hwnd;
        g_settings = load_settings();
        update_theme_resources(g_settings.dark_theme);
        g_status_color = get_status_color(AppStatusState::Ready, g_settings.dark_theme);

        // Modern ClearType Typography (Segoe UI)
        auto make_font = [](int size, int weight) {
            return CreateFontA(size, 0, 0, 0, weight, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        };
        g_font_title = make_font(-19, FW_BOLD);
        g_font_song_title = make_font(-16, FW_BOLD);
        g_font_bold = make_font(-13, FW_SEMIBOLD);
        g_font_regular = make_font(-13, FW_NORMAL);
        g_font_small = make_font(-11, FW_SEMIBOLD);

        // Header Title & Logo Icon
        HICON hLogo = reinterpret_cast<HICON>(GetClassLongPtrA(hwnd, GCLP_HICON));
        if (!hLogo) {
            hLogo = LoadIconA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1));
        }

        if (hLogo) {
            HWND hIconCtrl = CreateWindowA("STATIC", "", WS_VISIBLE | WS_CHILD | SS_ICON,
                                           28, 16, 32, 32, hwnd, nullptr, nullptr, nullptr);
            SendMessage(hIconCtrl, STM_SETICON, reinterpret_cast<WPARAM>(hLogo), 0);

            g_hwnd_title = CreateWindowA("STATIC", "HearThis", WS_VISIBLE | WS_CHILD,
                                         70, 14, 180, 24, hwnd, nullptr, nullptr, nullptr);
            SendMessage(g_hwnd_title, WM_SETFONT, reinterpret_cast<WPARAM>(g_font_title), TRUE);

            g_hwnd_subtitle = CreateWindowA("STATIC", "Native desktop audio recognition", WS_VISIBLE | WS_CHILD,
                                            70, 40, 240, 16, hwnd, nullptr, nullptr, nullptr);
            SendMessage(g_hwnd_subtitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_font_regular), TRUE);
        }

        auto make_ctrl = [&](const char* cls, const char* txt, DWORD style, int x, int y, int w, int h, int id, HFONT font) {
            HWND hCtrl = CreateWindowA(cls, txt, style | WS_CHILD, x, y, w, h, hwnd,
                                      id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr, nullptr, nullptr);
            if (font) SendMessage(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return hCtrl;
        };

        // Header Theme & Settings Buttons
        g_hwnd_btn_theme = make_ctrl("BUTTON", g_settings.dark_theme ? "Light" : "Dark", WS_VISIBLE | BS_OWNERDRAW, 314, 18, 62, 26, IDC_BTN_THEME, g_font_regular);
        g_hwnd_btn_settings = make_ctrl("BUTTON", "Settings", WS_VISIBLE | BS_OWNERDRAW, 382, 18, 86, 26, IDC_BTN_SETTINGS, g_font_regular);

        // Collapsible Settings Section
        g_hwnd_lbl_provider = make_ctrl("STATIC", "Music Provider:", 0, 28, 66, 120, 16, 0, g_font_bold);
        g_hwnd_provider = make_ctrl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 28, 86, 270, 120, IDC_COMBO_PROVIDER, g_font_regular);
        SendMessage(g_hwnd_provider, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("ACRCloud (Default)"));
        SendMessage(g_hwnd_provider, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("AudD (Secondary)"));

        g_hwnd_btn_get_creds = make_ctrl("BUTTON", "Get ACRCloud Keys", BS_OWNERDRAW, 308, 85, 160, 26, IDC_BTN_GET_CREDS, g_font_regular);

        // ACRCloud Inputs
        g_hwnd_lbl_acr_host = make_ctrl("STATIC", "ACRCloud Host:", 0, 28, 118, 140, 16, 0, g_font_regular);
        g_hwnd_edit_acr_host = make_ctrl("EDIT", "", ES_AUTOHSCROLL, 28, 137, 440, 23, IDC_EDIT_ACR_HOST, g_font_regular);

        g_hwnd_lbl_acr_key = make_ctrl("STATIC", "ACRCloud Access Key:", 0, 28, 167, 160, 16, 0, g_font_regular);
        g_hwnd_edit_acr_key = make_ctrl("EDIT", "", ES_AUTOHSCROLL, 28, 186, 440, 23, IDC_EDIT_ACR_KEY, g_font_regular);

        g_hwnd_lbl_acr_secret = make_ctrl("STATIC", "ACRCloud Access Secret:", 0, 28, 216, 180, 16, 0, g_font_regular);
        g_hwnd_edit_acr_secret = make_ctrl("EDIT", "", ES_AUTOHSCROLL | ES_PASSWORD, 28, 235, 440, 23, IDC_EDIT_ACR_SECRET, g_font_regular);

        // AudD Input
        g_hwnd_lbl_audd_token = make_ctrl("STATIC", "AudD API Token:", 0, 28, 118, 140, 16, 0, g_font_regular);
        g_hwnd_edit_audd_token = make_ctrl("EDIT", "", ES_AUTOHSCROLL | ES_PASSWORD, 28, 137, 440, 23, IDC_EDIT_AUDD_TOKEN, g_font_regular);

        // Symmetrical Segmented Pill Toggles
        g_hwnd_lbl_src = make_ctrl("STATIC", "Audio Source:", WS_VISIBLE, 28, 74, 120, 16, 0, g_font_bold);
        g_hwnd_btn_src_speakers = make_ctrl("BUTTON", "Speakers", WS_VISIBLE | BS_OWNERDRAW, 28, 94, 104, 28, IDC_TOGGLE_SRC_SPEAKERS, g_font_bold);
        g_hwnd_btn_src_mic = make_ctrl("BUTTON", "Mic", WS_VISIBLE | BS_OWNERDRAW, 134, 94, 104, 28, IDC_TOGGLE_SRC_MIC, g_font_bold);

        g_hwnd_lbl_dur = make_ctrl("STATIC", "Scan Duration:", WS_VISIBLE, 258, 74, 120, 16, 0, g_font_bold);
        g_hwnd_btn_dur_5s = make_ctrl("BUTTON", "5 Seconds", WS_VISIBLE | BS_OWNERDRAW, 258, 94, 104, 28, IDC_TOGGLE_DUR_5S, g_font_bold);
        g_hwnd_btn_dur_10s = make_ctrl("BUTTON", "10 Seconds", WS_VISIBLE | BS_OWNERDRAW, 364, 94, 104, 28, IDC_TOGGLE_DUR_10S, g_font_bold);

        // Identify Button
        g_hwnd_btn_identify = make_ctrl("BUTTON", "Identify Music Now", WS_VISIBLE | BS_OWNERDRAW, 28, 134, 440, 38, IDC_BTN_IDENTIFY, g_font_bold);

        // Status Label
        g_hwnd_status = make_ctrl("STATIC", "Ready. Click Identify Music Now to scan.", WS_VISIBLE | SS_CENTER, 28, 180, 440, 18, 0, g_font_regular);

        // Result Card Elements
        g_hwnd_card_badge = make_ctrl("STATIC", "", WS_VISIBLE | SS_RIGHT, 350, 214, 104, 18, 0, g_font_small);
        g_hwnd_song_title = make_ctrl("STATIC", "No song identified yet", WS_VISIBLE | SS_LEFT, 44, 214, 300, 24, 0, g_font_song_title);
        g_hwnd_song_artist = make_ctrl("STATIC", "Your detected track and links will appear here", WS_VISIBLE, 44, 240, 400, 18, 0, g_font_regular);

        // Action Row Buttons
        g_hwnd_btn_browser = make_ctrl("BUTTON", "Search Web", WS_VISIBLE | WS_DISABLED | BS_OWNERDRAW, 44, 270, 115, 26, IDC_BTN_BROWSER, g_font_regular);
        g_hwnd_btn_youtube = make_ctrl("BUTTON", "YouTube", WS_VISIBLE | WS_DISABLED | BS_OWNERDRAW, 167, 270, 105, 26, IDC_BTN_YOUTUBE, g_font_regular);
        g_hwnd_btn_copy = make_ctrl("BUTTON", "Copy Title", WS_VISIBLE | WS_DISABLED | BS_OWNERDRAW, 280, 270, 95, 26, IDC_BTN_COPY, g_font_regular);

        // Apply configuration controls
        SendMessage(g_hwnd_provider, CB_SETCURSEL, g_settings.provider_index, 0);
        SetWindowTextA(g_hwnd_edit_acr_host, g_settings.acr_host.c_str());
        SetWindowTextA(g_hwnd_edit_acr_key, g_settings.acr_key.c_str());
        SetWindowTextA(g_hwnd_edit_acr_secret, g_settings.acr_secret.c_str());
        SetWindowTextA(g_hwnd_edit_audd_token, g_settings.audd_token.c_str());

        // Expand settings only if credentials are completely empty
        if (g_settings.provider_index == 0 && (g_settings.acr_key.empty() || g_settings.acr_secret.empty())) {
            g_settings_expanded = true;
        } else if (g_settings.provider_index == 1 && g_settings.audd_token.empty()) {
            g_settings_expanded = true;
        }

        apply_layout(hwnd);
        break;
    }

    case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_bg_brush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        int base_y = g_settings_expanded ? 280 : 74;
        int status_y = base_y + 106;
        int card_y = status_y + 24;

        // Draw Soft Result Card Container
        RECT rcCard = { 28, card_y, 468, card_y + 110 };
        HGDIOBJ oldBrush = SelectObject(hdc, g_card_brush);
        HGDIOBJ oldPen = SelectObject(hdc, g_border_pen);
        RoundRect(hdc, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, 10, 10);

        // If settings are expanded, draw subtle divider and input borders
        if (g_settings_expanded) {
            SelectObject(hdc, g_border_subtle_pen);
            MoveToEx(hdc, 28, 268, nullptr);
            LineTo(hdc, 468, 268);

            // Subtle borders around edit inputs
            SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            SelectObject(hdc, g_border_pen);
            if (g_settings.provider_index == 0) {
                Rectangle(hdc, 27, 136, 469, 161);
                Rectangle(hdc, 27, 185, 469, 210);
                Rectangle(hdc, 27, 234, 469, 259);
            } else {
                Rectangle(hdc, 27, 136, 469, 161);
            }
        }

        // Live Scanning Progress Bar
        if (g_is_scanning) {
            int pbar_y = base_y + 98;
            RECT rcTrack = { 28, pbar_y, 468, pbar_y + 4 };
            HBRUSH trackBrush = CreateSolidBrush(g_settings.dark_theme ? RGB(32, 34, 42) : RGB(226, 232, 240));
            FillRect(hdc, &rcTrack, trackBrush);
            DeleteObject(trackBrush);

            int fill_w = static_cast<int>(440.0f * std::clamp(g_scan_progress, 0.0f, 1.0f));
            RECT rcFill = { 28, pbar_y, 28 + fill_w, pbar_y + 4 };
            FillRect(hdc, &rcFill, g_accent_brush);
        }

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER: {
        if (wParam == TIMER_SCAN_PROGRESS && g_is_scanning) {
            auto elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_scan_start_time).count();
            g_scan_progress = elapsed / static_cast<float>(g_scan_target_seconds);
            float remaining = std::max(0.0f, static_cast<float>(g_scan_target_seconds) - elapsed);

            char buf[64] = {0};
            sprintf_s(buf, sizeof(buf), "Listening... (%.1fs remaining)", remaining);
            SetWindowTextA(g_hwnd_status, buf);

            int base_y = g_settings_expanded ? 280 : 74;
            int pbar_y = base_y + 98;
            RECT rcBar = { 28, pbar_y, 468, pbar_y + 4 };
            InvalidateRect(hwnd, &rcBar, FALSE);

            if (g_scan_progress >= 1.0f) {
                SetWindowTextA(g_hwnd_status, "Analyzing audio fingerprint...");
            }
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* pDraw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!pDraw) break;

        bool disabled = (pDraw->itemState & ODS_DISABLED);
        bool selected = (pDraw->itemState & ODS_SELECTED);
        bool is_dark = g_settings.dark_theme;

        // Erase background behind button to eliminate corner clipping artifacts
        FillRect(pDraw->hDC, &pDraw->rcItem, g_bg_brush);

        if (pDraw->CtlID == IDC_BTN_IDENTIFY) {
            COLORREF bg = disabled ? (is_dark ? RGB(32, 34, 42) : RGB(226, 232, 240)) :
                          selected ? RGB(13, 148, 100) : RGB(16, 185, 129);
            COLORREF border = disabled ? (is_dark ? RGB(42, 45, 53) : RGB(203, 213, 225)) :
                              selected ? RGB(13, 148, 100) : RGB(16, 185, 129);
            COLORREF text = disabled ? (is_dark ? RGB(110, 115, 125) : RGB(148, 163, 184)) : RGB(255, 255, 255);
            draw_rounded_button(pDraw, bg, border, text, g_font_bold, 8);
            return TRUE;
        }

        if (pDraw->CtlID >= IDC_TOGGLE_SRC_SPEAKERS && pDraw->CtlID <= IDC_TOGGLE_DUR_10S) {
            bool is_active = (pDraw->CtlID == IDC_TOGGLE_SRC_SPEAKERS && g_settings.audio_source == hearthis::AudioSource::Speakers) ||
                             (pDraw->CtlID == IDC_TOGGLE_SRC_MIC && g_settings.audio_source == hearthis::AudioSource::Microphone) ||
                             (pDraw->CtlID == IDC_TOGGLE_DUR_5S && g_settings.duration_seconds == 5) ||
                             (pDraw->CtlID == IDC_TOGGLE_DUR_10S && g_settings.duration_seconds == 10);

            COLORREF bg = is_active ? (is_dark ? RGB(22, 54, 44) : RGB(16, 185, 129)) :
                                      (is_dark ? RGB(28, 30, 36) : RGB(241, 245, 249));
            COLORREF border = is_active ? RGB(16, 185, 129) : (is_dark ? RGB(42, 45, 53) : RGB(203, 213, 225));
            COLORREF text = is_active ? (is_dark ? RGB(52, 211, 153) : RGB(255, 255, 255)) :
                                        (is_dark ? RGB(160, 165, 175) : RGB(71, 85, 105));
            draw_rounded_button(pDraw, bg, border, text, g_font_bold, 6);
            return TRUE;
        }

        if (pDraw->CtlID == IDC_BTN_SETTINGS || pDraw->CtlID == IDC_BTN_THEME || pDraw->CtlID == IDC_BTN_GET_CREDS ||
            pDraw->CtlID == IDC_BTN_BROWSER || pDraw->CtlID == IDC_BTN_YOUTUBE || pDraw->CtlID == IDC_BTN_COPY) {
            if (pDraw->CtlID == IDC_BTN_BROWSER || pDraw->CtlID == IDC_BTN_YOUTUBE || pDraw->CtlID == IDC_BTN_COPY) {
                FillRect(pDraw->hDC, &pDraw->rcItem, g_card_brush);
            }

            COLORREF bg = disabled ? (is_dark ? RGB(28, 30, 36) : RGB(241, 245, 249)) :
                          selected ? (is_dark ? RGB(48, 52, 64) : RGB(218, 225, 233)) :
                                     (is_dark ? RGB(34, 37, 46) : RGB(241, 245, 249));
            COLORREF border = disabled ? (is_dark ? RGB(36, 38, 46) : RGB(226, 232, 240)) :
                                         (is_dark ? RGB(48, 52, 64) : RGB(203, 213, 225));
            COLORREF text = disabled ? (is_dark ? RGB(90, 95, 105) : RGB(148, 163, 184)) :
                                       (is_dark ? RGB(225, 228, 236) : RGB(17, 24, 39));
            draw_rounded_button(pDraw, bg, border, text, g_font_regular, 6);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        bool is_dark = g_settings.dark_theme;
        SetTextColor(hdc, is_dark ? RGB(240, 243, 248) : RGB(30, 35, 45));
        SetBkColor(hdc, is_dark ? RGB(28, 30, 36) : RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(g_input_brush);
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        bool is_dark = g_settings.dark_theme;
        SetTextColor(hdc, is_dark ? RGB(240, 243, 248) : RGB(30, 35, 45));
        SetBkColor(hdc, is_dark ? RGB(28, 30, 36) : RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(g_input_brush);
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND hCtrl = reinterpret_cast<HWND>(lParam);
        bool is_dark = g_settings.dark_theme;

        COLORREF card_bg = is_dark ? RGB(26, 28, 34) : RGB(255, 255, 255);
        COLORREF main_bg = is_dark ? RGB(20, 21, 25) : RGB(245, 246, 248);

        COLORREF text_col = is_dark ? RGB(215, 220, 230) : RGB(17, 24, 39);
        COLORREF bg_col = main_bg;
        HBRUSH brush = g_bg_brush;

        if (hCtrl == g_hwnd_song_title || hCtrl == g_hwnd_song_artist || hCtrl == g_hwnd_card_badge) {
            bg_col = card_bg;
            brush = g_card_brush;
            text_col = (hCtrl == g_hwnd_song_title) ? (is_dark ? RGB(255, 255, 255) : RGB(17, 24, 39)) :
                       (hCtrl == g_hwnd_song_artist) ? (is_dark ? RGB(156, 163, 175) : RGB(71, 85, 105)) :
                       (is_dark ? RGB(52, 211, 153) : RGB(5, 150, 105));
        } else if (hCtrl == g_hwnd_status) {
            text_col = g_status_color;
        } else if (hCtrl == g_hwnd_title) {
            text_col = is_dark ? RGB(255, 255, 255) : RGB(17, 24, 39);
        } else if (hCtrl == g_hwnd_subtitle) {
            text_col = is_dark ? RGB(140, 145, 155) : RGB(71, 85, 105);
        }

        SetTextColor(hdc, text_col);
        SetBkColor(hdc, bg_col);
        return reinterpret_cast<LRESULT>(brush);
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        if (wmId == IDC_BTN_THEME) {
            g_settings.dark_theme = !g_settings.dark_theme;
            save_settings(g_settings);
            update_theme_resources(g_settings.dark_theme);

            BOOL dark_mode = g_settings.dark_theme ? TRUE : FALSE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));

            SetWindowTextA(g_hwnd_btn_theme, g_settings.dark_theme ? "Light" : "Dark");
            g_status_color = get_status_color(g_status_state, g_settings.dark_theme);

            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        } else if (wmId == IDC_BTN_SETTINGS) {
            g_settings_expanded = !g_settings_expanded;
            SetWindowTextA(g_hwnd_btn_settings, g_settings_expanded ? "Hide" : "Settings");
            apply_layout(hwnd);
        } else if (wmId == IDC_COMBO_PROVIDER && wmEvent == CBN_SELCHANGE) {
            int idx = static_cast<int>(SendMessage(g_hwnd_provider, CB_GETCURSEL, 0, 0));
            g_settings.provider_index = idx;
            update_provider_visibility(idx);
        } else if (wmId == IDC_TOGGLE_SRC_SPEAKERS) {
            g_settings.audio_source = hearthis::AudioSource::Speakers;
            InvalidateRect(g_hwnd_btn_src_speakers, nullptr, TRUE);
            InvalidateRect(g_hwnd_btn_src_mic, nullptr, TRUE);
        } else if (wmId == IDC_TOGGLE_SRC_MIC) {
            g_settings.audio_source = hearthis::AudioSource::Microphone;
            InvalidateRect(g_hwnd_btn_src_speakers, nullptr, TRUE);
            InvalidateRect(g_hwnd_btn_src_mic, nullptr, TRUE);
        } else if (wmId == IDC_TOGGLE_DUR_5S) {
            g_settings.duration_seconds = 5;
            InvalidateRect(g_hwnd_btn_dur_5s, nullptr, TRUE);
            InvalidateRect(g_hwnd_btn_dur_10s, nullptr, TRUE);
        } else if (wmId == IDC_TOGGLE_DUR_10S) {
            g_settings.duration_seconds = 10;
            InvalidateRect(g_hwnd_btn_dur_5s, nullptr, TRUE);
            InvalidateRect(g_hwnd_btn_dur_10s, nullptr, TRUE);
        } else if (wmId == IDC_BTN_GET_CREDS) {
            int idx = static_cast<int>(SendMessage(g_hwnd_provider, CB_GETCURSEL, 0, 0));
            if (idx == 0) {
                ShellExecuteA(hwnd, "open", "https://console.acrcloud.com", nullptr, nullptr, SW_SHOWNORMAL);
            } else {
                ShellExecuteA(hwnd, "open", "https://audd.io", nullptr, nullptr, SW_SHOWNORMAL);
            }
        } else if (wmId == IDC_BTN_BROWSER) {
            if (!g_current_track_string.empty()) {
                std::string url = "https://www.google.com/search?q=";
                for (char c : g_current_track_string) {
                    if (c == ' ') url += '+';
                    else if (isalnum(c)) url += c;
                }
                ShellExecuteA(hwnd, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        } else if (wmId == IDC_BTN_YOUTUBE) {
            if (!g_current_youtube_url.empty()) {
                ShellExecuteA(hwnd, "open", g_current_youtube_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (!g_current_track_string.empty()) {
                std::string url = "https://www.youtube.com/results?search_query=";
                for (char c : g_current_track_string) {
                    if (c == ' ') url += '+';
                    else if (isalnum(c)) url += c;
                }
                ShellExecuteA(hwnd, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        } else if (wmId == IDC_BTN_COPY) {
            if (!g_current_track_string.empty()) {
                copy_to_clipboard(hwnd, g_current_track_string);
                SetWindowTextA(g_hwnd_btn_copy, "Copied!");
                InvalidateRect(g_hwnd_btn_copy, nullptr, TRUE);
            }
        } else if (wmId == IDC_BTN_IDENTIFY) {
            g_settings.provider_index = static_cast<int>(SendMessage(g_hwnd_provider, CB_GETCURSEL, 0, 0));

            char buf[512] = {0};
            GetWindowTextA(g_hwnd_edit_acr_host, buf, sizeof(buf)); g_settings.acr_host = buf;
            GetWindowTextA(g_hwnd_edit_acr_key, buf, sizeof(buf)); g_settings.acr_key = buf;
            GetWindowTextA(g_hwnd_edit_acr_secret, buf, sizeof(buf)); g_settings.acr_secret = buf;
            GetWindowTextA(g_hwnd_edit_audd_token, buf, sizeof(buf)); g_settings.audd_token = buf;

            save_settings(g_settings);

            // Validation
            if (g_settings.provider_index == 0) {
                if (g_settings.acr_host.empty() || g_settings.acr_key.empty() || g_settings.acr_secret.empty()) {
                    g_status_state = AppStatusState::Error;
                    g_status_color = get_status_color(g_status_state, g_settings.dark_theme);
                    SetWindowTextA(g_hwnd_status, "Please fill in ACRCloud Host, Key, and Secret.");
                    InvalidateRect(g_hwnd_status, nullptr, TRUE);
                    if (!g_settings_expanded) {
                        g_settings_expanded = true;
                        SetWindowTextA(g_hwnd_btn_settings, "Hide");
                        apply_layout(hwnd);
                    }
                    return 0;
                }
            } else {
                if (g_settings.audd_token.empty()) {
                    g_status_state = AppStatusState::Error;
                    g_status_color = get_status_color(g_status_state, g_settings.dark_theme);
                    SetWindowTextA(g_hwnd_status, "Please enter your AudD API Token.");
                    InvalidateRect(g_hwnd_status, nullptr, TRUE);
                    if (!g_settings_expanded) {
                        g_settings_expanded = true;
                        SetWindowTextA(g_hwnd_btn_settings, "Hide");
                        apply_layout(hwnd);
                    }
                    return 0;
                }
            }

            EnableWindow(g_hwnd_btn_identify, FALSE);
            EnableWindow(g_hwnd_btn_browser, FALSE);
            EnableWindow(g_hwnd_btn_youtube, FALSE);
            EnableWindow(g_hwnd_btn_copy, FALSE);
            SetWindowTextA(g_hwnd_btn_copy, "Copy Title");

            InvalidateRect(g_hwnd_btn_identify, nullptr, TRUE);
            InvalidateRect(g_hwnd_btn_browser, nullptr, TRUE);
            InvalidateRect(g_hwnd_btn_youtube, nullptr, TRUE);
            InvalidateRect(g_hwnd_btn_copy, nullptr, TRUE);

            g_cancel_scan = false;
            g_is_scanning = true;
            g_scan_progress = 0.0f;
            g_scan_target_seconds = g_settings.duration_seconds;
            g_scan_start_time = std::chrono::steady_clock::now();
            SetTimer(hwnd, TIMER_SCAN_PROGRESS, 50, nullptr);

            g_status_state = AppStatusState::Listening;
            g_status_color = get_status_color(g_status_state, g_settings.dark_theme);
            SetWindowTextA(g_hwnd_status, "Listening to audio...");
            InvalidateRect(g_hwnd_status, nullptr, TRUE);

            SetWindowTextA(g_hwnd_song_title, "Listening...");
            SetWindowTextA(g_hwnd_song_artist, "Sampling acoustic fingerprint");
            SetWindowTextA(g_hwnd_card_badge, "");

            if (g_worker_thread.joinable()) {
                g_worker_thread.join();
            }
            g_worker_thread = std::thread(run_recognition_worker, hwnd, g_settings);
        }
        break;
    }

    case WM_RECOGNITION_DONE: {
        KillTimer(hwnd, TIMER_SCAN_PROGRESS);
        g_is_scanning = false;
        g_scan_progress = 0.0f;

        if (g_worker_thread.joinable()) {
            g_worker_thread.join();
        }

        hearthis::RecognitionResult result;
        {
            std::lock_guard<std::mutex> lock(g_result_mutex);
            result = g_last_result;
        }

        // Force repaint of progress bar area
        int base_y = g_settings_expanded ? 280 : 74;
        int pbar_y = base_y + 98;
        RECT rcBar = { 28, pbar_y, 468, pbar_y + 4 };
        InvalidateRect(hwnd, &rcBar, TRUE);

        EnableWindow(g_hwnd_btn_identify, TRUE);
        InvalidateRect(g_hwnd_btn_identify, nullptr, TRUE);

        if (!result.error_message.empty()) {
            g_status_state = AppStatusState::Error;
            g_status_color = get_status_color(g_status_state, g_settings.dark_theme);
            std::string err = "Notice: " + result.error_message;
            SetWindowTextA(g_hwnd_status, err.c_str());
            SetWindowTextA(g_hwnd_song_title, "Scan Incomplete");
            SetWindowTextA(g_hwnd_song_artist, result.error_message.c_str());
            SetWindowTextA(g_hwnd_card_badge, "");
        } else if (result.matched) {
            g_status_state = AppStatusState::Success;
            g_status_color = get_status_color(g_status_state, g_settings.dark_theme);
            SetWindowTextA(g_hwnd_status, "Track successfully identified!");

            SetWindowTextA(g_hwnd_song_title, result.title.c_str());
            SetWindowTextA(g_hwnd_song_artist, result.artist.c_str());

            std::string badge_text = result.provider;
            SetWindowTextA(g_hwnd_card_badge, badge_text.c_str());

            g_current_youtube_url = result.youtube_url;
            g_current_track_string = result.title + " - " + result.artist;

            EnableWindow(g_hwnd_btn_browser, TRUE);
            EnableWindow(g_hwnd_btn_youtube, TRUE);
            EnableWindow(g_hwnd_btn_copy, TRUE);

            InvalidateRect(g_hwnd_btn_browser, nullptr, TRUE);
            InvalidateRect(g_hwnd_btn_youtube, nullptr, TRUE);
            InvalidateRect(g_hwnd_btn_copy, nullptr, TRUE);
        } else {
            g_status_state = AppStatusState::Warning;
            g_status_color = get_status_color(g_status_state, g_settings.dark_theme);
            SetWindowTextA(g_hwnd_status, "No match found. Try again during a clear music section.");
            SetWindowTextA(g_hwnd_song_title, "No Match Found");
            SetWindowTextA(g_hwnd_song_artist, "Could not match audio against catalog.");
            SetWindowTextA(g_hwnd_card_badge, "");
        }
        InvalidateRect(g_hwnd_status, nullptr, TRUE);
        break;
    }

    case WM_DESTROY: {
        g_cancel_scan = true;
        if (g_worker_thread.joinable()) {
            g_worker_thread.join();
        }
        KillTimer(hwnd, TIMER_SCAN_PROGRESS);
        for (HGDIOBJ obj : std::initializer_list<HGDIOBJ>{(HGDIOBJ)g_bg_brush, (HGDIOBJ)g_input_brush, (HGDIOBJ)g_card_brush, (HGDIOBJ)g_accent_brush, (HGDIOBJ)g_border_pen, (HGDIOBJ)g_border_subtle_pen,
                            (HGDIOBJ)g_font_title, (HGDIOBJ)g_font_song_title, (HGDIOBJ)g_font_bold, (HGDIOBJ)g_font_regular, (HGDIOBJ)g_font_small}) {
            if (obj) DeleteObject(obj);
        }
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    HICON hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(1));
    if (!hIcon) {
        hIcon = reinterpret_cast<HICON>(LoadImageA(nullptr, "assets\\icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
    }
    if (!hIcon) {
        char exePath[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        char* lastSlash = strrchr(exePath, '\\');
        if (lastSlash) {
            strcpy_s(lastSlash + 1, MAX_PATH - (lastSlash - exePath + 1), "icon.ico");
            hIcon = reinterpret_cast<HICON>(LoadImageA(nullptr, exePath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
        }
    }

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "HearThisNativeWindow";
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = hIcon;
    wc.hIconSm = hIcon;

    if (!RegisterClassExA(&wc)) {
        return 1;
    }

    int width = 512;
    int height = 425; // Compact height by default

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int pos_x = (screen_w - width) / 2;
    int pos_y = (screen_h - height) / 2;

    HWND hwnd = CreateWindowExA(
        0,
        "HearThisNativeWindow",
        "HearThis",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        pos_x, pos_y, width, height,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) {
        return 1;
    }

    if (hIcon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
    }

    AppSettings initial_settings = load_settings();
    BOOL dark_mode = initial_settings.dark_theme ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return static_cast<int>(msg.wParam);
}
