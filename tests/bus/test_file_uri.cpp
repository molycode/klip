#include "bus/file_uri.hpp"

#include <gtest/gtest.h>

#include <string_view>

using namespace Klip;

//////////////////////////////////////////////////////////////////////////
TEST(FileUri, PlainPathIsKept)
{
	EXPECT_EQ(Bus::ToFileUri("/home/joe/Videos/klip-captures"), "file:///home/joe/Videos/klip-captures");
}

//////////////////////////////////////////////////////////////////////////
TEST(FileUri, ReservedCharactersAreEncoded)
{
	EXPECT_EQ(Bus::ToFileUri("/home/a b/100%/#1?"), "file:///home/a%20b/100%25/%231%3F");
}

//////////////////////////////////////////////////////////////////////////
TEST(FileUri, Utf8IsEncodedByteByByte)
{
	EXPECT_EQ(Bus::ToFileUri("/home/jörg"), "file:///home/j%C3%B6rg");
}

//////////////////////////////////////////////////////////////////////////
TEST(FileUri, PathSurvivesTheRoundTrip)
{
	constexpr std::string_view Path{ "/home/jörg/a b/100%/#1?;,=\"'\\" };

	EXPECT_EQ(Bus::ToLocalPath(Bus::ToFileUri(Path)), Path);
}

//////////////////////////////////////////////////////////////////////////
TEST(FileUri, LowercaseEscapesDecode)
{
	EXPECT_EQ(Bus::ToLocalPath("file:///home/j%c3%b6rg"), "/home/jörg");
}

//////////////////////////////////////////////////////////////////////////
TEST(FileUri, LocalhostIsLocal)
{
	EXPECT_EQ(Bus::ToLocalPath("file://localhost/tmp/shot.png"), "/tmp/shot.png");
}

//////////////////////////////////////////////////////////////////////////
TEST(FileUri, OtherHostIsNotLocal)
{
	EXPECT_EQ(Bus::ToLocalPath("file://server/tmp/shot.png"), "");
}

//////////////////////////////////////////////////////////////////////////
TEST(FileUri, OtherSchemeIsNotLocal)
{
	EXPECT_EQ(Bus::ToLocalPath("https://example.org/shot.png"), "");
}

//////////////////////////////////////////////////////////////////////////
TEST(FileUri, MalformedEscapeIsRefused)
{
	EXPECT_EQ(Bus::ToLocalPath("file:///tmp/100%"), "");
	EXPECT_EQ(Bus::ToLocalPath("file:///tmp/%zz"), "");
}

//////////////////////////////////////////////////////////////////////////
TEST(FileUri, EncodedNulIsRefused)
{
	EXPECT_EQ(Bus::ToLocalPath("file:///tmp/a%00b"), "");
}
