#pragma once
#include "windowsapi.h"
#include <functional>

namespace IHHook {
	namespace RawInput {
		enum BUTTONEVENT {
			UP,
			ONDOWN,
			ONUP,
			HELD
		};

		//tex: was a plain function pointer (void(*)(BUTTONEVENT)) - widened to std::function so
		//dynamically-added key bindings (see KeyBindMenu) can register a capturing lambda that
		//remembers its own script path, one per vKey, rather than needing a separate named
		//function for every possible binding. Plain function pointers (ToggleMenu, MenuOff, etc.)
		//still work unchanged - they implicitly convert to std::function.
		typedef std::function<void(BUTTONEVENT buttonEvent)> ButtonAction;

		void InitializeInput();
		void HookWndProc(HWND hWnd);

		void RegisterAction(USHORT vKey, ButtonAction action);
		void UnRegisterAction(USHORT vKey);

		bool OnMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

		//		
		void BlockAll();
		void UnBlockAll();
		void BlockMouseClick(); 
		void UnBlockMouseClick();
		void BlockKeyboard();
		void UnBlockKeyboard();
	}//namespace RawInput
}//namespace IHHook
