#include "KeyBindMenu.h"
#include "RawInput.h"
#include "IHMenu.h"
#include "Util.h"
#include "spdlog/spdlog.h"
#include "imgui/imgui.h"

#include <fstream>

namespace IHHook {
	namespace KeyBindMenu {

		std::vector<KeyBind> bindings;

		//tex: persisted separately from ihhook_config.lua rather than shoehorned into it -
		//ihhook_config.lua's parser (see IHHook.cpp ParseConfig) is a fragile fixed-shape
		//key=value format, not suited to a variable-length list that also needs to be
		//re-written from the GUI every time a binding is added/removed. This is a much
		//simpler format we fully own both the reader and writer for.
		const std::string bindsFileName = "ihhook_keybinds.txt";

		//tex: not exhaustive, but covers the common cases for a dropdown. name is what's shown
		//in the combo box and what's persisted to disk (so the file stays human-readable/editable).
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

		//tex: e.g. "Shift+Alt+Z", "Alt+F6", "Z" - what's shown in the bindings list
		std::string CombinedDisplayName(const KeyBind& bind) {
			std::string result;
			if (bind.needShift) result += "Shift+";
			if (bind.needAlt) result += "Alt+";
			result += bind.keyName;
			return result;
		}//CombinedDisplayName

		//tex: the key that opens/closes this menu itself - defaults from config.keyBindMenuToggleKey
		//(see IHHook.cpp ParseConfig) but can be live-remapped from within the menu, at which point
		//this persisted value takes precedence on next launch (see LoadBindings/SaveBindings).
		//GOTCHA: unlike custom bindings, the menu-toggle key intentionally does NOT support Shift/Alt
		//modifiers - keeping it a single plain key avoids complicating the one binding that must
		//always be reachable to fix/undo everything else.
		USHORT menuToggleVKey = VK_F4;
		RawInput::ActionHandle menuToggleHandle = 0;
		bool menuOpen = false;

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

		//tex: reserved regardless of Shift/Alt - these built-in actions (ToggleCursor, ToggleMenu,
		//MenuOff, RunKeyZScript, and this menu's own toggle key) don't check modifier state
		//themselves, so e.g. Shift+F2 would still fire the plain ToggleCursor action alongside
		//whatever a new Shift+F2 custom binding did. Simplest to just keep these fully reserved.
		bool IsReservedVKey(USHORT vKey) {
			return vKey == VK_F2 || vKey == VK_F3 || vKey == VK_ESCAPE || vKey == 'Z' || vKey == menuToggleVKey;
		}//IsReservedVKey

		//tex: true if this exact key+modifier combination is free to bind. Different modifier
		//combos on the SAME physical vKey (e.g. "F6" and "Shift+F6") are different, non-conflicting
		//bindings - that's the whole point of adding modifier support - so this checks the full
		//(vKey, needShift, needAlt) tuple against existing bindings, not vKey alone.
		bool IsComboAvailable(USHORT vKey, bool needShift, bool needAlt) {
			if (IsReservedVKey(vKey)) {
				return false;
			}
			for (const KeyBind& bind : bindings) {
				if (bind.vKey == vKey && bind.needShift == needShift && bind.needAlt == needAlt) {
					return false;
				}
			}
			return true;
		}//IsComboAvailable

		//tex: same DoScript IPC round-trip RunKeyZScript (RawInput.cpp) uses - capturing scriptPath
		//(and the modifier requirement) per-lambda is exactly what widening ButtonAction to
		//std::function (see RawInput.h) enables. Registers unconditionally against the base vKey -
		//the modifier check happens inside the lambda itself, since RawInput dispatches by vKey
		//only and knows nothing about Shift/Alt requirements.
		RawInput::ActionHandle RegisterBindingAction(const KeyBind& bind) {
			std::string scriptPath = bind.scriptPath; //tex: copied - captured by value below
			bool needShift = bind.needShift;
			bool needAlt = bind.needAlt;
			return RawInput::RegisterAction(bind.vKey, [scriptPath, needShift, needAlt](RawInput::BUTTONEVENT buttonEvent) {
				if (buttonEvent != RawInput::BUTTONEVENT::ONDOWN) {
					return;
				}
				if (RawInput::IsKeyDown(VK_SHIFT) != needShift || RawInput::IsKeyDown(VK_MENU) != needAlt) {
					return;//tex: e.g. this is the plain "Z" binding but Shift is currently held - not a match
				}
				spdlog::debug("KeyBindMenu: queuing dofile for {}", scriptPath);
				IHMenu::QueueMessageIn("DoScript|dofile([[" + scriptPath + "]])");
			});
		}//RegisterBindingAction

