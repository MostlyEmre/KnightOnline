// ko-servers: starts KnightOnline.app's servers for the game and stops them when the game exits.
//
// usage: ko-servers --client-pid <pid> --user-dir <dir> --data-dir <dir> --bin-dir <dir>
//                   [--ready-fd <fd>]
//
// - Waits for an earlier ko-servers that is still stopping its servers (a lock in the user dir).
// - On first run, copies the seed database to <user-dir>/Server/KN_online.sqlite.
// - Starts AIServer, Ebenezer, Aujard, VersionManager and ItemManager in that order, each in
//   <user-dir>/Server, listening on 127.0.0.1 only, and waits for each to log that it's ready.
//   Their output goes to <user-dir>/Logs/<name>.log.
// - Writes "ready", or "failed: <reason>", and a newline to --ready-fd.
// - When the game exits, however it exits, or on SIGTERM/SIGINT/SIGHUP, stops the servers: the
//   login and game servers first, so that the database writer (Aujard) still saves what the game
//   server hands it, then the rest.

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/event.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace
{

struct Server
{
	Server(std::string name_, std::string readyText_, std::vector<std::string> extraArgs_) :
		name(std::move(name_)), readyText(std::move(readyText_)), extraArgs(std::move(extraArgs_))
	{
	}

	std::string name;
	std::string readyText; // logged once the server is ready for the next one
	std::vector<std::string> extraArgs;
	pid_t pid = 0;
	fs::path log;
};

FILE* s_log = stderr;

void Log(const std::string& message)
{
	std::time_t now = std::time(nullptr);
	std::array<char, 32> stamp {};
	std::strftime(stamp.data(), stamp.size(), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
	std::fputs(("[" + std::string(stamp.data()) + "] " + message + "\n").c_str(), s_log);
	std::fflush(s_log);
}

void Report(int fd, const std::string& line)
{
	if (fd < 0)
		return;

	std::string text = line + "\n";
	(void) write(fd, text.data(), text.size());
	close(fd);
}

bool Alive(pid_t pid)
{
	if (pid <= 0)
		return false;

	int status = 0;
	return waitpid(pid, &status, WNOHANG) == 0;
}

std::string ReadFile(const fs::path& path)
{
	std::ifstream file(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

// Starts exe in the current directory with its output going to logPath.
pid_t Spawn(const fs::path& exe, const std::vector<std::string>& args, const fs::path& logPath,
	const std::vector<std::string>& extraEnv)
{
	std::vector<std::string> argStrings = { exe.string() };
	argStrings.insert(argStrings.end(), args.begin(), args.end());
	std::vector<char*> argv;
	argv.reserve(argStrings.size() + 1);
	for (std::string& arg : argStrings)
		argv.push_back(arg.data());
	argv.push_back(nullptr);

	std::vector<std::string> envStrings = extraEnv;
	for (char** env = environ; *env != nullptr;
		env++) // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		envStrings.emplace_back(*env);
	std::vector<char*> envp;
	envp.reserve(envStrings.size() + 1);
	for (std::string& env : envStrings)
		envp.push_back(env.data());
	envp.push_back(nullptr);

	posix_spawn_file_actions_t actions {};
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_addopen(
		&actions, STDOUT_FILENO, logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

	// Only the descriptors set up above reach the server (not the lock or the game's pipe).
	posix_spawnattr_t attributes {};
	posix_spawnattr_init(&attributes);
	posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);

	pid_t pid = 0;
	int error = posix_spawn(&pid, exe.c_str(), &actions, &attributes, argv.data(), envp.data());
	posix_spawnattr_destroy(&attributes);
	posix_spawn_file_actions_destroy(&actions);
	if (error != 0)
	{
		Log("couldn't start " + exe.string() + ": " + std::strerror(error));
		return 0;
	}
	return pid;
}

// Waits for every given server to exit, up to the timeout; returns whether they all did.
bool WaitForExit(const std::vector<Server*>& servers, std::chrono::milliseconds timeout)
{
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline)
	{
		bool anyAlive = false;
		for (Server* server : servers)
			anyAlive |= Alive(server->pid);
		if (!anyAlive)
			return true;
		std::this_thread::sleep_for(100ms);
	}
	return false;
}

void Stop(std::vector<Server>& servers)
{
	std::map<std::string, Server*> byName;
	for (Server& server : servers)
		byName[server.name] = &server;

	auto stopGroup =
		[&](std::initializer_list<const char*> names, std::chrono::milliseconds timeout)
	{
		std::vector<Server*> group;
		for (const char* name : names)
		{
			Server* server = byName[name];
			if (server != nullptr && Alive(server->pid))
			{
				Log(std::string("stopping ") + name + " (pid " + std::to_string(server->pid) + ")");
				kill(server->pid, SIGINT);
				group.push_back(server);
			}
		}
		if (!WaitForExit(group, timeout))
		{
			for (Server* server : group)
			{
				if (Alive(server->pid))
				{
					Log(server->name + " didn't stop in time; killing it");
					kill(server->pid, SIGKILL);
				}
			}
			WaitForExit(group, 2s);
		}
	};

	stopGroup({ "VersionManager", "Ebenezer" }, 20s);
	stopGroup({ "Aujard", "ItemManager", "AIServer" }, 15s);
	Log("all servers stopped");
}

int Run(int argc, char** argv)
{
	std::map<std::string, std::string> opts;
	for (int i = 1; i + 1 < argc; i += 2)
		opts[argv[i]] = argv[i + 1]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

	int readyFd = opts.contains("--ready-fd") ? std::atoi(opts["--ready-fd"].c_str()) : -1;
	if (!opts.contains("--client-pid") || !opts.contains("--user-dir")
		|| !opts.contains("--data-dir") || !opts.contains("--bin-dir"))
	{
		std::fputs(
			"usage: ko-servers --client-pid <pid> --user-dir <dir> --data-dir <dir> --bin-dir "
			"<dir> [--ready-fd <fd>]\n",
			stderr);
		Report(readyFd, "failed: bad arguments");
		return 2;
	}

	// Don't get the signals meant for the game's process group; the game's exit is watched below.
	setsid();

	const pid_t clientPid = std::atoi(opts["--client-pid"].c_str());
	const fs::path userDir(opts["--user-dir"]);
	const fs::path dataDir(opts["--data-dir"]);
	const fs::path binDir(opts["--bin-dir"]);
	const fs::path serverDir = userDir / "Server";
	const fs::path logDir    = userDir / "Logs";

	std::error_code ec;
	fs::create_directories(serverDir, ec);
	fs::create_directories(logDir, ec);
	s_log = std::fopen((logDir / "ko-servers.log").c_str(), "a");
	if (s_log == nullptr)
		s_log = stderr;
	Log("starting for game pid " + std::to_string(clientPid));

	// One set of servers at a time: wait for an earlier run that is still stopping them.
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): open() is variadic
	int lockFd        = open((serverDir / "ko-servers.lock").c_str(), O_RDWR | O_CREAT, 0644);
	auto lockDeadline = std::chrono::steady_clock::now() + 40s;
	while (lockFd >= 0 && flock(lockFd, LOCK_EX | LOCK_NB) != 0)
	{
		if (std::chrono::steady_clock::now() > lockDeadline)
		{
			Log("another ko-servers still holds the lock");
			Report(readyFd, "failed: the servers from the last run are still shutting down");
			return 1;
		}
		std::this_thread::sleep_for(200ms);
	}

	// The database: the seed on first run, then the player's own.
	const fs::path database = serverDir / "KN_online.sqlite";
	if (!fs::exists(database, ec))
	{
		fs::path temporary  = database;
		temporary          += ".new";
		fs::copy_file(
			dataDir / "KN_online.sqlite", temporary, fs::copy_options::overwrite_existing, ec);
		if (!ec)
			fs::rename(temporary, database, ec);
		if (ec)
		{
			Log("couldn't create the database: " + ec.message());
			Report(readyFd, "failed: couldn't create the database (" + ec.message() + ")");
			return 1;
		}
		Log("created " + database.string() + " from the seed");
	}

	// The servers read and write their config (and logs) in the current directory.
	fs::current_path(serverDir, ec);
	if (ec)
	{
		Report(readyFd, "failed: couldn't use " + serverDir.string() + " (" + ec.message() + ")");
		return 1;
	}

	const std::string mapDir    = (dataDir / "MAP").string();
	const std::string questsDir = (dataDir / "QUESTS").string();
	std::vector<Server> servers = {
		// AIServer paces the NPC list it hands Ebenezer at startup at 50 ms a batch by default, which
		// keeps players out for ~15 s after the app opens; over loopback it needn't wait that long.
		{ "AIServer", "Listening on",
			{ "--map-dir", mapDir, "--event-dir", mapDir, "--npc-info-interval", "2" } },
		{ "Ebenezer", "initialized port=15001",
			{ "--map-dir", mapDir, "--quests-dir", questsDir } },
		{ "Aujard", "server started", {} },
		{ "VersionManager", "Listening on", {} },
		{ "ItemManager", "server started", {} },
	};
	const std::vector<std::string> env = { "OPENKO_SQLITE_DB=" + database.string() };

	for (Server& server : servers)
	{
		server.log                    = logDir / (server.name + ".log");
		std::vector<std::string> args = { "--headless", "--listen-address", "127.0.0.1" };
		args.insert(args.end(), server.extraArgs.begin(), server.extraArgs.end());

		server.pid    = Spawn(binDir / server.name, args, server.log, env);
		bool ready    = false;
		auto deadline = std::chrono::steady_clock::now() + 60s;
		while (server.pid > 0 && std::chrono::steady_clock::now() < deadline)
		{
			if (ReadFile(server.log).find(server.readyText) != std::string::npos)
			{
				ready = true;
				break;
			}
			if (!Alive(server.pid))
				break;
			std::this_thread::sleep_for(100ms);
		}

		if (!ready)
		{
			Log(server.name + " didn't become ready (see " + server.log.string() + ")");
			Report(readyFd, "failed: " + server.name + " didn't start");
			Stop(servers);
			return 1;
		}
		Log(server.name + " ready (pid " + std::to_string(server.pid) + ")");
	}

	Report(readyFd, "ready");

	// Wait for the game to exit or for a signal to stop.
	int kq = kqueue();
	std::array<struct kevent, 4> changes {};
	EV_SET(&changes[0], clientPid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
	EV_SET(&changes[1], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	EV_SET(&changes[2], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	EV_SET(&changes[3], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	std::signal(SIGTERM, SIG_IGN);
	std::signal(SIGINT, SIG_IGN);
	std::signal(SIGHUP, SIG_IGN);

	bool clientExited = false;
	if (kevent(kq, changes.data(), 1, nullptr, 0, nullptr) != 0 && errno == ESRCH)
		clientExited = true; // already gone
	kevent(kq, &changes[1], 3, nullptr, 0, nullptr);

	struct kevent event {};
	if (!clientExited && kevent(kq, nullptr, 0, &event, 1, nullptr) == 1)
		clientExited = event.filter == EVFILT_PROC;

	if (clientExited)
	{
		// The game server notices the game's connection close and has the player saved.
		Log("the game exited; letting the servers save the player");
		std::this_thread::sleep_for(3s);
	}
	else
	{
		Log("asked to stop");
	}

	Stop(servers);
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	try
	{
		return Run(argc, argv);
	}
	catch (const std::exception& ex)
	{
		Log(std::string("stopped by an error: ") + ex.what());
		return 1;
	}
	catch (...)
	{
		return 1;
	}
}
