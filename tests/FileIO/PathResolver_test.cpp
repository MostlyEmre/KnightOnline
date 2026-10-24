#include <gtest/gtest.h>
#include <FileIO/FileReader.h>
#include <FileIO/FileWriter.h>
#include <FileIO/PathResolver.h>
#include <atomic>
#include <ctime>
#include <string>

class PathResolverTest : public ::testing::Test
{
protected:
	std::filesystem::path _root;

	void SetUp() override
	{
		static std::atomic<uint32_t> s_testCounter = 0;
		static const time_t s_time                 = time(nullptr);

		_root                                      = std::filesystem::temp_directory_path()
				/ ("PathResolverTest_" + std::to_string(s_time) + "_"
					+ std::to_string(s_testCounter++));

		std::filesystem::create_directories(_root / "Item" / "Sub");

		FileWriter file;
		ASSERT_TRUE(file.Create(_root / "Item" / "Sub" / "Mixed_Case.dxt"));
		ASSERT_TRUE(file.Write("abc", 3));
		file.Close();
	}

	void TearDown() override
	{
		std::error_code ec;
		std::filesystem::remove_all(_root, ec);
	}

	// The path the way the game spells it: backslashes, all lower case.
	std::string GameSpelling(const std::string& relative) const
	{
		std::string path = _root.string() + "\\" + relative;
		for (char& ch : path)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return path;
	}
};

TEST_F(PathResolverTest, ExistingPathIsUnchanged)
{
	std::filesystem::path exact = _root / "Item" / "Sub" / "Mixed_Case.dxt";
	EXPECT_EQ(PathResolver::Resolve(exact), exact);
}

#if defined(_WIN32)
TEST_F(PathResolverTest, WindowsPathsPassThrough)
{
	std::filesystem::path spelt = GameSpelling("item\\sub\\mixed_case.dxt");
	EXPECT_EQ(PathResolver::Resolve(spelt), spelt);
}
#else
// These hold on case-sensitive file systems (where the resolver has to match the case itself) as
// well as on case-insensitive ones (where a mis-cased path already reaches the file).

TEST_F(PathResolverTest, GameSpellingFindsTheFile)
{
	FileReader file;
	ASSERT_TRUE(file.OpenExisting(GameSpelling("item\\sub\\mixed_case.dxt")));
	EXPECT_EQ(file.Size(), 3u);
}

TEST_F(PathResolverTest, ResolvesToTheFileOnDisk)
{
	std::filesystem::path resolved = PathResolver::Resolve(
		_root.string() + "/ITEM\\sub\\MIXED_case.DXT");
	EXPECT_TRUE(std::filesystem::equivalent(resolved, _root / "Item" / "Sub" / "Mixed_Case.dxt"));
}

TEST_F(PathResolverTest, NewFilesAreCreatedInTheResolvedDirectory)
{
	std::filesystem::path resolved = PathResolver::Resolve(_root.string() + "\\ITEM\\New_File.tmp");
	EXPECT_EQ(resolved.filename(), "New_File.tmp");
	EXPECT_TRUE(std::filesystem::equivalent(resolved.parent_path(), _root / "Item"));

	FileWriter file;
	ASSERT_TRUE(file.Create(_root.string() + "\\ITEM\\New_File.tmp"));
	file.Close();
	EXPECT_TRUE(std::filesystem::exists(_root / "Item" / "New_File.tmp"));
}

TEST_F(PathResolverTest, SeesFilesCreatedAfterTheDirectoryWasListed)
{
	// Look a file up mis-cased (listing Item/Sub where needed), add another, look that one up too.
	ASSERT_TRUE(std::filesystem::equivalent(
		PathResolver::Resolve(_root.string() + "/Item/Sub/MIXED_CASE.DXT"),
		_root / "Item" / "Sub" / "Mixed_Case.dxt"));

	FileWriter file;
	ASSERT_TRUE(file.Create(_root / "Item" / "Sub" / "Later.dxt"));
	file.Close();

	EXPECT_TRUE(
		std::filesystem::equivalent(PathResolver::Resolve(_root.string() + "/Item/Sub/LATER.DXT"),
			_root / "Item" / "Sub" / "Later.dxt"));
}

TEST_F(PathResolverTest, MissingPathKeepsItsSpelling)
{
	std::filesystem::path resolved = PathResolver::Resolve(_root.string() + "/Nope/Missing.dxt");
	EXPECT_EQ(resolved, _root / "Nope" / "Missing.dxt");
}
#endif