		void SaveBindings() {
			std::ofstream outFile(bindsFileName);
			if (!outFile) {
				spdlog::warn("KeyBindMenu::SaveBindings: couldn't open {} for writing", bindsFileName);
				return;
			}
			outFile << "MENUKEY|" << NameForVKey(menuToggleVKey) << "\n";
			for (const KeyBind& bind : bindings) {
				outFile << "BIND|" << bind.keyName << "|" << (bind.needShift ? "1" : "0") << "|" << (bind.needAlt ? "1" : "0") << "|" << bind.scriptPath << "\n";
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
				else if (parts[0] == "BIND" && parts.size() >= 5) {
					std::string keyName = trim(parts[1]);
					bool needShift = trim(parts[2]) == "1";
					bool needAlt = trim(parts[3]) == "1";
					std::string scriptPath = trim(parts[4]);
					int vKey = VKeyForName(keyName);
					if (vKey == -1) {
						spdlog::warn("KeyBindMenu::LoadBindings: unknown key name '{}', skipping binding", keyName);
						continue;
					}
					//tex: handle assigned once registered - see Init(), which registers everything
					//loaded here right after this function returns.
					bindings.push_back(KeyBind{ (USHORT)vKey, needShift, needAlt, keyName, scriptPath, 0 });
				}
				else if (parts[0] == "BIND") {
					spdlog::warn("KeyBindMenu::LoadBindings: skipping old-format/malformed BIND line: {}", line);
				}
			}//while line
			spdlog::debug("KeyBindMenu::LoadBindings: loaded {} binding(s) from {}", bindings.size(), bindsFileName);
		}//LoadBindings

		void AddBinding(USHORT vKey, const std::string& keyName, bool needShift, bool needAlt, const std::string& scriptPath) {
			KeyBind bind{ vKey, needShift, needAlt, keyName, scriptPath, 0 };
			bindings.push_back(bind);
			bindings.back().handle = RegisterBindingAction(bindings.back());
			SaveBindings();
		}//AddBinding

		void RemoveBinding(int index) {
			if (index < 0 || index >= (int)bindings.size()) {
				return;
			}
			RawInput::UnRegisterAction(bindings[index].vKey, bindings[index].handle);//tex: removes just this one binding's action
			bindings.erase(bindings.begin() + index);
			SaveBindings();
		}//RemoveBinding

		void RemoveAllBindings() {
			for (const KeyBind& bind : bindings) {
				RawInput::UnRegisterAction(bind.vKey, bind.handle);
			}
			bindings.clear();
			SaveBindings();
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
			for (KeyBind& bind : bindings) {
				bind.handle = RegisterBindingAction(bind);
			}
			RegisterMenuToggleKey(menuToggleVKey);
		}//Init

		void Draw(bool* p_open) {
			ImGui::SetNextWindowSize(ImVec2(440, 440), ImGuiCond_::ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("IHHook Key Bindings", p_open)) {
				ImGui::End();
				return;
			}

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
				if (newVKey == menuToggleVKey || IsComboAvailable(newVKey, false, false)) {
					RawInput::UnRegisterAction(menuToggleVKey, menuToggleHandle);
					RegisterMenuToggleKey(newVKey);
					SaveBindings();
				}
				else {
					spdlog::warn("KeyBindMenu: can't remap menu-toggle key to {} - already in use", vkNameTable[menuKeyComboIndex].name);
				}
			}

			ImGui::Separator();
			ImGui::TextWrapped("Custom bindings - press a key (+ Shift/Alt if set) in-game to dofile() the matching script.");
			ImGui::Spacing();

			//tex: existing bindings list, each with its own remove button, plus a bulk "Remove All"
			int removeIndex = -1;
			ImGui::BeginChild("BindingsList", ImVec2(0, 200), true);
			for (int i = 0; i < (int)bindings.size(); i++) {
				ImGui::PushID(i);
				ImGui::Text("%s", CombinedDisplayName(bindings[i]).c_str());
				ImGui::SameLine(110);
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
			static bool addShift = false;
			static bool addAlt = false;
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
			bool comboAvailable = IsComboAvailable(selectedVKey, addShift, addAlt);
			bool canAdd = scriptPathBuffer[0] != '\0' && comboAvailable;
			if (!canAdd) {
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Add Binding")) {
				AddBinding(selectedVKey, vkNameTable[addComboIndex].name, addShift, addAlt, scriptPathBuffer);
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
