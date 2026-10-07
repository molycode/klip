#pragma once

#include <tge/non_copyable.hpp>

#include <cstddef>
#include <string>
#include <string_view>

struct ImVec2;
struct SDL_Window;

namespace Klip
{
class CAboutDialog final : private Tge::SNoCopyNoMove
{
public:

	CAboutDialog() = default;
	~CAboutDialog() = default;

	void Initialize(SDL_Window* pWindow, std::string_view configDir, std::string_view logsDir);
	void Open();
	void Draw();

private:

	void DrawAbout() const;
	void DrawSystem();
	void DrawLicences(ImVec2 const& pageSize);

	SDL_Window* m_pWindow{ nullptr };
	std::string m_configDir;
	std::string m_logsDir;
	std::string m_systemInfo;
	std::string m_result;
	size_t      m_licenceIndex{ 0 };
	bool        m_isResultError{ false };
	bool        m_shouldOpen{ false };
};
} // namespace Klip
