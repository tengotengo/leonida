// Checks the window-surface pass-through shims (runtime/shims/window_surface.cpp): the title's
// class registration, window creation through lifecycle's CreateWindowExA shim, and the
// message-pump forwarding. The window and class are destroyed at the end.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdio>

#include "shims.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* what) {
    ++g_checks;
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) ++g_failures;
}

int g_user_message = 0;

LRESULT CALLBACK test_wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_USER + 1) ++g_user_message;
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

using RegExFn = ATOM(WINAPI*)(const WNDCLASSEXA*);
using DefProcFn = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using ShowFn = BOOL(WINAPI*)(HWND, int);
using PeekFn = BOOL(WINAPI*)(MSG*, HWND, UINT, UINT);
using TranslateFn = BOOL(WINAPI*)(const MSG*);
using DispatchFn = LRESULT(WINAPI*)(const MSG*);
using CreateExAFn = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU,
                                  HINSTANCE, LPVOID);

}  // namespace

int main() {
    const auto reg = reinterpret_cast<RegExFn>(
        rt::shims::find("ext-ms-win-rtcore-ntuser-window-ansi-l1-1-0.dll", "RegisterClassExA"));
    const auto def = reinterpret_cast<DefProcFn>(
        rt::shims::find("ext-ms-win-rtcore-ntuser-window-ansi-l1-1-0.dll", "DefWindowProcA"));
    const auto show = reinterpret_cast<ShowFn>(
        rt::shims::find("ext-ms-win-rtcore-ntuser-window-l1-1-0.dll", "ShowWindow"));
    const auto peek = reinterpret_cast<PeekFn>(
        rt::shims::find("ext-ms-win-rtcore-ntuser-message-ansi-l1-1-0.dll", "PeekMessageA"));
    const auto translate = reinterpret_cast<TranslateFn>(
        rt::shims::find("ext-ms-win-rtcore-ntuser-message-l1-1-0.dll", "TranslateMessage"));
    const auto dispatch = reinterpret_cast<DispatchFn>(
        rt::shims::find("ext-ms-win-rtcore-ntuser-message-ansi-l1-1-0.dll", "DispatchMessageA"));
    const auto create_a = reinterpret_cast<CreateExAFn>(rt::shims::find("user32.dll", "CreateWindowExA"));

    check(reg != nullptr && def != nullptr && show != nullptr && peek != nullptr && translate != nullptr &&
              dispatch != nullptr,
          "the six window-surface shims resolve through the title's ext-ms-win-rtcore-ntuser names");
    check(create_a != nullptr, "lifecycle's CreateWindowExA resolves");
    // The same bindings under user32.dll (a build may name them there).
    check(rt::shims::find("user32.dll", "RegisterClassExA") == reg &&
              rt::shims::find("user32.dll", "DefWindowProcA") == def &&
              rt::shims::find("user32.dll", "ShowWindow") == show &&
              rt::shims::find("user32.dll", "PeekMessageA") == peek &&
              rt::shims::find("user32.dll", "TranslateMessage") == translate &&
              rt::shims::find("user32.dll", "DispatchMessageA") == dispatch,
          "user32.dll names resolve to the same shims");

    // Register the test class through the shim; the class must exist in the host user32 so
    // lifecycle's CreateWindowExA pass-through can find it by name.
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &test_wnd_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = "leonida_window_surface_test";
    const ATOM atom = reg(&wc);
    WNDCLASSEXA cls{};
    cls.cbSize = sizeof(cls);
    const BOOL class_info = ::GetClassInfoExA(wc.hInstance, "leonida_window_surface_test", &cls);
    std::printf("  diag: atom=%p class_info=%d wndproc=%p expected=%p\n",
                (void*)atom, (int)class_info, (void*)cls.lpfnWndProc, (void*)&test_wnd_proc);
    check(atom != 0, "RegisterClassExA returns a class atom");
    check(class_info != 0, "GetClassInfoExA finds the class in the host (the shim forwarded)");
    check(cls.lpfnWndProc == reinterpret_cast<WNDPROC>(&test_wnd_proc),
          "the host class keeps the guest WNDPROC");

    // Create the window through lifecycle's shim (fitting + the SC_CLOSE hook), small so it is
    // not fitted.
    const HWND hwnd =
        create_a(0, "leonida_window_surface_test", "window surface test", WS_OVERLAPPEDWINDOW,
                 CW_USEDEFAULT, CW_USEDEFAULT, 100, 50, nullptr, nullptr, wc.hInstance, nullptr);
    check(hwnd != nullptr && IsWindow(hwnd), "CreateWindowExA (lifecycle) finds the shim-registered class");

    if (hwnd) {
        const BOOL shown = show(hwnd, SW_SHOWNOACTIVATE);
        const LONG_PTR style_after = GetWindowLongPtrA(hwnd, GWL_STYLE);
        std::printf("  diag: ShowWindow=%d GWL_STYLE=0x%lx err=%lu\n", (int)shown, (unsigned long)style_after,
                    (unsigned long)GetLastError());
        check(shown == TRUE || (style_after & WS_VISIBLE) != 0,
              "ShowWindow forwards: the window is shown (host may report FALSE on some wayland hosts)");

        // Post one message to the window and pump it through the shims: peek, translate,
        // dispatch (the guest WNDPROC must receive it), then the queue is empty again.
        PostMessageA(hwnd, WM_USER + 1, 0, 0);
        MSG msg{};
        check(peek(&msg, hwnd, 0, 0) == TRUE && msg.message == WM_USER + 1 && msg.hwnd == hwnd,
              "PeekMessageA forwards: the posted message is in the host queue");
        translate(&msg);  // no accelerator table; only that it forwards without faulting
        dispatch(&msg);
        check(g_user_message == 1, "DispatchMessageA forwards: the guest WNDPROC received the message");
        // A real top-level window receives host messages (paint/size/pos): drain the queue
        // through the shims, then it must be empty.
        int drained = 0;
        for (MSG next{}; (peek(&next, hwnd, 0, 0) == TRUE) && drained < 256; ++drained) {
            translate(&next);
            dispatch(&next);
        }
        std::printf("  diag: drained %d host messages after the posted one\n", drained);
        MSG next{};
        check(peek(&next, hwnd, 0, 0) == FALSE, "PeekMessageA forwards: the queue is empty after the drain");

        // The shim's DefWindowProc answers like the host's default handler.
        check(def(hwnd, WM_ERASEBKGND, 1, 0) != 0, "DefWindowProcA forwards to the host default");
        DestroyWindow(hwnd);
    }
    UnregisterClassA("leonida_window_surface_test", wc.hInstance);

    std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
