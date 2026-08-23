#include "KeyBindMenu.h"
#include "RawInput.h"
#include "IHMenu.h"
#include "DebuggerMenu.h"
#include "Util.h"
#include "spdlog/spdlog.h"
#include "imgui/imgui.h"

#include <fstream>
#include <filesystem>
#include <map>

namespace IHHook {
	namespace KeyBindMenu {

		std::vector<KeyBind> bindings;

		//tex: lives under mod/radarKeys/ (created automatically if missing - see SaveBindings)
		//alongside any other files this mod needs, rather than loose at game root next to
		//dinput8.dll. Renamed from the old ihhook_keybinds.txt/.conf naming to make clear this
		//is specifically this mod's own config, not a generic ihhook file.
		//GOTCHA: this is a path/name change from earlier builds - an existing ihhook_keybinds.txt
		//at game root is NOT migrated automatically; bindings saved under the old name need to be
		//re-added once under this new path.
		const std::string bindsFileName = "mod/radarKeys/radar_keybinds.conf";

		//tex: not exhaustive, but covers the common cases for a dropdown. name is what's shown
		//in the combo box and what's persisted to disk (so the file stays human-readable/editable).
		//Ctrl/Shift/Alt are NOT in here - they're modifier checkboxes, not selectable base keys
		//(see the Draw() Add-binding section).
		struct VkNameEntry { const char* name; USHORT vKey; };
		const VkNameEntry vkNameTable[] = {
			{"A", 'A'}, {"B", 'B'}, {"C", 'C'}, {"D", 'D'}, {"E", 'E'}, {"F", 'F'},
			{"G", 'G'}, {"H", 'H'}, {"I", 'I'}, {"J", 'J'}, {"K", 'K'}, {"L", 'L'},
			{"M", 'M'}, {"N", 'N'}, {"O", 'O'}, {"P", 'P'}, {"Q", 'Q'}, {"R", 'R'},
			{"S", 'S'}, {"T", 'T'}, {"U", 'U'}, {"V", 'V'}, {"W", 'W'}, {"X", 'X'},
			{"Y", 'Y'}, {"Z", 'Z'},
			{"0", '0'}, {"1", '1'}, {"2", '2'}, {"3", '3'}, {"4", '4'},
			{"5", '5'}, {"6", '6'}, {"7", '7'}, {"8", '8'}, {"9", '9'},
			{"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4},
			{"F5", VK_F5}, {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8},
			{"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
			{"Space", VK_SPACE}, {"Tab", VK_TAB}, {"Enter", VK_RETURN},
			{"Insert", VK_INSERT}, {"Delete", VK_DELETE},
			{"Home", VK_HOME}, {"End", VK_END},
			{"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT},
			{"Up", VK_UP}, {"Down", VK_DOWN}, {"Left", VK_LEFT}, {"Right", VK_RIGHT},
			{"Numpad0", VK_NUMPAD0}, {"Numpad1", VK_NUMPAD1}, {"Numpad2", VK_NUMPAD2},
			{"Numpad3", VK_NUMPAD3}, {"Numpad4", VK_NUMPAD4}, {"Numpad5", VK_NUMPAD5},
			{"Numpad6", VK_NUMPAD6}, {"Numpad7", VK_NUMPAD7}, {"Numpad8", VK_NUMPAD8},
			{"Numpad9", VK_NUMPAD9},
			{",", VK_OEM_COMMA}, {".", VK_OEM_PERIOD},
		};
		const int vkNameTableCount = sizeof(vkNameTable) / sizeof(vkNameTable[0]);

		std::string NameForVKey(USHORT vKey) {
			for (int i = 0; i < vkNameTableCount; i++) {
				if (vkNameTable[i].vKey == vKey) {
					return vkNameTable[i].name;
				}
			}
			return "Unknown(" + std::to_string(vKey) + ")";
		}//NameForVKey

		//tex: returns -1 if not found in the table
		int VKeyForName(const std::string& name) {
			for (int i = 0; i < vkNameTableCount; i++) {
				if (name == vkNameTable[i].name) {
					return vkNameTable[i].vKey;
				}
			}
			return -1;
		}//VKeyForName

		//tex: e.g. "Ctrl+Shift+Alt+Z", "Shift+X", "X" - what's shown in the bindings list.
		//Ctrl/Shift/Alt ordering matches the common Windows accelerator-key convention.
		std::string CombinedDisplayName(const KeyBind& bind) {
			std::string result;
			if (bind.needCtrl) result += "Ctrl+";
			if (bind.needShift) result += "Shift+";
			if (bind.needAlt) result += "Alt+";
			result += bind.keyName;
			return result;
		}//CombinedDisplayName

		//tex: the key that opens/closes this menu itself - defaults from config.keyBindMenuToggleKey
		//(see IHHook.cpp ParseConfig) but can be live-remapped from within the menu, at which point
		//this persisted value takes precedence on next launch (see LoadBindings/SaveBindings).
		//GOTCHA: unlike custom bindings, the menu-toggle key intentionally does NOT support Ctrl/
		//Shift/Alt modifiers - keeping it a single plain key avoids complicating the one binding
		//that must always be reachable to fix/undo everything else.
		USHORT menuToggleVKey = VK_F4;
		RawInput::ActionHandle menuToggleHandle = 0;
		bool menuOpen = false;

		//tex: one shared RawInput action per physical vKey that has at least one custom binding,
		//rather than one action per (vKey, modifier) binding. Needed so OnBoundKeyPressed can see
		//ALL bindings on that key at once and implement fallback (see below) - individual bindings
		//independently checking only their own exact modifier match can't express "fall back to
		//the plain binding if the held modifiers don't match anything more specific".
		std::map<USHORT, RawInput::ActionHandle> vKeyDispatchers;

		//tex: RawInput action wired to whatever menuToggleVKey currently is - see RegisterMenuToggleKey
		void OnMenuToggleKeyPressed(RawInput::BUTTONEVENT buttonEvent) {
			if (buttonEvent != RawInput::BUTTONEVENT::ONDOWN) {
				return;
			}
			menuOpen = !menuOpen;
		}//OnMenuToggleKeyPressed

		void RegisterMenuToggleKey(USHORT vKey) {
			menuToggleVKey = vKey;
			menuToggleHandle = RawInput::RegisterAction(vKey, OnMenuToggleKeyPressed);
		}//RegisterMenuToggleKey

		//tex: reserved regardless of modifiers - these built-in actions (ToggleCursor, ToggleMenu,
		//MenuOff, RunKeyZScript, and this menu's own toggle key) don't check modifier state
		//themselves, so e.g. Shift+F2 would still fire the plain ToggleCursor action alongside
		//whatever a new Shift+F2 custom binding did. Simplest to just keep these fully reserved.
		bool IsReservedVKey(USHORT vKey) {
			return vKey == VK_F2 || vKey == VK_F3 || vKey == VK_ESCAPE || vKey == 'Z' || vKey == menuToggleVKey;
		}//IsReservedVKey

		//tex: true if this exact key+modifier combination is free to bind. Different modifier
		//combos on the SAME physical vKey (e.g. "F6" and "Shift+F6") are different, non-conflicting
		//bindings - that's the whole point of supporting modifiers - so this checks the full
		//(vKey, needCtrl, needShift, needAlt) tuple against existing bindings, not vKey alone.
		bool IsComboAvailable(USHORT vKey, bool needCtrl, bool needShift, bool needAlt) {
			if (IsReservedVKey(vKey)) {
				return false;
			}
			for (const KeyBind& bind : bindings) {
				if (bind.vKey == vKey && bind.needCtrl == needCtrl && bind.needShift == needShift && bind.needAlt == needAlt) {
					return false;
				}
			}
			return true;
		}//IsComboAvailable

		//tex: fired once per physical vKey, regardless of how many bindings share that key.
		//Looks for an EXACT modifier match first (e.g. Ctrl+Shift+X only fires if Ctrl+Shift+X is
		//held exactly); if none exists, falls back to the plain/unmodified binding on that same
		//vKey if one exists - e.g. holding Shift (to run) and pressing X still fires a plain "X"
		//binding when no "Shift+X"-specific binding was ever added. If neither exists, nothing fires.
		void OnBoundKeyPressed(USHORT vKey, RawInput::BUTTONEVENT buttonEvent) {
			if (buttonEvent != RawInput::BUTTONEVENT::ONDOWN) {
				return;
			}

			bool ctrlHeld = RawInput::IsKeyDown(VK_CONTROL);
			bool shiftHeld = RawInput::IsKeyDown(VK_SHIFT);
			bool altHeld = RawInput::IsKeyDown(VK_MENU);

			//tex: "log when a button was pressed" fires for every ONDOWN on a bound vKey,
			//regardless of whether it actually resolves to a script below - this is meant to
			//report the raw key event, not just successful matches.
			std::string pressedName;
			if (ctrlHeld) pressedName += "Ctrl+";
			if (shiftHeld) pressedName += "Shift+";
			if (altHeld) pressedName += "Alt+";
			pressedName += NameForVKey(vKey);
			DebuggerMenu::LogButtonPress(pressedName + " pressed");

			const KeyBind* exactMatch = nullptr;
			const KeyBind* fallbackMatch = nullptr;//tex: the plain/no-modifier binding on this vKey, if any
			for (const KeyBind& bind : bindings) {
				if (bind.vKey != vKey) {
					continue;
				}
				if (bind.needCtrl == ctrlHeld && bind.needShift == shiftHeld && bind.needAlt == altHeld) {
					exactMatch = &bind;
					break;
				}
				if (!bind.needCtrl && !bind.needShift && !bind.needAlt) {
					fallbackMatch = &bind;
				}
			}

			const KeyBind* toRun = exactMatch != nullptr ? exactMatch : fallbackMatch;
			if (toRun != nullptr) {
				//tex: LogScriptAttempt does the actual dll-side file-existence check (not just
				//logging) - only queue the IPC message to Lua if the file is really there. If it
				//returns false, it already logged the "not found" entry itself.
				if (DebuggerMenu::LogScriptAttempt(toRun->scriptPath)) {
					IHMenu::QueueMessageIn("DoScript|dofile([[" + toRun->scriptPath + "]])");
				}
			}
		}//OnBoundKeyPressed

		//tex: registers the shared dispatcher for vKey if it doesn't already have one - safe to
		//call repeatedly for the same vKey (e.g. once per binding sharing that key).
		void EnsureDispatcherRegistered(USHORT vKey) {
			if (vKeyDispatchers.find(vKey) != vKeyDispatchers.end()) {
				return;
			}
			RawInput::ActionHandle handle = RawInput::RegisterAction(vKey, [vKey](RawInput::BUTTONEVENT buttonEvent) {
				OnBoundKeyPressed(vKey, buttonEvent);
			});
			vKeyDispatchers[vKey] = handle;
		}//EnsureDispatcherRegistered

		//tex: only actually unregisters the dispatcher once NO bindings remain on that vKey -
		//other modifier combos on the same physical key may still need it.
		void RemoveDispatcherIfUnused(USHORT vKey) {
			for (const KeyBind& bind : bindings) {
				if (bind.vKey == vKey) {
					return;//tex: still in use by another binding
				}
			}
			auto it = vKeyDispatchers.find(vKey);
			if (it != vKeyDispatchers.end()) {
				RawInput::UnRegisterAction(vKey, it->second);
				vKeyDispatchers.erase(it);
			}
		}//RemoveDispatcherIfUnused

		void SaveBindings() {
			//tex: ofstream won't create missing intermediate directories itself - ensure
			//mod/radarKeys/ exists before attempting to open the file inside it (matters on
			//first run, or first run after this path changed from the old game-root location).
			std::error_code ec;
			std::filesystem::create_directories("mod/radarKeys", ec);
			if (ec) {
				spdlog::warn("KeyBindMenu::SaveBindings: couldn't create mod/radarKeys directory: {}", ec.message());
			}

			std::ofstream outFile(bindsFileName);
			if (!outFile) {
				spdlog::warn("KeyBindMenu::SaveBindings: couldn't open {} for writing", bindsFileName);
				return;
			}
			outFile << "MENUKEY|" << NameForVKey(menuToggleVKey) << "\n";
			for (const KeyBind& bind : bindings) {
				outFile << "BIND|" << bind.keyName << "|" << (bind.needCtrl ? "1" : "0") << "|" << (bind.needShift ? "1" : "0") << "|" << (bind.needAlt ? "1" : "0") << "|" << bind.scriptPath << "\n";
			}
			outFile.close();
			spdlog::debug("KeyBindMenu::SaveBindings: wrote {} binding(s) to {}", bindings.size(), bindsFileName);
		}//SaveBindings

		void LoadBindings() {
			std::ifstream inFile(bindsFileName);
			if (!inFile) {
				spdlog::debug("KeyBindMenu::LoadBindings: no {} yet (fine on first run)", bindsFileName);
				return;
			}

			std::string line;
			while (std::getline(inFile, line)) {
				line = trim(line);
				if (line.size() == 0) {
					continue;
				}
				std::vector<std::string> parts = split(line, "|");
				if (parts.size() < 2) {
					spdlog::warn("KeyBindMenu::LoadBindings: skipping malformed line: {}", line);
					continue;
				}

				if (parts[0] == "MENUKEY") {
					int vKey = VKeyForName(trim(parts[1]));
					if (vKey != -1) {
						menuToggleVKey = (USHORT)vKey;
					}
					else {
						spdlog::warn("KeyBindMenu::LoadBindings: unknown MENUKEY name '{}', keeping default", parts[1]);
					}
				}
				else if (parts[0] == "BIND" && parts.size() >= 6) {
					std::string keyName = trim(parts[1]);
					bool needCtrl = trim(parts[2]) == "1";
					bool needShift = trim(parts[3]) == "1";
					bool needAlt = trim(parts[4]) == "1";
					std::string scriptPath = trim(parts[5]);
					int vKey = VKeyForName(keyName);
					if (vKey == -1) {
						spdlog::warn("KeyBindMenu::LoadBindings: unknown key name '{}', skipping binding", keyName);
						continue;
					}
					bindings.push_back(KeyBind{ (USHORT)vKey, needCtrl, needShift, needAlt, keyName, scriptPath });
				}
				else if (parts[0] == "BIND") {
					//tex: catches the older 3-field and 5-field formats from earlier builds -
					//skip gracefully rather than crash on out-of-range access.
					spdlog::warn("KeyBindMenu::LoadBindings: skipping old-format/malformed BIND line: {}", line);
				}
			}//while line
			spdlog::debug("KeyBindMenu::LoadBindings: loaded {} binding(s) from {}", bindings.size(), bindsFileName);
		}//LoadBindings

		void AddBinding(USHORT vKey, const std::string& keyName, bool needCtrl, bool needShift, bool needAlt, const std::string& scriptPath) {
			KeyBind bind{ vKey, needCtrl, needShift, needAlt, keyName, scriptPath };
			bindings.push_back(bind);
			EnsureDispatcherRegistered(vKey);
			SaveBindings();
			DebuggerMenu::LogBindEvent("bound " + CombinedDisplayName(bind) + " -> " + scriptPath);
		}//AddBinding

		void RemoveBinding(int index) {
			if (index < 0 || index >= (int)bindings.size()) {
				return;
			}
			std::string removedDesc = CombinedDisplayName(bindings[index]) + " -> " + bindings[index].scriptPath;
			USHORT vKey = bindings[index].vKey;
			bindings.erase(bindings.begin() + index);
			RemoveDispatcherIfUnused(vKey);
			SaveBindings();
			DebuggerMenu::LogBindEvent("unbound " + removedDesc);
		}//RemoveBinding

		void RemoveAllBindings() {
			size_t count = bindings.size();
			for (const auto& entry : vKeyDispatchers) {
				RawInput::UnRegisterAction(entry.first, entry.second);
			}
			vKeyDispatchers.clear();
			bindings.clear();
			SaveBindings();
			DebuggerMenu::LogBindEvent("unbound all (" + std::to_string(count) + " binding(s))");
		}//RemoveAllBindings

		void Init(const std::string& defaultMenuKeyName) {
			int defaultVKey = VKeyForName(defaultMenuKeyName);
			if (defaultVKey != -1) {
				menuToggleVKey = (USHORT)defaultVKey;
			}
			else if (!defaultMenuKeyName.empty()) {
				spdlog::warn("KeyBindMenu::Init: unknown keyBindMenuToggleKey '{}' in ihhook_config.lua, using F4", defaultMenuKeyName);
			}

			LoadBindings();//tex: may override menuToggleVKey again if ihhook_keybinds.txt has a persisted MENUKEY
			for (const KeyBind& bind : bindings) {
				EnsureDispatcherRegistered(bind.vKey);
			}
			RegisterMenuToggleKey(menuToggleVKey);
		}//Init

		void Draw(bool* p_open) {
			ImGui::SetNextWindowSize(ImVec2(460, 460), ImGuiCond_::ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("IHHook Key Bindings", p_open)) {
				ImGui::End();
				return;
			}

			if (ImGui::Button("Debugger")) {
				DebuggerMenu::menuOpen = !DebuggerMenu::menuOpen;
			}
			ImGui::Separator();

			//tex: remap the menu's own toggle key (no modifier support here - see IsReservedVKey comment)
			ImGui::Text("Menu opens with: %s", NameForVKey(menuToggleVKey).c_str());
			ImGui::SameLine();
			static int menuKeyComboIndex = -1;
			if (menuKeyComboIndex == -1) {
				for (int i = 0; i < vkNameTableCount; i++) {
					if (vkNameTable[i].vKey == menuToggleVKey) {
						menuKeyComboIndex = i;
						break;
					}
				}
				if (menuKeyComboIndex == -1) menuKeyComboIndex = 0;
			}
			ImGui::SetNextItemWidth(100);
			if (ImGui::BeginCombo("##menuKeyCombo", vkNameTable[menuKeyComboIndex].name)) {
				for (int i = 0; i < vkNameTableCount; i++) {
					bool selected = (i == menuKeyComboIndex);
					if (ImGui::Selectable(vkNameTable[i].name, selected)) {
						menuKeyComboIndex = i;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::Button("Apply##menuKey")) {
				USHORT newVKey = vkNameTable[menuKeyComboIndex].vKey;
				if (newVKey == menuToggleVKey || IsComboAvailable(newVKey, false, false, false)) {
					RawInput::UnRegisterAction(menuToggleVKey, menuToggleHandle);
					RegisterMenuToggleKey(newVKey);
					SaveBindings();
				}
				else {
					spdlog::warn("KeyBindMenu: can't remap menu-toggle key to {} - already in use", vkNameTable[menuKeyComboIndex].name);
				}
			}

			ImGui::Separator();
			ImGui::TextWrapped("Custom bindings - press a key (+ Ctrl/Shift/Alt if set) in-game to dofile() the matching script. A modified binding (e.g. Shift+X) falls back to the plain key's binding (X) if no exact match exists for the modifiers currently held.");
			ImGui::Spacing();

			//tex: existing bindings list, each with its own remove button, plus a bulk "Remove All"
			int removeIndex = -1;
			ImGui::BeginChild("BindingsList", ImVec2(0, 180), true);
			for (int i = 0; i < (int)bindings.size(); i++) {
				ImGui::PushID(i);
				ImGui::Text("%s", CombinedDisplayName(bindings[i]).c_str());
				ImGui::SameLine(140);
				ImGui::TextWrapped("%s", bindings[i].scriptPath.c_str());
				ImGui::SameLine();
				if (ImGui::Button("Remove")) {
					removeIndex = i;
				}
				ImGui::PopID();
				ImGui::Separator();
			}
			ImGui::EndChild();
			if (removeIndex != -1) {
				RemoveBinding(removeIndex);
			}

			if (bindings.empty()) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Remove All Bindings")) {
				RemoveAllBindings();
			}
			if (bindings.empty()) {
				ImGui::EndDisabled();
			}

			ImGui::Spacing();
			ImGui::Text("Add new binding");

			static int addComboIndex = 0;
			ImGui::SetNextItemWidth(100);
			if (ImGui::BeginCombo("Key##addCombo", vkNameTable[addComboIndex].name)) {
				for (int i = 0; i < vkNameTableCount; i++) {
					bool selected = (i == addComboIndex);
					if (ImGui::Selectable(vkNameTable[i].name, selected)) {
						addComboIndex = i;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			static bool addCtrl = false;
			static bool addShift = false;
			static bool addAlt = false;
			ImGui::Checkbox("Ctrl", &addCtrl);
			ImGui::SameLine();
			ImGui::Checkbox("Shift", &addShift);
			ImGui::SameLine();
			ImGui::Checkbox("Alt", &addAlt);

			//tex: plain text input rather than a native file-browse dialog - keeps this feature
			//self-contained with no new Win32 API surface/library dependency (commdlg.h/comdlg32.lib)
			//to get wrong on a first pass. Paste an absolute path, or one relative to game root
			//(same as keyZScriptPath) - dofile() accepts either.
			static char scriptPathBuffer[512] = "";
			ImGui::SetNextItemWidth(-1);
			ImGui::InputText("##scriptPathInput", scriptPathBuffer, IM_ARRAYSIZE(scriptPathBuffer));
			ImGui::TextDisabled("Full path to a .lua file, or one relative to the game folder");

			USHORT selectedVKey = vkNameTable[addComboIndex].vKey;
			bool comboAvailable = IsComboAvailable(selectedVKey, addCtrl, addShift, addAlt);
			bool canAdd = scriptPathBuffer[0] != '\0' && comboAvailable;
			if (!canAdd) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Add Binding")) {
				AddBinding(selectedVKey, vkNameTable[addComboIndex].name, addCtrl, addShift, addAlt, scriptPathBuffer);
				scriptPathBuffer[0] = '\0';
			}
			if (!canAdd) {
				ImGui::EndDisabled();
			}
			if (scriptPathBuffer[0] != '\0' && !comboAvailable) {
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "That key + modifier combination is already in use");
			}

			ImGui::End();
		}//Draw

	}//namespace KeyBindMenu
}//namespace IHHook
