#include "InputScript.h"
#include "../Compat/DInputKeys.h"
#include "Cocoa/CocoaApp.h"
#include "Direct3D9_macOS.h"
#include "MacState.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <spdlog/fmt/bundled/format.h>

namespace Platform::macOS
{

// Defined in TextInput_macOS.cpp.
void TypeScriptedText(const std::string& szUtf8);
void SendScriptedTextCommand(const std::string& szCommand);

namespace InputScript
{

namespace
{

struct Command
{
	std::string verb;
	std::vector<std::string> args;
	int line = 0;
};

using Clock = std::chrono::steady_clock;

// Script times are in 60ths of a second of real time, so a script behaves the same at any frame
// rate (the game runs well above 60 frames per second while its window is covered).
constexpr std::chrono::microseconds Tick60 { 16'667 };

struct State
{
	bool loaded = false;
	bool active = false;
	std::vector<Command> commands;
	size_t next    = 0;
	int waitFrames = 0;          // frames to wait at least
	Clock::time_point waitUntil; // and until when
	int cursorX = 0, cursorY = 0;
	uint32_t buttons = 0;
	int buttonFrames = 0;
	std::array<int, 256> keyFrames {};              // frames each key stays down at least
	std::array<Clock::time_point, 256> keyUntil {}; // and until when
};

State& S()
{
	static State state;
	return state;
}

int ScanCodeOf(std::string name)
{
	static const std::map<std::string, int> names = {
		{ "ESCAPE", DIK_ESCAPE },
		{ "RETURN", DIK_RETURN },
		{ "ENTER", DIK_RETURN },
		{ "TAB", DIK_TAB },
		{ "SPACE", DIK_SPACE },
		{ "BACK", DIK_BACK },
		{ "DELETE", DIK_DELETE },
		{ "UP", DIK_UP },
		{ "DOWN", DIK_DOWN },
		{ "LEFT", DIK_LEFT },
		{ "RIGHT", DIK_RIGHT },
		{ "HOME", DIK_HOME },
		{ "END", DIK_END },
		{ "PRIOR", DIK_PRIOR },
		{ "NEXT", DIK_NEXT },
		{ "LSHIFT", DIK_LSHIFT },
		{ "LCONTROL", DIK_LCONTROL },
		{ "LMENU", DIK_LMENU },
		{ "F1", DIK_F1 },
		{ "F2", DIK_F2 },
		{ "F3", DIK_F3 },
		{ "F4", DIK_F4 },
		{ "F5", DIK_F5 },
		{ "F6", DIK_F6 },
		{ "F7", DIK_F7 },
		{ "F8", DIK_F8 },
		{ "F9", DIK_F9 },
		{ "F10", DIK_F10 },
		{ "F11", DIK_F11 },
		{ "F12", DIK_F12 },
		{ "A", DIK_A },
		{ "B", DIK_B },
		{ "C", DIK_C },
		{ "D", DIK_D },
		{ "E", DIK_E },
		{ "F", DIK_F },
		{ "G", DIK_G },
		{ "H", DIK_H },
		{ "I", DIK_I },
		{ "J", DIK_J },
		{ "K", DIK_K },
		{ "L", DIK_L },
		{ "M", DIK_M },
		{ "N", DIK_N },
		{ "O", DIK_O },
		{ "P", DIK_P },
		{ "Q", DIK_Q },
		{ "R", DIK_R },
		{ "S", DIK_S },
		{ "T", DIK_T },
		{ "U", DIK_U },
		{ "V", DIK_V },
		{ "W", DIK_W },
		{ "X", DIK_X },
		{ "Y", DIK_Y },
		{ "Z", DIK_Z },
		{ "0", DIK_0 },
		{ "1", DIK_1 },
		{ "2", DIK_2 },
		{ "3", DIK_3 },
		{ "4", DIK_4 },
		{ "5", DIK_5 },
		{ "6", DIK_6 },
		{ "7", DIK_7 },
		{ "8", DIK_8 },
		{ "9", DIK_9 },
	};
	for (char& ch : name)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	auto it = names.find(name);
	return it != names.end() ? it->second : -1;
}

void Load()
{
	State& s         = S();
	s.loaded         = true;

	const char* path = std::getenv("OPENKO_INPUT_SCRIPT");
	if (path == nullptr)
		return;

	std::ifstream file(path);
	if (!file)
	{
		fmt::print(stderr, "InputScript: can't open {}\n", path);
		return;
	}

	std::string line;
	int number = 0;
	while (std::getline(file, line))
	{
		number++;
		if (size_t hash = line.find('#'); hash != std::string::npos)
			line.resize(hash);

		std::istringstream words(line);
		Command command;
		command.line = number;
		if (!(words >> command.verb))
			continue;

		if (command.verb == "text" || command.verb == "snapshot")
		{
			// The rest of the line, as written.
			std::string rest;
			std::getline(words, rest);
			rest.erase(0, rest.find_first_not_of(' '));
			command.args.push_back(rest);
		}
		else
		{
			for (std::string word; words >> word;)
				command.args.push_back(word);
		}

		s.commands.push_back(std::move(command));
	}

	s.active = true;
	fmt::print(stderr, "InputScript: playing {} commands from {}\n", s.commands.size(), path);
}

int IntArg(const Command& command, size_t index, int fallback = 0)
{
	return index < command.args.size() ? std::atoi(command.args[index].c_str()) : fallback;
}

// Runs one command; returns false when the script should stop running commands this frame.
bool Run(const Command& command)
{
	State& s = S();
	fmt::print(stderr, "InputScript: line {}: {}\n", command.line, command.verb);

	if (command.verb == "wait")
	{
		s.waitFrames = 1;
		s.waitUntil  = Clock::now() + IntArg(command, 0, 1) * Tick60;
		return false;
	}

	if (command.verb == "move")
	{
		s.cursorX = IntArg(command, 0);
		s.cursorY = IntArg(command, 1);
		return true;
	}

	if (command.verb == "click")
	{
		s.cursorX      = IntArg(command, 0);
		s.cursorY      = IntArg(command, 1);
		bool right     = command.args.size() > 2 && command.args[2] == "right";
		s.buttons      = right ? 0x2 : 0x1;
		s.buttonFrames = 2;
		s.waitFrames   = 3; // let the game see the press and the release
		s.waitUntil    = Clock::now() + 3 * Tick60;
		return false;
	}

	if (command.verb == "down" || command.verb == "up")
	{
		const bool down = command.verb == "down";
		s.buttons       = down ? 0x1 : 0;
		s.buttonFrames  = down ? std::numeric_limits<int>::max() : 0;
		s.waitFrames    = 3;
		s.waitUntil     = Clock::now() + 3 * Tick60;
		return false;
	}

	if (command.verb == "key" || command.verb == "hold")
	{
		int dik = command.args.empty() ? -1 : ScanCodeOf(command.args[0]);
		if (dik < 0)
		{
			fmt::print(stderr, "InputScript: line {}: unknown key\n", command.line);
			return true;
		}

		int ticks                             = command.verb == "hold" ? IntArg(command, 1, 2) : 2;
		s.keyFrames[static_cast<size_t>(dik)] = 2;
		s.keyUntil[static_cast<size_t>(dik)]  = Clock::now() + ticks * Tick60;
		if (dik == DIK_RETURN && command.verb == "key")
			SendScriptedTextCommand("insertNewline:");
		s.waitFrames = 3;
		s.waitUntil  = Clock::now() + (ticks + 1) * Tick60;
		return false;
	}

	if (command.verb == "text")
	{
		TypeScriptedText(command.args.empty() ? std::string() : command.args[0]);
		return true;
	}

	if (command.verb == "snapshot")
	{
		// The next command waits for the frame, or its input would be in it.
		RequestSnapshot(command.args.empty() ? std::string("snapshot.png") : command.args[0]);
		s.waitFrames = 1;
		s.waitUntil  = Clock::now();
		return false;
	}

	if (command.verb == "quit")
	{
		RequestQuit(0);
		return false;
	}

	if (command.verb == "options")
	{
		// The game's Options window, as the app menu's Settings... opens it.
		if (Handlers().OnShowOptions)
			Handlers().OnShowOptions();
		return true;
	}

	if (command.verb == "genie")
	{
		// Genie's window, as G opens it in the game.
		if (Handlers().OnShowGenie)
			Handlers().OnShowGenie();
		return true;
	}

	if (command.verb == "handoff")
	{
		// Real input from here on, for whoever is at the Mac.
		s.active       = false;
		s.buttons      = 0;
		s.buttonFrames = 0;
		s.keyFrames.fill(0);
		s.keyUntil.fill({});
		Cocoa::ActivateGameWindow();
		return false;
	}

	fmt::print(stderr, "InputScript: line {}: unknown command '{}'\n", command.line, command.verb);
	return true;
}

} // namespace

bool IsActive()
{
	if (!S().loaded)
		Load();
	return S().active;
}

void Tick()
{
	if (!IsActive())
		return;

	State& s = S();
	if (s.buttonFrames > 0)
	{
		s.buttonFrames--;
		if (s.buttonFrames == 0)
			s.buttons = 0;
	}
	for (int& frames : s.keyFrames)
		if (frames > 0)
			frames--;

	if (s.waitFrames > 0)
		s.waitFrames--;
	if (s.waitFrames > 0 || Clock::now() < s.waitUntil)
		return;

	while (s.next < s.commands.size())
	{
		if (!Run(s.commands[s.next++]))
			break;
	}
}

void CursorClient(int& x, int& y)
{
	x = S().cursorX;
	y = S().cursorY;
}

uint32_t MouseButtons()
{
	return S().buttons;
}

bool IsKeyDown(int iScanCode)
{
	if (iScanCode < 0 || iScanCode >= 256)
		return false;

	auto key = static_cast<size_t>(iScanCode);
	return S().keyFrames[key] > 0 || Clock::now() < S().keyUntil[key];
}

} // namespace InputScript

} // namespace Platform::macOS
