#include "bus/connection.hpp"
#include "desktop/fake_file_manager.hpp"
#include "desktop/file_manager.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace Klip;

namespace
{
Tests::CFakeFileManager gFakeFileManager;

class CFileManagerTest : public testing::Test
{
protected:

	static void SetUpTestSuite()
	{
		ASSERT_TRUE(Bus::gConnection.Initialize("klip-bus"));
		ASSERT_TRUE(gFakeFileManager.Initialize()) << "Cannot own org.freedesktop.FileManager1 on the test bus";
	}

	static void TearDownTestSuite()
	{
		gFakeFileManager.Terminate();
		Bus::gConnection.Terminate();
	}
};

class CNoFileManagerTest : public testing::Test
{
protected:

	static void SetUpTestSuite()
	{
		ASSERT_TRUE(Bus::gConnection.Initialize("klip-bus"));
	}

	static void TearDownTestSuite()
	{
		Bus::gConnection.Terminate();
	}
};
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST_F(CFileManagerTest, ShowItemsIsAskedForTheFile)
{
	EXPECT_TRUE(Desktop::ShowInFileManager("file:///tmp/a%20b.mp4"));
	EXPECT_EQ(gFakeFileManager.TakeShown(), std::vector<std::string>{ "file:///tmp/a%20b.mp4" });
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CNoFileManagerTest, MissingFileManagerIsReported)
{
	EXPECT_FALSE(Desktop::ShowInFileManager("file:///tmp/a.mp4"));
}
