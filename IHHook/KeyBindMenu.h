#pragma once
#include "windowsapi.h"
#include <string>
#include <vector>

namespace IHHook {
	namespace KeyBindMenu {
		//tex: one user-added "press this key, run this lua file" binding
		struct KeyBind {
			USHORT vKey;
			std::string keyName;     //tex: display name, e.g. "F6", "A", "Numpad5" - see vkNameTable
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
