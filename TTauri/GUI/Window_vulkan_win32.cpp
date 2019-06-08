// Copyright 2019 Pokitec
// All rights reserved.

#include "Window_vulkan_win32.hpp"
#include "Instance.hpp"
#include "TTauri/all.hpp"

namespace TTauri::GUI {

using namespace std;
using namespace TTauri;

template<typename T>
inline gsl::not_null<T *>to_ptr(LPARAM lParam)
{
    T *ptr;
    memcpy(&ptr, &lParam, sizeof(T *));

    return gsl::not_null<T *>(ptr);
}

const wchar_t *Window_vulkan_win32::win32WindowClassName = nullptr;
WNDCLASSW Window_vulkan_win32::win32WindowClass = {};
bool Window_vulkan_win32::win32WindowClassIsRegistered = false;
std::shared_ptr<std::unordered_map<HWND, Window_vulkan_win32 *>> Window_vulkan_win32::win32WindowMap = {};
bool Window_vulkan_win32::firstWindowHasBeenOpened = false;

void Window_vulkan_win32::createWindowClass()
{
    if (!Window_vulkan_win32::win32WindowClassIsRegistered) {
         // Register the window class.
        Window_vulkan_win32::win32WindowClassName = L"TTauri Window Class";

        Window_vulkan_win32::win32WindowClass.lpfnWndProc = Window_vulkan_win32::_WindowProc;
        Window_vulkan_win32::win32WindowClass.hInstance = application->hInstance;
        Window_vulkan_win32::win32WindowClass.lpszClassName = Window_vulkan_win32::win32WindowClassName;
        Window_vulkan_win32::win32WindowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&win32WindowClass);
    }
    Window_vulkan_win32::win32WindowClassIsRegistered = true;
}

void Window_vulkan_win32::createWindow(const std::string &title, u32extent2 extent)
{
    Window_vulkan_win32::createWindowClass();

    auto u16title = translateString<wstring>(title);

    win32Window = CreateWindowExW(
        0, // Optional window styles.
        Window_vulkan_win32::win32WindowClassName, // Window class
        u16title.data(), // Window text
        WS_OVERLAPPEDWINDOW, // Window style

        // Size and position
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        extent.width(),
        extent.height(),

        NULL, // Parent window
        NULL, // Menu
        application->hInstance, // Instance handle
        this
    );

    if (win32Window == nullptr) {
        BOOST_THROW_EXCEPTION(Application::Error());
    }

    if (!Window_vulkan_win32::firstWindowHasBeenOpened) {
        ShowWindow(win32Window, application->nCmdShow);
        Window_vulkan_win32::firstWindowHasBeenOpened = true;
    }
    ShowWindow(win32Window, SW_SHOW);
}

Window_vulkan_win32::Window_vulkan_win32(const std::shared_ptr<WindowDelegate> delegate, const std::string title) :
    Window_vulkan(move(delegate), title)
{
}

Window_vulkan_win32::~Window_vulkan_win32()
{
    try {
        [[gsl::suppress(f.6)]] {
            if (win32Window != nullptr) {
                LOG_FATAL("win32Window was not destroyed before Window '%s' was destructed.") % title;
                abort();
            }
        }
    } catch (...) {
        abort();
    }
}

void Window_vulkan_win32::closingWindow()
{
    // Don't lock mutex, no members of this are being accessed.
    PostThreadMessageW(application->mainThreadID, WM_APP_CLOSING_WINDOW, 0, reinterpret_cast<LPARAM>(this));
}

void Window_vulkan_win32::mainThreadClosingWindow()
{
    // Don't lock mutex, the window is about to be destructed.
    Window_vulkan::closingWindow();
}

void Window_vulkan_win32::openingWindow()
{
    // Don't lock mutex, no members of this are being accessed.
    PostThreadMessageW(application->mainThreadID, WM_APP_OPENING_WINDOW, 0, reinterpret_cast<LPARAM>(this));
}

void Window_vulkan_win32::mainThreadOpeningWindow()
{
    std::scoped_lock lock(TTauri::GUI::mutex);

    Window_vulkan::openingWindow();

    // Delegate has been called, layout of widgets has been calculated for the
    // minimum and maximum size of the window.
    u32extent2 windowExtent = minimumWindowExtent;
    createWindow(title, windowExtent);
}

vk::SurfaceKHR Window_vulkan_win32::getSurface()
{
    return instance->createWin32SurfaceKHR({
        vk::Win32SurfaceCreateFlagsKHR(),
        application->hInstance,
        win32Window
    });
}


LRESULT Window_vulkan_win32::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RECT* rect;
    MINMAXINFO* minmaxinfo;

    switch (uMsg) {    
    case WM_DESTROY: {
            std::scoped_lock lock(TTauri::GUI::mutex);

            win32Window = nullptr;
            state = State::WINDOW_LOST;
        }
        break;

    case WM_SIZING: {
            std::scoped_lock lock(TTauri::GUI::mutex);

            rect = to_ptr<RECT>(lParam);
            OSWindowRectangle.offset.x = rect->left;
            OSWindowRectangle.offset.y = 0; // XXX Without screen height, it is not possible to calculate the y of the left-bottom corner.
            // XXX - figure out size of decoration to remove these constants.
            OSWindowRectangle.extent.x = (rect->right - rect->left) - 26;
            OSWindowRectangle.extent.y = (rect->bottom - rect->top) - 39;
        }
        break;

    case WM_ENTERSIZEMOVE: {
            std::scoped_lock lock(TTauri::GUI::mutex);
            resizing = true;
        }
        break;

    case WM_EXITSIZEMOVE: {
            std::scoped_lock lock(TTauri::GUI::mutex);
            resizing = false;
        }
        break;
    
    case WM_GETMINMAXINFO: {
            std::scoped_lock lock(TTauri::GUI::mutex);

            minmaxinfo = to_ptr<MINMAXINFO>(lParam);
            // XXX - figure out size of decoration to remove these constants.
            minmaxinfo->ptMaxSize.x = maximumWindowExtent.width() + 26;
            minmaxinfo->ptMaxSize.y = maximumWindowExtent.height() + 39;
            minmaxinfo->ptMinTrackSize.x = minimumWindowExtent.width() + 26;
            minmaxinfo->ptMinTrackSize.y = minimumWindowExtent.height() + 39;
            minmaxinfo->ptMaxTrackSize.x = maximumWindowExtent.width() + 26;
            minmaxinfo->ptMaxTrackSize.y = maximumWindowExtent.height() + 39;
        }
        break;

    default:
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK Window_vulkan_win32::_WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (!win32WindowMap) {
        win32WindowMap = make_shared<std::unordered_map<HWND, Window_vulkan_win32 *>>();
    }

    if (uMsg == WM_NCCREATE && lParam) {
        [[gsl::suppress(type.1)]] {
            let createData = reinterpret_cast<CREATESTRUCT *>(lParam);

            if (createData->lpCreateParams) {
                [[gsl::suppress(lifetime.1)]] {
                    (*Window_vulkan_win32::win32WindowMap)[hwnd] = static_cast<Window_vulkan_win32 *>(createData->lpCreateParams);
                }
            }
        }
    }

    auto i = Window_vulkan_win32::win32WindowMap->find(hwnd);
    if (i != Window_vulkan_win32::win32WindowMap->end()) {
        let window = i->second;
        let result = window->windowProc(hwnd, uMsg, wParam, lParam);

        if (uMsg == WM_DESTROY) {
            Window_vulkan_win32::win32WindowMap->erase(i);
        }

        return result;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

}
