#include "config/config_test.hpp"
#include "config/xdg_paths.hpp"

#include <gtest/gtest.h>

using namespace Klip;

namespace
{
using CXdgPathsTest = Tests::CConfigTest;
} // namespace

//////////////////////////////////////////////////////////////////////////
TEST_F(CXdgPathsTest, VideosFallBackToHomeWithoutUserDirs)
{
	EXPECT_EQ(Config::GetVideosDirectory(m_configHome, m_home), m_home / "Videos");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CXdgPathsTest, VideosFallBackToHomeWhenUserDirsNamesNone)
{
	Tests::WriteText(m_configHome / "user-dirs.dirs", "XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n");

	EXPECT_EQ(Config::GetVideosDirectory(m_configHome, m_home), m_home / "Videos");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CXdgPathsTest, QuotedVideosUnderHomeAreExpanded)
{
	Tests::WriteText(m_configHome / "user-dirs.dirs", "# a comment\nXDG_VIDEOS_DIR=\"$HOME/Filme\"\n");

	EXPECT_EQ(Config::GetVideosDirectory(m_configHome, m_home), m_home / "Filme");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CXdgPathsTest, VideosLoseOneTrailingSlash)
{
	Tests::WriteText(m_configHome / "user-dirs.dirs", "XDG_VIDEOS_DIR=\"/data/clips/\"\n");

	EXPECT_EQ(Config::GetVideosDirectory(m_configHome, m_home), "/data/clips");
}

//////////////////////////////////////////////////////////////////////////
TEST_F(CXdgPathsTest, LastVideosEntryWins)
{
	Tests::WriteText(m_configHome / "user-dirs.dirs", "XDG_VIDEOS_DIR=\"/first\"\nXDG_VIDEOS_DIR=\"/second\"\n");

	EXPECT_EQ(Config::GetVideosDirectory(m_configHome, m_home), "/second");
}
