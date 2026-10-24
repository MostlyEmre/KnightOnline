#include "../Application.h"
#include "../Shell.h"
#include "Cocoa/CocoaApp.h"
#include "MacState.h"

#include <FileIO/PathResolver.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <poll.h>
#include <spawn.h>
#include <unistd.h>

#include <spdlog/fmt/bundled/format.h>

extern char** environ;

// KnightOnline.app runs the game from a folder in Application Support, laid over the game data in
// the bundle (Contents/Resources/Client), and starts the bundled servers through a supervisor
// helper (Contents/Helpers/ko-servers) that stops them again when the game exits. Run outside a
// bundle (from a build folder), the game uses its working directory as it always has.

namespace Platform
{

namespace macOS
{

std::filesystem::path AppSupportDirectory()
{
	const char* home = std::getenv("HOME");
	return std::filesystem::path(home != nullptr ? home : ".") / "Library" / "Application Support"
		   / "KnightOnline";
}

} // namespace macOS

namespace
{

constexpr int ServerStartTimeoutMs = 90'000;

void Fail(const std::string& szMessage)
{
	fmt::print(stderr, "KnightOnline: {}\n", szMessage);
	ShowMessageBox(nullptr, szMessage.c_str(), "Knight Online");
}

// Starts the supervisor and waits until it reports the servers ready ("ready") or failed
// ("failed: <reason>").
bool StartServers(const std::filesystem::path& helpers, const std::filesystem::path& serverData,
	const std::filesystem::path& userDir)
{
	std::filesystem::path supervisor = helpers / "ko-servers";
	std::error_code ec;
	if (!std::filesystem::exists(supervisor, ec))
	{
		Fail("The game's servers are missing from the app (" + supervisor.string() + ").");
		return false;
	}

	std::array<int, 2> fds = { -1, -1 };
	if (pipe(fds.data()) != 0)
	{
		Fail("Couldn't start the game's servers (pipe failed).");
		return false;
	}

	std::vector<std::string> args = { supervisor.string(), "--client-pid", std::to_string(getpid()),
		"--user-dir", userDir.string(), "--data-dir", serverData.string(), "--bin-dir",
		helpers.string(), "--ready-fd", "3" };
	std::vector<char*> argv;
	argv.reserve(args.size() + 1);
	for (std::string& arg : args)
		argv.push_back(arg.data());
	argv.push_back(nullptr);

	// The supervisor gets stdin/stdout/stderr and the pipe's write end as fd 3, nothing else of
	// ours (POSIX_SPAWN_CLOEXEC_DEFAULT closes every descriptor the actions don't set up).
	posix_spawn_file_actions_t actions {};
	posix_spawn_file_actions_init(&actions);
	for (int fd : { STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO })
		posix_spawn_file_actions_addinherit_np(&actions, fd);
	posix_spawn_file_actions_adddup2(&actions, fds[1], 3);

	posix_spawnattr_t attributes {};
	posix_spawnattr_init(&attributes);
	posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);

	pid_t pid = 0;
	int error = posix_spawn(&pid, supervisor.c_str(), &actions, &attributes, argv.data(), environ);
	posix_spawnattr_destroy(&attributes);
	posix_spawn_file_actions_destroy(&actions);
	close(fds[1]);
	if (error != 0)
	{
		close(fds[0]);
		Fail("Couldn't start the game's servers (" + std::string(strerror(error)) + ").");
		return false;
	}

	// Read the supervisor's report line.
	std::string line;
	auto deadline = std::chrono::steady_clock::now()
					+ std::chrono::milliseconds(ServerStartTimeoutMs);
	while (line.find('\n') == std::string::npos)
	{
		auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now());
		pollfd pfd { fds[0], POLLIN, 0 };
		if (remaining.count() <= 0 || poll(&pfd, 1, static_cast<int>(remaining.count())) <= 0)
		{
			line = "failed: the servers took too long to start";
			break;
		}

		std::array<char, 256> buffer {};
		ssize_t n = read(fds[0], buffer.data(), buffer.size());
		if (n <= 0)
		{
			line = "failed: the server supervisor exited";
			break;
		}
		line.append(buffer.data(), static_cast<size_t>(n));
	}
	close(fds[0]);

	line = line.substr(0, line.find('\n'));
	if (line == "ready")
		return true;

	Fail("The game's servers didn't start: " + line.substr(line.find(':') + 1)
		 + "\n\nLogs: " + (userDir / "Logs").string());
	return false;
}

} // namespace

bool InitializeProcess()
{
	std::filesystem::path resources  = macOS::Cocoa::ResourceDirectory();
	std::filesystem::path clientData = resources / "Client";
	std::error_code ec;
	if (resources.empty() || !std::filesystem::is_directory(clientData, ec))
		return true; // not the app bundle: run from the working directory

	std::filesystem::path userDir = macOS::AppSupportDirectory();
	std::filesystem::create_directories(userDir, ec);
	std::filesystem::current_path(userDir, ec);
	if (ec)
	{
		Fail("Couldn't use " + userDir.string() + ": " + ec.message());
		return false;
	}

	PathResolver::SetOverlay(userDir, clientData);
	return StartServers(resources.parent_path() / "Helpers", resources / "Server", userDir);
}

} // namespace Platform
