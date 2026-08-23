#pragma once
#include <string>
#include <vector>

namespace IHHook {
	namespace DebuggerMenu {
		//tex: independent toggles - see Draw()'s "Log All" checkbox, which just sets/clears
		//all three together as a convenience, not a separate fourth mode.
		extern bool logBindUnbind;
		extern bool logButtonPress;
		extern bool logScriptResult;

		extern bool menuOpen;//tex: opened via a button inside KeyBindMenu::Draw(), not its own hotkey

		//tex: call once at startup (see IHHook.cpp init sequence, after IHMenu::AddMenuCommands())-
		//registers the "DoScriptResult" IHMenu command that InfExtToMgsv.lua's DoScript reports
		//back through after actually attempting to run a script.
		void Init();

		void Draw(bool* p_open);

		//tex: each is a no-op unless its matching logXxx toggle is enabled, so callers (KeyBindMenu,
		//RawInput's RunKeyZScript) don't need to check the toggle themselves.
		void LogBindEvent(const std::string& message);
		void LogButtonPress(const std::string& message);

		//tex: does the dll-side file-existence check itself, always (not just when logScriptResult
		//is on) - this doubles as validation, not just logging: false means the script file wasn't
		//found on disk, so the caller should NOT queue the DoScript IPC message at all. Returns
		//true if the file exists and the caller should proceed. The eventual success/failure of
		//actually running it arrives later, asynchronously, via the registered DoScriptResult command.
		bool LogScriptAttempt(const std::string& scriptPath);
	}//namespace DebuggerMenu
}//namespace IHHook
