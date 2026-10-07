#include "config/config_test.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace Klip::Tests
{
//////////////////////////////////////////////////////////////////////////
void CConfigTest::SetUp()
{
	std::error_code error{};
	std::string     pattern{ (std::filesystem::temp_directory_path(error) / "klip-config-XXXXXX").string() };

	ASSERT_NE(::mkdtemp(pattern.data()), nullptr);
	m_home = pattern;
	m_configHome = m_home / ".config";
}

//////////////////////////////////////////////////////////////////////////
void CConfigTest::TearDown()
{
	std::error_code error{};

	std::filesystem::remove_all(m_home, error);
}

//////////////////////////////////////////////////////////////////////////
std::filesystem::path CConfigTest::GetSettingsPath() const
{
	return m_configHome / "klip" / "config.json";
}

//////////////////////////////////////////////////////////////////////////
std::filesystem::path CConfigTest::GetLegacyPath() const
{
	return m_configHome / "klip" / "Klip.conf";
}

//////////////////////////////////////////////////////////////////////////
std::string ReadText(std::filesystem::path const& path)
{
	std::ifstream file{ path, std::ios::binary };

	return std::string{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
}

//////////////////////////////////////////////////////////////////////////
void WriteText(std::filesystem::path const& path, std::string_view text)
{
	std::error_code error{};

	std::filesystem::create_directories(path.parent_path(), error);
	ASSERT_EQ(error.value(), 0) << path.parent_path();

	std::ofstream file{ path, std::ios::binary };

	file << text;
}
} // namespace Klip::Tests
