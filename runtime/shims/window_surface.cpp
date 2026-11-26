// window_surface: pass-through shims for the title's window surface.
//
// The console image's window set is exactly seven functions (its import table): RegisterClassExA,
// CreateWindowExA, DefWindowProcA (the ANSI window class), and ShowWindow, PeekMessageA,
// TranslateMessage, DispatchMessageA (the message pump). CreateWindowExA/W is lifecycle.cpp's
// (oversized top-level fitting + the SC_CLOSE suspend hook) and must not be re-bound here.
//
// The title registers a class, creates a top-level window, and pumps a message loop; on the
// console the pump idles (there are no host messages). Every one of these calls is a plain user32
// operation on in-process values (the title's own WNDPROC, host HWND/ATOM handles), so each shim
// forwards to the host API unchanged: the class and window live in the host user32, the title's
// WNDPROC runs in-process (the GameInput shim already registers a guest WNDPROC this way), and
// the pump serves the window's real host messages. The ANSI variants are what the title imports;
// the bindings are repeated for user32.dll so a build that names the same functions there resolves
// them identically.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "shims.h"

namespace {

ATOM WINAPI register_class_ex_a(const WNDCLASSEXA* wc) {
    return ::RegisterClassExA(wc);
}

LRESULT WINAPI def_window_proc_a(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return ::DefWindowProcA(hwnd, message, wparam, lparam);
}

BOOL WINAPI show_window(HWND hwnd, int n_cmd_show) {
    return ::ShowWindow(hwnd, n_cmd_show);
}

BOOL WINAPI peek_message_a(MSG* msg, HWND hwnd, UINT min_filter, UINT max_filter) {
    // The modern SDK prototype takes a fifth wRemoveMsg argument; the title (and the console)
    // call the 4-argument form, so forward the classic PM_REMOVE semantics.
    return ::PeekMessageA(msg, hwnd, min_filter, max_filter, PM_REMOVE);
}

BOOL WINAPI translate_message(const MSG* msg) {
    return ::TranslateMessage(msg);
}

LRESULT WINAPI dispatch_message_a(const MSG* msg) {
    return ::DispatchMessageA(msg);
}

#define WINDOW_SURFACE_API(dll)                                                                        \
    {dll, "RegisterClassExA", reinterpret_cast<void*>(&register_class_ex_a)},                          \
    {dll, "DefWindowProcA", reinterpret_cast<void*>(&def_window_proc_a)},                              \
    {dll, "ShowWindow", reinterpret_cast<void*>(&show_window)},                                        \
    {dll, "PeekMessageA", reinterpret_cast<void*>(&peek_message_a)},                                   \
    {dll, "TranslateMessage", reinterpret_cast<void*>(&translate_message)},                            \
    {dll, "DispatchMessageA", reinterpret_cast<void*>(&dispatch_message_a)}

}  // namespace

SHIM_TABLE(window_surface,
    WINDOW_SURFACE_API("user32.dll"),
    WINDOW_SURFACE_API("ext-ms-win-rtcore-ntuser-window-ansi-l1-1-0.dll"),
    WINDOW_SURFACE_API("ext-ms-win-rtcore-ntuser-window-l1-1-0.dll"),
    WINDOW_SURFACE_API("ext-ms-win-rtcore-ntuser-message-ansi-l1-1-0.dll"),
    WINDOW_SURFACE_API("ext-ms-win-rtcore-ntuser-message-l1-1-0.dll"),
);
