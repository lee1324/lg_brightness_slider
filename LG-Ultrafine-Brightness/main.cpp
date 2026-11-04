#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdint.h>
#include "hidapi.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "hidapi.lib")

const uint16_t max_brightness = 0xd2f0;
const uint16_t min_brightness = 0x0190;
const int slider_max = 100;

static hid_device *g_handle = nullptr;
static HWND g_hint = nullptr;
static HWND g_slider = nullptr;
static HWND g_min_btn = nullptr;
static HWND g_max_btn = nullptr;
static WNDPROC g_slider_proc = nullptr;
static bool g_slider_tracking = false;
static bool g_updating_slider = false;

enum {
    IDC_SLIDER = 1001,
    IDC_MIN = 1002,
    IDC_MAX = 1003,
};

uint16_t get_brightness(hid_device *handle) {
    uint8_t data[7] = {0};
    int res = hid_get_feature_report(handle, data, sizeof(data));
    if (res < 0) {
        return min_brightness;
    }
    return data[1] + (data[2] << 8);
}

void set_brightness(hid_device *handle, uint16_t val) {
    if (!handle) {
        return;
    }
    if (val < min_brightness) {
        val = min_brightness;
    }
    if (val > max_brightness) {
        val = max_brightness;
    }
    uint8_t data[7] = {
        0x00,
        uint8_t(val & 0x00ff),
        uint8_t((val >> 8) & 0x00ff),
        0x00, 0x00, 0x00, 0x00};
    hid_send_feature_report(handle, data, sizeof(data));
}

int brightness_percent(uint16_t val) {
    if (val <= min_brightness) {
        return 0;
    }
    if (val >= max_brightness) {
        return slider_max;
    }
    uint32_t span = uint32_t(max_brightness - min_brightness);
    return int((uint32_t(val - min_brightness) * slider_max + span / 2) / span);
}

uint16_t brightness_from_percent(int percent) {
    if (percent < 0) {
        percent = 0;
    }
    if (percent > slider_max) {
        percent = slider_max;
    }
    uint32_t span = uint32_t(max_brightness - min_brightness);
    return uint16_t(min_brightness + (span * uint32_t(percent)) / slider_max);
}

void apply_slider_position(int pos) {
    if (!g_handle) {
        return;
    }
    set_brightness(g_handle, brightness_from_percent(pos));
}

int percent_from_slider_x(HWND slider, int x) {
    RECT channel;
    RECT thumb;
    SendMessage(slider, TBM_GETCHANNELRECT, 0, (LPARAM)&channel);
    SendMessage(slider, TBM_GETTHUMBRECT, 0, (LPARAM)&thumb);
    int thumb_w = thumb.right - thumb.left;
    if (thumb_w < 1) {
        thumb_w = 1;
    }
    int span = (channel.right - channel.left) - thumb_w;
    if (span < 1) {
        span = 1;
    }
    int rel = x - channel.left - thumb_w / 2;
    if (rel < 0) {
        rel = 0;
    }
    if (rel > span) {
        rel = span;
    }
    return int((rel * slider_max + span / 2) / span);
}

void jump_slider_to_x(HWND slider, int x) {
    int pos = percent_from_slider_x(slider, x);
    g_updating_slider = true;
    SendMessage(slider, TBM_SETPOS, TRUE, pos);
    g_updating_slider = false;
    apply_slider_position(pos);
}

LRESULT CALLBACK SliderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
        RECT thumb;
        SendMessage(hwnd, TBM_GETTHUMBRECT, 0, (LPARAM)&thumb);
        if (PtInRect(&thumb, pt)) {
            break;
        }
        jump_slider_to_x(hwnd, pt.x);
        g_slider_tracking = true;
        SetFocus(hwnd);
        SetCapture(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_slider_tracking && (wParam & MK_LBUTTON)) {
            jump_slider_to_x(hwnd, (int)(short)LOWORD(lParam));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_slider_tracking) {
            jump_slider_to_x(hwnd, (int)(short)LOWORD(lParam));
            g_slider_tracking = false;
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        g_slider_tracking = false;
        break;
    }
    return CallWindowProc(g_slider_proc, hwnd, msg, wParam, lParam);
}

void set_slider_percent(int percent) {
    if (!g_slider) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    }
    if (percent > slider_max) {
        percent = slider_max;
    }
    g_updating_slider = true;
    SendMessage(g_slider, TBM_SETPOS, TRUE, percent);
    g_updating_slider = false;
    apply_slider_position(percent);
}

