#pragma once
#include "windowsapi.h"
#include "RawInput.h"
#include <string>
#include <vector>

namespace IHHook {
	namespace KeyBindMenu {
		//tex: one user-added "press this key (+ optional Ctrl/Shift/Alt), run this lua file" binding.
		//GOTCHA: no per-binding RawInput::ActionHandle here anymore - a single shared dispatcher
		//now covers every binding on a given vKey (see EnsureDispatcherRegistered in the .cpp),
		//since the dispatcher needs to see ALL bindings on that key at once to implement fallback
		//(e.g. Shift+X falls back to plain X if no Shift+X-specific binding exists).
		struct KeyBind {
			USHORT vKey;
			bool needCtrl;
			bool needShift;
			bool needAlt;
			std::string keyName;     //tex: base key display name, e.g. "F6", "A", "," - see vkNameTable
			std::string scriptPath;  //tex: passed to dofile() via the same DoScript IPC path RunKeyZScript uses
		};

		//tex: called once at startup (see IHHook.cpp init sequence) - loads persisted bindings
		//and the persisted menu-toggle key from ihhook_keybinds.txt (if present), registers them
		//all with RawInput, and registers the menu-toggle key action itself. defaultMenuKeyName
		//is the fallback starting key (from config.keyBindMenuToggleKey, e.g. "F4") used only if
		//ihhook_keybinds.txt doesn't already have a persisted MENUKEY override from a previous
		//in-menu remap.
		void Init(const std::string& defaultMenuKeyName);

		//tex: draws the key-bindings window. Only call while the menu is actually open -
		//mirrors IHMenu::DrawMenu(bool*, bool)'s p_open pattern (see IHHook.cpp's DrawUI).
		void Draw(bool* p_open);

		extern std::vector<KeyBind> bindings;
		extern bool menuOpen;//tex: IHHook.cpp's DrawUI checks this directly and passes &menuOpen into Draw()
	}//namespace KeyBindMenu
}//namespace IHHook
