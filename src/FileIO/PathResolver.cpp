#include "PathResolver.h"

#if !defined(_WIN32)
#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#endif

namespace PathResolver
{

#if defined(_WIN32)

std::filesystem::path Resolve(const std::filesystem::path& path)
{
	return path;
}

std::filesystem::path ResolveForWriting(const std::filesystem::path& path)
{
	return path;
}

void SetOverlay(const std::filesystem::path& /*userDir*/, const std::filesystem::path& /*dataDir*/)
{
}

#else

namespace
{

std::string ToLower(std::string_view text)
{
	std::string lower(text);
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return lower;
}

/// \brief Each directory's entries keyed by lower-cased name, listed on first use and listed again
/// whenever the directory has changed since.
class DirectoryCache
{
public:
	/// \brief The entry of `directory` whose name matches `name` ignoring case, or "" if none does.
	std::string Find(const std::filesystem::path& directory, const std::string& name)
	{
		std::error_code ec;
		auto modified = std::filesystem::last_write_time(directory, ec);
		if (ec)
			return {};

		std::lock_guard<std::mutex> lock(_mutex);

		Listing& listing = _listings[directory.native()];
		if (!listing.valid || listing.modified != modified)
		{
			listing.entries.clear();
			for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
			{
				std::string entryName = entry.path().filename().native();
				listing.entries.emplace(ToLower(entryName), entryName);
			}

			listing.modified = modified;
			listing.valid    = !ec;
		}

		auto match = listing.entries.find(ToLower(name));
		return match != listing.entries.end() ? match->second : std::string();
	}

private:
	struct Listing
	{
		std::filesystem::file_time_type modified;
		std::unordered_map<std::string, std::string> entries;
		bool valid = false;
	};

	std::mutex _mutex;
	std::unordered_map<std::string, Listing> _listings;
};

DirectoryCache& Cache()
{
	static DirectoryCache cache;
	return cache;
}

struct Overlay
{
	std::filesystem::path userDir;
	std::string userDirLower; // with a trailing '/', for matching the game's lower-cased paths
	std::filesystem::path dataDir;
};

Overlay& TheOverlay()
{
	static Overlay overlay;
	return overlay;
}

} // namespace

std::filesystem::path ResolveForWriting(const std::filesystem::path& path)
{
	std::string spelt = path.native();
	std::replace(spelt.begin(), spelt.end(), '\\', '/');
	std::filesystem::path normalized(spelt);

	std::error_code ec;
	if (std::filesystem::exists(normalized, ec))
		return normalized;

	// Walk the components, fixing the case of those that don't exist as spelt.
	std::filesystem::path resolved = normalized.root_path();
	bool found                     = true;
	for (const std::filesystem::path& part : normalized.relative_path())
	{
		if (found)
		{
			std::filesystem::path candidate = resolved / part;
			if (part == "." || part == ".." || std::filesystem::exists(candidate, ec))
			{
				resolved = std::move(candidate);
				continue;
			}

			std::string actual = Cache().Find(
				resolved.empty() ? std::filesystem::path(".") : resolved, part.native());
			if (!actual.empty())
			{
				resolved /= actual;
				continue;
			}

			// Nothing matches: the rest doesn't exist (yet), so it keeps its spelling.
			found = false;
		}

		resolved /= part;
	}

	return resolved;
}

std::filesystem::path Resolve(const std::filesystem::path& path)
{
	std::filesystem::path resolved = ResolveForWriting(path);

	const Overlay& overlay         = TheOverlay();
	std::error_code ec;
	if (overlay.dataDir.empty() || std::filesystem::exists(resolved, ec))
		return resolved;

	// Where the path points inside the user directory: all of it when relative.
	std::string spelt = path.native();
	std::replace(spelt.begin(), spelt.end(), '\\', '/');
	std::string_view inside(spelt);
	if (!inside.empty() && inside.front() == '/')
	{
		if (!ToLower(spelt).starts_with(overlay.userDirLower))
			return resolved;
		inside.remove_prefix(overlay.userDirLower.size());
	}

	std::filesystem::path inData = ResolveForWriting(overlay.dataDir / inside);
	return std::filesystem::exists(inData, ec) ? inData : resolved;
}

void SetOverlay(const std::filesystem::path& userDir, const std::filesystem::path& dataDir)
{
	Overlay& overlay     = TheOverlay();
	overlay.userDir      = userDir;
	overlay.userDirLower = ToLower(userDir.native());
	if (!overlay.userDirLower.ends_with('/'))
		overlay.userDirLower += '/';
	overlay.dataDir = dataDir;
}

#endif

} // namespace PathResolver
