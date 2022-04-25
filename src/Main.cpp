#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <d3d12.h>       // D3D interface
#include <dxgi.h>        // DirectX driver interface
#include <d3dcompiler.h> // shader compiler

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

LRESULT WINAPI WndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam);

int main(int argc, char *argv[])
{
	WNDCLASSEXA cls = WNDCLASSEXA {
		sizeof(WNDCLASSEXA),
	};

	cls.style = CS_CLASSDC;
	cls.lpfnWndProc = WndProc;
	cls.lpszClassName = "Game";
	cls.hInstance = GetModuleHandleA(NULL);
	RegisterClassExA(&cls);

	HWND wnd = CreateWindowExA(WS_EX_APPWINDOW, cls.lpszClassName, "PlebCraft - Treidex", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, cls.hInstance, NULL);

	ShowWindow(wnd, SW_SHOWMINNOACTIVE);
	UpdateWindow(wnd);

	while (true)
	{
		MSG msg;
		while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageA(&msg);

			if (msg.message == WM_QUIT)
				goto end;
		}

		
	}

end:
	DestroyWindow(wnd);
	UnregisterClassA(cls.lpszClassName, cls.hInstance);
	system("pause");
}


LRESULT WINAPI WndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcA(wnd, msg, wParam, lParam);
}