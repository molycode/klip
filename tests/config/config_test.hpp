#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace Klip::Tests
{
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
void        WriteText(std::filesystem::path const& path, std::string_view text);
} // namespace Klip::Tests
