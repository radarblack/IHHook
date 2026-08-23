#pragma once
#include "SafeQueue.h"
#include <vector>
#include <string>

namespace IHHook {
	namespace IHMenu {
		//tex: plain function pointer, matching the existing menuCommands map in IHMenu.cpp -
		//no captures needed since handlers just forward parsed args on to whichever module
		//actually wants them (e.g. DebuggerMenu::OnDoScriptResult).
		typedef void(*MenuCommandFunc) (std::vector<std::string> args);

		void AddMenuCommands();
		void AddMenuCommand(const std::string& cmd, MenuCommandFunc func);//tex: lets other modules register without IHMenu.cpp needing to know about them
		void ProcessMessages();

		void SetInitialText();
		void DrawMenu(bool* p_open, bool openPrev);

		void QueueMessageOut(std::string message);
		void QueueMessageIn(std::string message);

		extern SafeQueue<std::string> messagesOut;
		extern SafeQueue<std::string> messagesIn;
	}//namespace IHMenu
}//IHHook
