#include <gtest/gtest.h>
#include <FileIO/MemoryReader.h>
#include <cstdio>
#include <vector>

class MemoryReaderTest : public ::testing::Test
{
protected:
	static constexpr int TEST_DATA_SIZE = 4'096;

	MemoryReader _file;

	void SetUp() override
	{
		std::vector<uint8_t> data(TEST_DATA_SIZE);
		for (size_t i = 0; i < data.size(); i++)
			data[i] = static_cast<uint8_t>(i % 256);

		ASSERT_TRUE(_file.Open(std::move(data), "Data/test.tbl"));
	}
};

TEST_F(MemoryReaderTest, Open_ReportsSizePathAndStartOffset)
{
	EXPECT_TRUE(_file.IsOpen());
	EXPECT_EQ(_file.Size(), TEST_DATA_SIZE);
	EXPECT_EQ(_file.Offset(), 0);
	EXPECT_EQ(_file.Path(), std::filesystem::path("Data/test.tbl"));
}

TEST_F(MemoryReaderTest, Close_ResetsState)
{
	EXPECT_TRUE(_file.Close());
	EXPECT_FALSE(_file.IsOpen());
	EXPECT_EQ(_file.Size(), 0);
	EXPECT_EQ(_file.Offset(), 0);
	EXPECT_FALSE(_file.Close());
}

TEST_F(MemoryReaderTest, Read_CopiesBytesAndAdvances)
{
	uint8_t buffer[16] {};
	size_t bytesRead = 0;
	ASSERT_TRUE(_file.Seek(250, SEEK_SET));
	EXPECT_TRUE(_file.Read(buffer, sizeof(buffer), &bytesRead));
	EXPECT_EQ(bytesRead, sizeof(buffer));
	EXPECT_EQ(_file.Offset(), 266);
	for (size_t i = 0; i < sizeof(buffer); i++)
		EXPECT_EQ(buffer[i], static_cast<uint8_t>((250 + i) % 256));
}

TEST_F(MemoryReaderTest, Read_PastEndCopiesWhatIsLeftThenFails)
{
	uint8_t buffer[16] {};
	size_t bytesRead = 0;
	ASSERT_TRUE(_file.Seek(-4, SEEK_END));
	EXPECT_TRUE(_file.Read(buffer, sizeof(buffer), &bytesRead));
	EXPECT_EQ(bytesRead, 4u);
	EXPECT_EQ(_file.Offset(), TEST_DATA_SIZE);

	EXPECT_FALSE(_file.Read(buffer, sizeof(buffer), &bytesRead));
	EXPECT_EQ(bytesRead, 0u);
}

TEST_F(MemoryReaderTest, Read_NothingRequestedSucceeds)
{
	uint8_t buffer[1] {};
	EXPECT_TRUE(_file.Read(buffer, 0));
	EXPECT_FALSE(_file.Read(nullptr, 1));
}

TEST_F(MemoryReaderTest, Seek_SucceedsOnlyWithinTheData)
{
	EXPECT_TRUE(_file.Seek(TEST_DATA_SIZE, SEEK_SET));
	EXPECT_FALSE(_file.Seek(TEST_DATA_SIZE + 1, SEEK_SET));
	EXPECT_FALSE(_file.Seek(-1, SEEK_SET));
	EXPECT_EQ(_file.Offset(), TEST_DATA_SIZE);

	EXPECT_TRUE(_file.Seek(-10, SEEK_CUR));
	EXPECT_EQ(_file.Offset(), TEST_DATA_SIZE - 10);
	EXPECT_FALSE(_file.Seek(11, SEEK_CUR));
	EXPECT_FALSE(_file.Seek(0, 99));
}

TEST_F(MemoryReaderTest, WritingIsNotSupported)
{
	uint8_t byte = 0;
	EXPECT_FALSE(_file.Write(&byte, 1));
	EXPECT_FALSE(_file.Create("x"));
	EXPECT_FALSE(_file.OpenExisting("x"));
}
