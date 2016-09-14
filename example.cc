//
// The following code attempts to support multiple Windows with OpenGL, without
// having to resort to wglMakeCurrent calls as they're unusefully slow.
//

#include <stdlib.h>
#include <stdio.h>

#include <intrin.h>

#include <windows.h>
#include <gl/gl.h>

#define WINDOW_A windows[0]
#define WINDOW_B windows[1]

#define HDC_A dcs[0]
#define HDC_B dcs[1]

#define HGLRC_A glrcs[0]
#define HGLRC_B glrcs[1]

enum Method {
  TRADITIONAL = 1,
  BLACK_MAGIC = 666
};

static Method method_ = TRADITIONAL;

static HWND target_;

static void __declspec(naked) hooked_window_from_dc() {
  __asm {
    mov eax, dword ptr [target_]
    ret 4
  }
}

static void toggle_window_from_dc_hook() {
  void * const original = GetProcAddress(LoadLibraryA("user32.dll"), "WindowFromDC");

  // Deprotect to allow writing.
  DWORD old_virt_mem_protection;
  VirtualProtect((LPVOID)((DWORD)original - 5), 5 + 2, PAGE_EXECUTE_READWRITE, &old_virt_mem_protection);

  // Far relative jump to |hooked_window_from_dc| in the 5-bytes of NOPs prior
  // to the function preamble.
  *((unsigned char *)original - 5) = 0xE9;
  *((DWORD *)((unsigned char *)original - 5 + 1)) = (DWORD)&hooked_window_from_dc - (DWORD)original;

  if (*((unsigned short *)original) != 0xff8b) {
    // Unhook.
    *((unsigned short *)original) = 0xff8b;
  } else {
    // Short relative jump to far relative jump five bytes before.
    *((unsigned short *)original) = 0xf9eb;
  }

  // Not strictly necessary on x86.
  FlushInstructionCache(GetCurrentProcess(), (LPVOID)((DWORD)original - 5), 5 + 2);

  // Reprotect to allow execution.
  VirtualProtect((LPVOID)((DWORD)original - 5), 5 + 2, old_virt_mem_protection, NULL);
}

static LRESULT WINAPI _WindowProcW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    case WM_KEYUP: {
      if (wParam == 0x36) {
        method_ = (method_ == TRADITIONAL) ? BLACK_MAGIC : TRADITIONAL;
        toggle_window_from_dc_hook();
      }
    } return 0;

    case WM_CLOSE:
      exit(EXIT_SUCCESS);
      return TRUE;
  }

  return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

int main(int argc, const char **argv) {
  WNDCLASSEXW wcx;
  memset(&wcx, 0, sizeof(wcx));
  wcx.cbSize        = sizeof(WNDCLASSEX);
  wcx.style         = CS_VREDRAW | CS_HREDRAW | CS_OWNDC;
  wcx.lpfnWndProc   = &_WindowProcW;
  wcx.hInstance     = ::GetModuleHandle(NULL);
  wcx.hCursor       = ::LoadCursor(NULL, IDC_ARROW);
  // TODO(mtwilliams): Use our own icon, IDI_ENGINE_ICON?
  wcx.hIcon         = ::LoadIconA(wcx.hInstance, IDI_APPLICATION);
  wcx.hIconSm       = ::LoadIconA(wcx.hInstance, IDI_APPLICATION);
  wcx.lpszClassName = L"04f153d0-0a49-40d9-b282-8579afe03370";

  ::RegisterClassExW(&wcx);

  const DWORD styles = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  const DWORD ex_styles = WS_EX_APPWINDOW | WS_EX_OVERLAPPEDWINDOW;

  RECT client_area_a = { 0, 0, 640, 480 };
  ::AdjustWindowRectEx(&client_area_a, styles, FALSE, ex_styles);
  const DWORD adjusted_width_a = client_area_a.right - client_area_a.left;
  const DWORD adjusted_height_a = client_area_a.bottom - client_area_a.top;

  RECT client_area_b = { 0, 0, 1280, 720 };
  ::AdjustWindowRectEx(&client_area_b, styles, FALSE, ex_styles);
  const DWORD adjusted_width_b = client_area_b.right - client_area_b.left;
  const DWORD adjusted_height_b = client_area_b.bottom - client_area_b.top;

  const HWND windows[2] = {
    ::CreateWindowExW(ex_styles, wcx.lpszClassName, L"Window A", styles, 0, 0,
                      adjusted_width_a, adjusted_height_a, NULL, NULL,
                      ::GetModuleHandle(NULL), NULL),

    ::CreateWindowExW(ex_styles, wcx.lpszClassName, L"Window B", styles, 0, 0,
                      adjusted_width_b, adjusted_height_b, NULL, NULL,
                      ::GetModuleHandle(NULL), NULL)
  };

  ::ShowWindow(WINDOW_A, SW_SHOW);
  ::ShowWindow(WINDOW_B, SW_SHOW);

  PIXELFORMATDESCRIPTOR pfd;
  ZeroMemory(&pfd, sizeof(PIXELFORMATDESCRIPTOR));
  pfd.nSize        = sizeof(PIXELFORMATDESCRIPTOR);
  pfd.nVersion     = 1;
  pfd.dwFlags      = PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
  pfd.iPixelType   = PFD_TYPE_RGBA;
  pfd.cColorBits   = 24;
  pfd.cAlphaBits   = 0;
  pfd.cDepthBits   = 24;
  pfd.cStencilBits = 8;
  pfd.iLayerType   = PFD_MAIN_PLANE;

  const HDC dcs[2] = {
    GetDC(WINDOW_A),
    GetDC(WINDOW_B)
  };

  const int pixel_format_a = ChoosePixelFormat(HDC_A, &pfd);
  const int pixel_format_b = ChoosePixelFormat(HDC_B, &pfd);

  SetPixelFormat(HDC_A, pixel_format_a, &pfd);
  SetPixelFormat(HDC_B, pixel_format_b, &pfd);

  const HGLRC glrcs[2] = {
    wglCreateContext(HDC_A),
    wglCreateContext(HDC_B)
  };

  wglShareLists(HGLRC_A, HGLRC_B);
  wglMakeCurrent(HDC_A, HGLRC_A);

  while (true) {
    MSG msg;
    while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
    }

    for (size_t window = 0; window < 2; ++window) {
      switch (method_) {
        case TRADITIONAL: {
          // Render to A
          wglMakeCurrent(HDC_A, HGLRC_A);
          glViewport(0, 0, 640, 480);
          glClearColor(0.5f, 0.0f, 0.0f, 1.0f);
          glClear(GL_COLOR_BUFFER_BIT);
          SwapBuffers(HDC_A);

          // Render to B
          wglMakeCurrent(HDC_B, HGLRC_B);
          glViewport(0, 0, 1280, 720);
          glClearColor(0.0f, 0.0f, 0.5f, 1.0f);
          glClear(GL_COLOR_BUFFER_BIT);
          SwapBuffers(HDC_B);
        } break;

        case BLACK_MAGIC: {
          // Render to A
          glViewport(0, 0, 640, 480);
          glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
          glClear(GL_COLOR_BUFFER_BIT);

          target_ = WINDOW_A;
          SwapBuffers(HDC_A);

          // Render to B
          glViewport(0, 0, 1280, 720);
          glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
          glClear(GL_COLOR_BUFFER_BIT);

          target_ = WINDOW_B;
          SwapBuffers(HDC_B);
        } break;
      }
    }
  }

  return EXIT_SUCCESS;
}