void layout_controls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    const int margin = 16;
    int inner = width - margin * 2;
    if (inner < 1) {
        inner = 1;
    }

    if (g_hint) {
        MoveWindow(g_hint, margin, 12, inner, 18, TRUE);
    }
    if (g_slider) {
        MoveWindow(g_slider, margin, 36, inner, 40, TRUE);
    }

    const int btn_w = 72;
    const int btn_h = 26;
    const int btn_y = 82;
    if (g_min_btn) {
        MoveWindow(g_min_btn, margin, btn_y, btn_w, btn_h, TRUE);
    }
    if (g_max_btn) {
        MoveWindow(g_max_btn, margin + inner - btn_w, btn_y, btn_w, btn_h, TRUE);
    }
}

char *find_ultrafine_path(hid_device_info *devs) {
    for (hid_device_info *cur = devs; cur; cur = cur->next) {
        if (cur->vendor_id != 0x043e) {
            continue;
        }
        if (cur->product_string && wcsstr(cur->product_string, L"BRIGHTNESS")) {
            return cur->path;
        }
    }
    return nullptr;
}

bool open_monitor() {
    if (hid_init()) {
        MessageBoxA(nullptr, "Failed to initialize HID.", "LG Ultrafine Brightness", MB_ICONERROR);
        return false;
    }

    hid_device_info *devs = hid_enumerate(0x0, 0x0);
    char *path = find_ultrafine_path(devs);
    if (!path) {
        hid_free_enumeration(devs);
        MessageBoxA(nullptr, "LG Ultrafine brightness device not found.", "LG Ultrafine Brightness", MB_ICONERROR);
        return false;
    }

    g_handle = hid_open_path(path);
    hid_free_enumeration(devs);
    if (!g_handle) {
        MessageBoxA(nullptr, "Unable to open LG Ultrafine device.", "LG Ultrafine Brightness", MB_ICONERROR);
        return false;
    }
    return true;
}

void sync_slider_from_device() {
    if (!g_handle || !g_slider) {
        return;
    }
    int pos = brightness_percent(get_brightness(g_handle));
    g_updating_slider = true;
    SendMessage(g_slider, TBM_SETPOS, TRUE, pos);
    g_updating_slider = false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hint = CreateWindowA(
            "STATIC",
            "Drag the slider to adjust brightness",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0,
            hwnd, nullptr, nullptr, nullptr);

        g_slider = CreateWindowExA(
            0,
            TRACKBAR_CLASSA,
            "Brightness",
            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ | TBS_TOOLTIPS,
            0, 0, 0, 0,
            hwnd, (HMENU)IDC_SLIDER, nullptr, nullptr);

        SendMessage(g_slider, TBM_SETRANGE, TRUE, MAKELONG(0, slider_max));
        SendMessage(g_slider, TBM_SETTICFREQ, 10, 0);
        g_slider_proc = (WNDPROC)SetWindowLongPtr(g_slider, GWLP_WNDPROC, (LONG_PTR)SliderProc);

        g_min_btn = CreateWindowA(
            "BUTTON",
            "Min",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd, (HMENU)IDC_MIN, nullptr, nullptr);
        g_max_btn = CreateWindowA(
            "BUTTON",
            "Max",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd, (HMENU)IDC_MAX, nullptr, nullptr);

        sync_slider_from_device();
        return 0;
    }
    case WM_SIZE:
        layout_controls(hwnd);
        return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            if (LOWORD(wParam) == IDC_MIN) {
                set_slider_percent(0);
            } else if (LOWORD(wParam) == IDC_MAX) {
                set_slider_percent(slider_max);
            }
        }
        return 0;
    case WM_HSCROLL: {
        if ((HWND)lParam == g_slider && !g_updating_slider) {
            int pos = static_cast<int>(SendMessage(g_slider, TBM_GETPOS, 0, 0));
            apply_slider_position(pos);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_DESTROY:
        if (g_handle) {
            hid_close(g_handle);
            g_handle = nullptr;
        }
        hid_exit();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    if (!open_monitor()) {
        hid_exit();
        return 1;
    }

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "LGUltrafineBrightnessWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = (HICON)LoadImageA(
        hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageA(
        hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExA(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME;
    RECT window_rect = {0, 0, 400, 120};
    AdjustWindowRect(&window_rect, style, FALSE);

    HWND hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "LG Ultrafine Brightness",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBoxA(nullptr, "Failed to create window.", "LG Ultrafine Brightness", MB_ICONERROR);
        hid_close(g_handle);
        hid_exit();
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}
