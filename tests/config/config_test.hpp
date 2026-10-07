#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace Klip::Tests
{
// A home of its own with the config home inside it, so no test reads or writes the real settings.
class CConfigTest : public testing::Test
{
protected:

	// testing::Test
	void SetUp() override;
	void TearDown() override;
	// ~testing::Test

	std::filesystem::path GetSettingsPath() const;
	std::filesystem::path GetLegacyPath() const;

	std::filesystem::path m_home;
	std::filesystem::path m_configHome;
};

std::string ReadText(std::filesystem::path const& path);
// Its directory is made first.
void        WriteText(std::filesystem::path const& path, std::string_view text);
} // namespace Klip::Tests
