// Copyright 2019 Pokitec
// All rights reserved.

#include "gui_window_vulkan_win32.hpp"
#include "KeyboardVirtualKey.hpp"
#include "gui_system_vulkan_win32.hpp"
#include "ThemeBook.hpp"
#include "../widgets/WindowWidget.hpp"
#include "../strings.hpp"
#include "../thread.hpp"
#include "../Application.hpp"
#include <windowsx.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi")

namespace tt {


using namespace std;

template<typename T>
inline T *to_ptr(LPARAM lParam) noexcept
{
    T *ptr;
    memcpy(&ptr, &lParam, sizeof(T *));

    return ptr;
}

static const wchar_t *win32WindowClassName = nullptr;
static WNDCLASSW win32WindowClass = {};
static bool win32WindowClassIsRegistered = false;
static bool firstWindowHasBeenOpened = false;

static std::unordered_map<HWND, gui_window_vulkan_win32 *> win32_window_map = {};
static unfair_mutex win32_window_map_mutex;

static void add_win32_window(HWND handle, gui_window_vulkan_win32 &window) noexcept
{
    ttlet lock = std::scoped_lock(win32_window_map_mutex);
    win32_window_map[handle] = &window;
}

static gui_window_vulkan_win32 *find_win32_window(HWND handle) noexcept
{
    ttlet lock = std::scoped_lock(win32_window_map_mutex);
    auto i = win32_window_map.find(handle);
    return i != win32_window_map.end() ? i->second : nullptr;
}

static void erase_win32_window(HWND handle) noexcept
{
    ttlet lock = std::scoped_lock(win32_window_map_mutex);
    auto i = win32_window_map.find(handle);
    if (i != win32_window_map.end()) {
        win32_window_map.erase(i);
    }
}

/** The win32 window message handler.
 * This function should not take any locks as _WindowProc is called recursively.
 */
static LRESULT CALLBACK _WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
{
    if (uMsg == WM_NCCREATE && lParam) {
        ttlet createData = reinterpret_cast<CREATESTRUCT *>(lParam);
        auto *const window = static_cast<gui_window_vulkan_win32 *>(createData->lpCreateParams);

        if (window != nullptr) {
            add_win32_window(hwnd, *window);
        }
    }

    auto *const window = find_win32_window(hwnd);
    if (window != nullptr) {
        tt_assume(gui_system_mutex.recurse_lock_count() == 0);
        LRESULT result = window->windowProc(uMsg, wParam, lParam);

        if (uMsg == WM_DESTROY) {
            // Remove the window now, before DefWindowProc, which could recursively
            // Reuse the window as it is being cleaned up.
            erase_win32_window(hwnd);
        }

        // The call to DefWindowProc() recurses make sure we do not hold on to any locks.
        if (result == -1) {
            result = DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        return result;

    } else {
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

static void createWindowClass()
{
    if (!win32WindowClassIsRegistered) {
        // Register the window class.
        win32WindowClassName = L"TTauri Window Class";

        std::memset(&win32WindowClass, 0, sizeof(WNDCLASSW));
        win32WindowClass.style = CS_DBLCLKS;
        win32WindowClass.lpfnWndProc = _WindowProc;
        win32WindowClass.hInstance = reinterpret_cast<HINSTANCE>(application->hInstance);
        win32WindowClass.lpszClassName = win32WindowClassName;
        win32WindowClass.hCursor = nullptr;
        RegisterClassW(&win32WindowClass);
    }
    win32WindowClassIsRegistered = true;
}

void gui_window_vulkan_win32::createWindow(const std::u8string &_title, vec extent)
{
    // This function should be called during init(), and therefor should not have a lock on the window.
    tt_assert2(is_main_thread(), "createWindow should be called from the main thread.");
    tt_assume(gui_system_mutex.recurse_lock_count() == 0);

    createWindowClass();

    auto u16title = to_wstring(_title);

    // We are opening a popup window with a caption bar to cause drop-shadow to appear around
    // the window.
    win32Window = CreateWindowExW(
        0, // Optional window styles.
        win32WindowClassName, // Window class
        u16title.data(), // Window text
        WS_OVERLAPPEDWINDOW, // Window style
        // Size and position
        500,
        500,
        narrow_cast<int>(extent.x()),
        narrow_cast<int>(extent.y()),

        NULL, // Parent window
        NULL, // Menu
        reinterpret_cast<HINSTANCE>(application->hInstance), // Instance handle
        this);
    if (win32Window == nullptr) {
        LOG_FATAL("Could not open a win32 window: {}", getLastErrorMessage());
    }

    // Now we extend the drawable area over the titlebar and and border, excluding the drop shadow.
    // At least one value needs to be postive for the drop-shadow to be rendered.
    MARGINS m{0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(reinterpret_cast<HWND>(win32Window), &m);

    // Force WM_NCCALCSIZE to be send to the window.
    SetWindowPos(
        reinterpret_cast<HWND>(win32Window),
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

    if (!firstWindowHasBeenOpened) {
        ShowWindow(reinterpret_cast<HWND>(win32Window), application->nCmdShow);
        firstWindowHasBeenOpened = true;
    }

    trackMouseLeaveEventParameters.cbSize = sizeof(trackMouseLeaveEventParameters);
    trackMouseLeaveEventParameters.dwFlags = TME_LEAVE;
    trackMouseLeaveEventParameters.hwndTrack = reinterpret_cast<HWND>(win32Window);
    trackMouseLeaveEventParameters.dwHoverTime = HOVER_DEFAULT;

    ShowWindow(reinterpret_cast<HWND>(win32Window), SW_SHOW);

    auto _dpi = GetDpiForWindow(reinterpret_cast<HWND>(win32Window));
    if (_dpi == 0) {
        TTAURI_THROW(gui_error("Could not retrieve dpi for window."));
    }
    dpi = narrow_cast<float>(_dpi);
}

gui_window_vulkan_win32::gui_window_vulkan_win32(
    gui_system &system,
    std::weak_ptr<gui_window_delegate> const &delegate,
    label const &title) :
    gui_window_vulkan(system, delegate, title), trackMouseLeaveEventParameters()
{
    doubleClickMaximumDuration = GetDoubleClickTime() * 1ms;
    LOG_INFO("Double click duration {} ms", doubleClickMaximumDuration / 1ms);
}

gui_window_vulkan_win32::~gui_window_vulkan_win32()
{
    try {
        if (win32Window != nullptr) {
            LOG_FATAL("win32Window was not destroyed before Window '{}' was destructed.", title);
        }
    } catch (...) {
        abort();
    }
}

void gui_window_vulkan_win32::closeWindow()
{
    run_from_main_loop([=]() {
        DestroyWindow(reinterpret_cast<HWND>(win32Window));
    });
}

void gui_window_vulkan_win32::minimizeWindow()
{
    run_from_main_loop([=]() {
        ShowWindow(reinterpret_cast<HWND>(win32Window), SW_MINIMIZE);
    });
}

void gui_window_vulkan_win32::maximizeWindow()
{
    run_from_main_loop([=]() {
        ShowWindow(reinterpret_cast<HWND>(win32Window), SW_MAXIMIZE);
    });
}

void gui_window_vulkan_win32::normalizeWindow()
{
    run_from_main_loop([=]() {
        ShowWindow(reinterpret_cast<HWND>(win32Window), SW_RESTORE);
    });
}

void gui_window_vulkan_win32::setWindowSize(ivec extent)
{
    gui_system_mutex.lock();
    ttlet handle = reinterpret_cast<HWND>(win32Window);
    gui_system_mutex.unlock();

    run_from_main_loop([=]() {
        SetWindowPos(
            reinterpret_cast<HWND>(handle),
            HWND_NOTOPMOST,
            0,
            0,
            extent.x(),
            extent.y(),
            SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_DEFERERASE | SWP_NOCOPYBITS | SWP_FRAMECHANGED);
    });
}

[[nodiscard]] ivec gui_window_vulkan_win32::virtualScreenSize() const noexcept
{
    ttlet width = GetSystemMetrics(SM_CXMAXTRACK);
    ttlet height = GetSystemMetrics(SM_CYMAXTRACK);
    if (width <= 0 || height <= 0) {
        LOG_FATAL("Failed to get virtual screen size");
    }
    return {width, height};
}

[[nodiscard]] std::u8string gui_window_vulkan_win32::getTextFromClipboard() const noexcept
{
    auto r = std::u8string{};

    gui_system_mutex.lock();
    ttlet handle = reinterpret_cast<HWND>(win32Window);
    gui_system_mutex.unlock();

    if (!OpenClipboard(handle)) {
        LOG_ERROR("Could not open win32 clipboard '{}'", getLastErrorMessage());
        return r;
    }

    UINT format = 0;

    while ((format = EnumClipboardFormats(format)) != 0) {
        switch (format) {
        case CF_TEXT:
        case CF_OEMTEXT:
        case CF_UNICODETEXT: {
            ttlet cb_data = GetClipboardData(CF_UNICODETEXT);
            if (cb_data == nullptr) {
                LOG_ERROR("Could not get clipboard data: '{}'", getLastErrorMessage());
                goto done;
            }

            ttlet wstr_c = reinterpret_cast<wchar_t *>(GlobalLock(cb_data));
            if (wstr_c == nullptr) {
                LOG_ERROR("Could not lock clipboard data: '{}'", getLastErrorMessage());
                goto done;
            }

            ttlet wstr = std::wstring_view(wstr_c);
            r = tt::to_u8string(wstr);
            LOG_DEBUG("getTextFromClipboad '{}'", tt::to_string(r));

            if (!GlobalUnlock(cb_data) && GetLastError() != ERROR_SUCCESS) {
                LOG_ERROR("Could not unlock clipboard data: '{}'", getLastErrorMessage());
                goto done;
            }
        }
            goto done;

        default:;
        }
    }

    if (GetLastError() != ERROR_SUCCESS) {
        LOG_ERROR("Could not enumerator clipboard formats: '{}'", getLastErrorMessage());
    }

done:
    CloseClipboard();

    return r;
}

void gui_window_vulkan_win32::setTextOnClipboard(std::u8string str) noexcept
{
    if (!OpenClipboard(reinterpret_cast<HWND>(win32Window))) {
        LOG_ERROR("Could not open win32 clipboard '{}'", getLastErrorMessage());
        return;
    }

    if (!EmptyClipboard()) {
        LOG_ERROR("Could not empty win32 clipboard '{}'", getLastErrorMessage());
        goto done;
    }

    {
        auto wstr = tt::to_wstring(str);

        auto wstr_handle = GlobalAlloc(GMEM_MOVEABLE, (std::ssize(wstr) + 1) * sizeof(wchar_t));
        if (wstr_handle == nullptr) {
            LOG_ERROR("Could not allocate clipboard data '{}'", getLastErrorMessage());
            goto done;
        }

        auto wstr_c = reinterpret_cast<wchar_t *>(GlobalLock(wstr_handle));
        if (wstr_c == nullptr) {
            LOG_ERROR("Could not lock clipboard data '{}'", getLastErrorMessage());
            GlobalFree(wstr_handle);
            goto done;
        }

        std::memcpy(wstr_c, wstr.c_str(), (std::ssize(wstr) + 1) * sizeof(wchar_t));

        if (!GlobalUnlock(wstr_handle) && GetLastError() != ERROR_SUCCESS) {
            LOG_ERROR("Could not unlock clipboard data '{}'", getLastErrorMessage());
            GlobalFree(wstr_handle);
            goto done;
        }

        auto handle = SetClipboardData(CF_UNICODETEXT, wstr_handle);
        if (handle == nullptr) {
            LOG_ERROR("Could not set clipboard data '{}'", getLastErrorMessage());
            GlobalFree(wstr_handle);
            goto done;
        }
    }

done:
    CloseClipboard();
}

vk::SurfaceKHR gui_window_vulkan_win32::getSurface() const
{
    ttlet lock = std::scoped_lock(gui_system_mutex);
    return narrow_cast<gui_system_vulkan_win32&>(system).createWin32SurfaceKHR(
        {vk::Win32SurfaceCreateFlagsKHR(),
         reinterpret_cast<HINSTANCE>(application->hInstance),
         reinterpret_cast<HWND>(win32Window)});
}

void gui_window_vulkan_win32::setOSWindowRectangleFromRECT(RECT rect) noexcept
{
    ttlet lock = std::scoped_lock(gui_system_mutex);

    // XXX Without screen height, it is not possible to calculate the y of the left-bottom corner.
    OSWindowRectangle = iaarect{rect.left, 0 - rect.bottom, rect.right - rect.left, rect.bottom - rect.top};

    // Force a redraw, so that the swapchain is used and causes out-of-date results on window resize,
    // which in turn will cause a forceLayout.
    request_redraw();
}

void gui_window_vulkan_win32::setCursor(Cursor cursor) noexcept
{
    tt_assume(gui_system_mutex.recurse_lock_count() == 0);

    {
        ttlet lock = std::scoped_lock(gui_system_mutex);

        if (currentCursor == cursor) {
            return;
        }
        currentCursor = cursor;

        if (cursor == Cursor::None) {
            return;
        }
    }

    static auto idcAppStarting = LoadCursorW(nullptr, IDC_APPSTARTING);
    static auto idcArrow = LoadCursorW(nullptr, IDC_ARROW);
    static auto idcHand = LoadCursorW(nullptr, IDC_HAND);
    static auto idcIBeam = LoadCursorW(nullptr, IDC_IBEAM);
    static auto idcNo = LoadCursorW(nullptr, IDC_NO);

    auto idc = idcNo;
    switch (cursor) {
    case Cursor::None: idc = idcAppStarting; break;
    case Cursor::Default: idc = idcArrow; break;
    case Cursor::Button: idc = idcHand; break;
    case Cursor::TextEdit: idc = idcIBeam; break;
    default: tt_no_default();
    }

    SetCursor(idc);
}

[[nodiscard]] KeyboardModifiers gui_window_vulkan_win32::getKeyboardModifiers() noexcept
{
    auto r = KeyboardModifiers::None;

    if ((static_cast<uint16_t>(GetAsyncKeyState(VK_SHIFT)) & 0x8000) != 0) {
        r |= KeyboardModifiers::Shift;
    }
    if ((static_cast<uint16_t>(GetAsyncKeyState(VK_CONTROL)) & 0x8000) != 0) {
        r |= KeyboardModifiers::Control;
    }
    if ((static_cast<uint16_t>(GetAsyncKeyState(VK_MENU)) & 0x8000) != 0) {
        r |= KeyboardModifiers::Alt;
    }
    if ((static_cast<uint16_t>(GetAsyncKeyState(VK_LWIN)) & 0x8000) != 0 ||
        (static_cast<uint16_t>(GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0) {
        r |= KeyboardModifiers::Super;
    }

    return r;
}

[[nodiscard]] KeyboardState gui_window_vulkan_win32::getKeyboardState() noexcept
{
    auto r = KeyboardState::Idle;

    if (GetKeyState(VK_CAPITAL) != 0) {
        r |= KeyboardState::CapsLock;
    }
    if (GetKeyState(VK_NUMLOCK) != 0) {
        r |= KeyboardState::NumLock;
    }
    if (GetKeyState(VK_SCROLL) != 0) {
        r |= KeyboardState::ScrollLock;
    }
    return r;
}

/** The win32 window message handler.
 * This function should not take any long-term-locks as windowProc is called recursively.
 */
int gui_window_vulkan_win32::windowProc(unsigned int uMsg, uint64_t wParam, int64_t lParam) noexcept
{
    MouseEvent mouseEvent;

    switch (uMsg) {
    case WM_DESTROY: {
        ttlet lock = std::scoped_lock(gui_system_mutex);
        win32Window = nullptr;
        state = State::WindowLost;
    } break;

    case WM_CREATE: {
        ttlet createstruct_ptr = to_ptr<CREATESTRUCT>(lParam);
        RECT rect;
        rect.left = createstruct_ptr->x;
        rect.top = createstruct_ptr->y;
        rect.right = createstruct_ptr->x + createstruct_ptr->cx;
        rect.bottom = createstruct_ptr->y + createstruct_ptr->cy;
        setOSWindowRectangleFromRECT(rect);
    } break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        ttlet lock = std::scoped_lock(gui_system_mutex);
        
        PAINTSTRUCT ps;
        BeginPaint(win32Window, &ps);

        ttlet update_rectangle = aarect{
            ps.rcPaint.left,
            currentWindowExtent.height() - ps.rcPaint.bottom,
            ps.rcPaint.right - ps.rcPaint.left,
            ps.rcPaint.bottom - ps.rcPaint.top
        };

        request_redraw(update_rectangle);
        EndPaint(win32Window, &ps);
    } break;

    case WM_NCPAINT: {
        ttlet lock = std::scoped_lock(gui_system_mutex);
        request_redraw(aarect::infinity());
    } break;

    case WM_SIZE: {
        ttlet lock = std::scoped_lock(gui_system_mutex);
        switch (wParam) {
        case SIZE_MAXIMIZED: size = Size::Maximized; break;
        case SIZE_MINIMIZED: size = Size::Minimized; break;
        case SIZE_RESTORED: size = Size::Normal; break;
        default: break;
        }
    } break;

    case WM_SIZING: {
        ttlet rect_ptr = to_ptr<RECT>(lParam);
        setOSWindowRectangleFromRECT(*rect_ptr);
    } break;

    case WM_MOVING: {
        ttlet rect_ptr = to_ptr<RECT>(lParam);
        setOSWindowRectangleFromRECT(*rect_ptr);
    } break;

    case WM_WINDOWPOSCHANGED: {
        ttlet windowpos_ptr = to_ptr<WINDOWPOS>(lParam);
        RECT rect;
        rect.left = windowpos_ptr->x;
        rect.top = windowpos_ptr->y;
        rect.right = windowpos_ptr->x + windowpos_ptr->cx;
        rect.bottom = windowpos_ptr->y + windowpos_ptr->cy;
        setOSWindowRectangleFromRECT(rect);
    } break;

    case WM_ENTERSIZEMOVE: {
        ttlet lock = std::scoped_lock(gui_system_mutex);
        resizing = true;
    } break;

    case WM_EXITSIZEMOVE: {
        ttlet lock = std::scoped_lock(gui_system_mutex);
        resizing = false;
    } break;

    case WM_ACTIVATE: {
        ttlet lock = std::scoped_lock(gui_system_mutex);
        switch (wParam) {
        case 1: // WA_ACTIVE
        case 2: // WA_CLICKACTIVE
            active = true;
            break;
        case 0: // WA_INACTIVE
            active = false;
            break;
        default: LOG_ERROR("Unknown WM_ACTIVE value.");
        }
        requestLayout = true;
    } break;

    case WM_GETMINMAXINFO: {
        ttlet lock = std::scoped_lock(gui_system_mutex);
        tt_assume(widget);
        ttlet widget_size = widget->preferred_size();
        ttlet minimum_widget_size = widget_size.minimum();
        ttlet maximum_widget_size = widget_size.maximum();
        ttlet minmaxinfo = to_ptr<MINMAXINFO>(lParam);
        minmaxinfo->ptMaxSize.x = narrow_cast<LONG>(maximum_widget_size.width());
        minmaxinfo->ptMaxSize.y = narrow_cast<LONG>(maximum_widget_size.height());
        minmaxinfo->ptMinTrackSize.x = narrow_cast<LONG>(minimum_widget_size.width());
        minmaxinfo->ptMinTrackSize.y = narrow_cast<LONG>(minimum_widget_size.height());
        minmaxinfo->ptMaxTrackSize.x = narrow_cast<LONG>(maximum_widget_size.width());
        minmaxinfo->ptMaxTrackSize.y = narrow_cast<LONG>(maximum_widget_size.height());
    } break;

    case WM_UNICHAR: {
        auto c = narrow_cast<char32_t>(wParam);
        if (c == UNICODE_NOCHAR) {
            // Tell the 3rd party keyboard handler application that we support WM_UNICHAR.
            return 1;
        } else if (c >= 0x20) {
            auto keyboardEvent = KeyboardEvent();
            keyboardEvent.type = KeyboardEvent::Type::Grapheme;
            keyboardEvent.grapheme = c;
            handle_keyboard_event(keyboardEvent);
        }
    } break;

    case WM_DEADCHAR: {
        auto c = handleSuragates(narrow_cast<char32_t>(wParam));
        if (c != 0) {
            handle_keyboard_event(c, false);
        }
    } break;

    case WM_CHAR: {
        auto c = handleSuragates(narrow_cast<char32_t>(wParam));
        if (c >= 0x20) {
            handle_keyboard_event(c);
        }
    } break;

    case WM_SYSKEYDOWN: {
        auto alt_pressed = (narrow_cast<uint32_t>(lParam) & 0x20000000) != 0;
        if (!alt_pressed) {
            return -1;
        }
    }
        [[fallthrough]];
    case WM_KEYDOWN: {
        auto extended = (narrow_cast<uint32_t>(lParam) & 0x01000000) != 0;
        auto key_code = narrow_cast<int>(wParam);

        LOG_ERROR("Key 0x{:x} extended={}", key_code, extended);

        ttlet key_state = getKeyboardState();
        ttlet key_modifiers = getKeyboardModifiers();
        ttlet virtual_key = to_KeyboardVirtualKey(key_code, extended, key_modifiers);
        if (virtual_key != KeyboardVirtualKey::Nul) {
            handle_keyboard_event(key_state, key_modifiers, virtual_key);
        }
    } break;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_XBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    case WM_XBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE: handle_mouse_event(createMouseEvent(uMsg, wParam, lParam)); break;

    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            // When wParam is TRUE, simply returning 0 without processing the NCCALCSIZE_PARAMS rectangles
            // will cause the client area to resize to the size of the window, including the window frame.
            // This will remove the window frame and caption items from your window, leaving only the client area displayed.
            //
            // Starting with Windows Vista, removing the standard frame by simply
            // returning 0 when the wParam is TRUE does not affect frames that are
            // extended into the client area using the DwmExtendFrameIntoClientArea function.
            // Only the standard frame will be removed.
            return 0;
        }

        break;

    case WM_NCHITTEST: {
        gui_system_mutex.lock();
        ttlet screenPosition = vec{GET_X_LPARAM(lParam), 0 - GET_Y_LPARAM(lParam)};
        ttlet insideWindowPosition = screenPosition - vec{OSWindowRectangle.offset()};
        ttlet hitbox_type = widget->hitbox_test(insideWindowPosition).type;
        gui_system_mutex.unlock();

        switch (hitbox_type) {
        case HitBox::Type::BottomResizeBorder: setCursor(Cursor::None); return HTBOTTOM;
        case HitBox::Type::TopResizeBorder: setCursor(Cursor::None); return HTTOP;
        case HitBox::Type::LeftResizeBorder: setCursor(Cursor::None); return HTLEFT;
        case HitBox::Type::RightResizeBorder: setCursor(Cursor::None); return HTRIGHT;
        case HitBox::Type::BottomLeftResizeCorner: setCursor(Cursor::None); return HTBOTTOMLEFT;
        case HitBox::Type::BottomRightResizeCorner: setCursor(Cursor::None); return HTBOTTOMRIGHT;
        case HitBox::Type::TopLeftResizeCorner: setCursor(Cursor::None); return HTTOPLEFT;
        case HitBox::Type::TopRightResizeCorner: setCursor(Cursor::None); return HTTOPRIGHT;
        case HitBox::Type::ApplicationIcon: setCursor(Cursor::None); return HTSYSMENU;
        case HitBox::Type::MoveArea: setCursor(Cursor::None); return HTCAPTION;
        case HitBox::Type::TextEdit: setCursor(Cursor::TextEdit); return HTCLIENT;
        case HitBox::Type::Button: setCursor(Cursor::Button); return HTCLIENT;
        case HitBox::Type::Default: setCursor(Cursor::Default); return HTCLIENT;
        case HitBox::Type::Outside: setCursor(Cursor::None); return HTCLIENT;
        default: tt_no_default();
        }
    } break;

    case WM_SETTINGCHANGE: {
        ttlet lock = std::scoped_lock(gui_system_mutex);
        doubleClickMaximumDuration = GetDoubleClickTime() * 1ms;
        LOG_INFO("Double click duration {} ms", doubleClickMaximumDuration / 1ms);

        application->themes->settheme_mode(read_os_theme_mode());
        _request_setting_change = true;
    } break;

    case WM_DPICHANGED: {
        ttlet lock = std::scoped_lock(gui_system_mutex);
        // x-axis dpi value.
        dpi = narrow_cast<float>(LOWORD(wParam));
        requestLayout = true;
    } break;

    case WM_WIN_LANGUAGE_CHANGE:
        language::set_preferred_languages(language::read_os_preferred_languages());
        _request_setting_change = true;
        break;

    default: break;
    }

    // Let DefWindowProc() handle it.
    return -1;
}

[[nodiscard]] char32_t gui_window_vulkan_win32::handleSuragates(char32_t c) noexcept
{
    tt_assume(gui_system_mutex.recurse_lock_count() == 0);
    ttlet lock = std::scoped_lock(gui_system_mutex);

    if (c >= 0xd800 && c <= 0xdbff) {
        highSurrogate = ((c - 0xd800) << 10) + 0x10000;
        return 0;

    } else if (c >= 0xdc00 && c <= 0xdfff) {
        c = highSurrogate ? highSurrogate | (c - 0xdc00) : 0xfffd;
    }
    highSurrogate = 0;
    return c;
}

[[nodiscard]] MouseEvent gui_window_vulkan_win32::createMouseEvent(unsigned int uMsg, uint64_t wParam, int64_t lParam) noexcept
{
    // We have to do manual locking, since we don't want this
    // function or its caller to hold a lock while calling the windows API.
    // This function should only return once, to make it easier.
    tt_assume(gui_system_mutex.recurse_lock_count() == 0);
    gui_system_mutex.lock();

    auto mouseEvent = MouseEvent{};
    mouseEvent.timePoint = cpu_utc_clock::now();

    // On Window 7 up to and including Window10, the I-beam cursor hot-spot is 2 pixels to the left
    // of the vertical bar. But most applications do not fix this problem.
    mouseEvent.position = vec::point(GET_X_LPARAM(lParam), currentWindowExtent.y() - GET_Y_LPARAM(lParam));

    mouseEvent.wheelDelta = vec{};
    if (uMsg == WM_MOUSEWHEEL) {
        mouseEvent.wheelDelta.y(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA * 10.0f);
    } else if (uMsg == WM_MOUSEHWHEEL) {
        mouseEvent.wheelDelta.x(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA * 10.0f);
    }

    // Track which buttons are down, in case the application wants to track multiple buttons being pressed down.
    mouseEvent.down.controlKey = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) > 0;
    mouseEvent.down.leftButton = (GET_KEYSTATE_WPARAM(wParam) & MK_LBUTTON) > 0;
    mouseEvent.down.middleButton = (GET_KEYSTATE_WPARAM(wParam) & MK_MBUTTON) > 0;
    mouseEvent.down.rightButton = (GET_KEYSTATE_WPARAM(wParam) & MK_RBUTTON) > 0;
    mouseEvent.down.shiftKey = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) > 0;
    mouseEvent.down.x1Button = (GET_KEYSTATE_WPARAM(wParam) & MK_XBUTTON1) > 0;
    mouseEvent.down.x2Button = (GET_KEYSTATE_WPARAM(wParam) & MK_XBUTTON2) > 0;

    // Check which buttons caused the mouse event.
    switch (uMsg) {
    case WM_LBUTTONUP:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: mouseEvent.cause.leftButton = true; break;
    case WM_RBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK: mouseEvent.cause.rightButton = true; break;
    case WM_MBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK: mouseEvent.cause.middleButton = true; break;
    case WM_XBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
        mouseEvent.cause.x1Button = (GET_XBUTTON_WPARAM(wParam) & XBUTTON1) > 0;
        mouseEvent.cause.x2Button = (GET_XBUTTON_WPARAM(wParam) & XBUTTON2) > 0;
        break;
    case WM_MOUSEMOVE:
        if (mouseButtonEvent.type == MouseEvent::Type::ButtonDown) {
            mouseEvent.cause = mouseButtonEvent.cause;
        }
        break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_MOUSELEAVE: break;
    default: tt_no_default();
    }

    ttlet a_button_is_pressed = mouseEvent.down.leftButton || mouseEvent.down.middleButton || mouseEvent.down.rightButton ||
        mouseEvent.down.x1Button || mouseEvent.down.x2Button;

    switch (uMsg) {
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    case WM_XBUTTONUP:
        mouseEvent.type = MouseEvent::Type::ButtonUp;
        mouseEvent.downPosition = mouseButtonEvent.downPosition;
        mouseEvent.clickCount = 0;

        if (!a_button_is_pressed) {
            gui_system_mutex.unlock();
            tt_assume(gui_system_mutex.recurse_lock_count() == 0);
            ReleaseCapture();
            gui_system_mutex.lock();
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_XBUTTONDOWN: {
        mouseEvent.type = MouseEvent::Type::ButtonDown;
        mouseEvent.downPosition = mouseEvent.position;
        mouseEvent.clickCount = (mouseEvent.timePoint < doubleClickTimePoint + doubleClickMaximumDuration) ? 3 : 1;

        // Track draging past the window borders.
        tt_assume(win32Window != 0);
        ttlet window_handle = reinterpret_cast<HWND>(win32Window);

        gui_system_mutex.unlock();
        tt_assume(gui_system_mutex.recurse_lock_count() == 0);
        SetCapture(window_handle);
        gui_system_mutex.lock();
    } break;

    case WM_LBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_XBUTTONDBLCLK:
        mouseEvent.type = MouseEvent::Type::ButtonDown;
        mouseEvent.downPosition = mouseEvent.position;
        mouseEvent.clickCount = 2;
        doubleClickTimePoint = cpu_utc_clock::now();
        break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: mouseEvent.type = MouseEvent::Type::Wheel; break;

    case WM_MOUSEMOVE: {
        // XXX Make sure the mouse is moved enough for this to cause a drag event.
        mouseEvent.type = a_button_is_pressed ? MouseEvent::Type::Drag : MouseEvent::Type::Move;
        mouseEvent.downPosition = mouseButtonEvent.downPosition;
        mouseEvent.clickCount = mouseButtonEvent.clickCount;
    } break;

    case WM_MOUSELEAVE:
        mouseEvent.type = MouseEvent::Type::Exited;
        mouseEvent.downPosition = mouseButtonEvent.downPosition;
        mouseEvent.clickCount = 0;

        // After this event we need to ask win32 to track the mouse again.
        trackingMouseLeaveEvent = false;

        // Force currentCursor to None so that the Window is in a fresh
        // state when the mouse reenters it.
        currentCursor = Cursor::None;
        break;

    default: tt_no_default();
    }

    // Make sure we start tracking mouse events when the mouse has entered the window again.
    // So that once the mouse leaves the window we receive a WM_MOUSELEAVE event.
    if (!trackingMouseLeaveEvent && uMsg != WM_MOUSELEAVE) {
        auto *track_mouse_leave_event_parameters_p = &trackMouseLeaveEventParameters;
        gui_system_mutex.unlock();
        tt_assume(gui_system_mutex.recurse_lock_count() == 0);
        if (!TrackMouseEvent(track_mouse_leave_event_parameters_p)) {
            LOG_ERROR("Could not track leave event '{}'", getLastErrorMessage());
        }
        gui_system_mutex.lock();
        trackingMouseLeaveEvent = true;
    }

    // Remember the last time a button was pressed or released, so that we can convert
    // a move into a drag event.
    if (mouseEvent.type == MouseEvent::Type::ButtonDown || mouseEvent.type == MouseEvent::Type::ButtonUp ||
        mouseEvent.type == MouseEvent::Type::Exited) {
        mouseButtonEvent = mouseEvent;
    }

    gui_system_mutex.unlock();
    tt_assume(gui_system_mutex.recurse_lock_count() == 0);
    return mouseEvent;
}

} // namespace tt