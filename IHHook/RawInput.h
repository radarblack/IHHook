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

		//tex: opaque id returned by RegisterAction, identifying one specific registered action so
		//it alone (not every action on that vKey) can be removed later - see UnRegisterAction(vKey, handle).
		//Needed once more than one action can share a vKey (e.g. KeyBindMenu's "Z" and "Shift+Z"
		//both fundamentally register against vKey='Z'; without a handle, removing one via the old
		//UnRegisterAction(vKey) would silently also delete the other).
		typedef unsigned long long ActionHandle;

		void InitializeInput();
		void HookWndProc(HWND hWnd);

		//tex: return value can be safely ignored by callers that never need to remove their own
		//action individually (ToggleMenu, MenuOff, etc. never call UnRegisterAction at all).
		ActionHandle RegisterAction(USHORT vKey, ButtonAction action);

		void UnRegisterAction(USHORT vKey);//tex: old behaviour - removes EVERY action on this vKey
		void UnRegisterAction(USHORT vKey, ActionHandle handle);//tex: removes just the one matching action

		//tex: true if vKey is currently held down - lets an action registered on one vKey check
		//whether a modifier (VK_SHIFT/VK_MENU) is also currently held, e.g. to distinguish "Z" from
		//"Shift+Z" (both are still fundamentally a 'Z' key event; the modifier check happens inside
		//the registered action itself, see KeyBindMenu::RegisterBindingAction).
		bool IsKeyDown(USHORT vKey);

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
