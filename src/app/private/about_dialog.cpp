#include "about_dialog.hpp"

#include "bus/file_uri.hpp"
#include "format_to.hpp"
#include "licensed_components.hpp"
#include "log.hpp"
#include "theme.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace Klip
{
namespace
{
constexpr char const* PopupId{ "About Klip###about" };
constexpr float       PageEm{ 35.0f };
constexpr float       PageLines{ 11.0f };
constexpr float       ListEm{ 11.0f };
constexpr float       TitleScale{ 1.6f };

//////////////////////////////////////////////////////////////////////////
std::string_view OrNone(char const* pText)
{
	return (pText != nullptr) ? std::string_view{ pText } : std::string_view{ "none" };
}

//////////////////////////////////////////////////////////////////////////
std::string_view OrNone(std::string_view text)
{
	return text.empty() ? std::string_view{ "none" } : text;
}

//////////////////////////////////////////////////////////////////////////
void DrawDisabledText(std::string_view text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextUnformatted(text.data(), text.data() + text.size());
	ImGui::PopStyleColor();
}

//////////////////////////////////////////////////////////////////////////
bool SelectableText(std::string_view text, bool isSelected)
{
	ImVec2 const position{ ImGui::GetCursorScreenPos() };
	float const  width{ ImGui::CalcTextSize(text.data(), text.data() + text.size()).x };
	bool const   isPressed{ ImGui::Selectable("##text", isSelected, ImGuiSelectableFlags_SpanAvailWidth,
	                                          ImVec2{ width, 0.0f }) };

	ImGui::GetWindowDrawList()->AddText(position, ImGui::GetColorU32(ImGuiCol_Text), text.data(),
	                                    text.data() + text.size());

	return isPressed;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
void CAboutDialog::Initialize(SDL_Window* pWindow, std::string_view configDir, std::string_view logsDir)
{
	m_pWindow = pWindow;
	m_configDir = configDir;
	m_logsDir = logsDir;
}

//////////////////////////////////////////////////////////////////////////
void CAboutDialog::Open()
{
	int const sdlVersion{ SDL_GetVersion() };

	m_systemInfo = std::format(
		"Klip {}\nSDL {}.{}.{} · video {} · renderer {} · UI scale {:.2f}\nDear ImGui {}\nConfig {}\nLogs {}",
		KLIP_VERSION, SDL_VERSIONNUM_MAJOR(sdlVersion), SDL_VERSIONNUM_MINOR(sdlVersion),
		SDL_VERSIONNUM_MICRO(sdlVersion), OrNone(SDL_GetCurrentVideoDriver()),
		OrNone(SDL_GetRendererName(SDL_GetRenderer(m_pWindow))), ImGui::GetStyle().FontScaleDpi, IMGUI_VERSION,
		OrNone(m_configDir), OrNone(m_logsDir));
	m_result.clear();
	m_shouldOpen = true;
}

//////////////////////////////////////////////////////////////////////////
void CAboutDialog::Draw()
{
	ImGuiViewport const* const pViewport{ ImGui::GetMainViewport() };
	ImVec2 const               pageSize{ PageEm * ImGui::GetFontSize(), ImGui::GetTextLineHeightWithSpacing() * PageLines };

	if (m_shouldOpen)
	{
		ImGui::OpenPopup(PopupId);
		m_shouldOpen = false;
	}

	ImGui::SetNextWindowPos(pViewport->GetWorkCenter(), ImGuiCond_Always, ImVec2{ 0.5f, 0.5f });

	if (ImGui::BeginPopupModal(PopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });

		if (ImGui::BeginTabBar("##tabs"))
		{
			if (ImGui::BeginTabItem("About"))
			{
				ImGui::BeginChild("##page", pageSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
				DrawAbout();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("System"))
			{
				ImGui::BeginChild("##page", pageSize);
				DrawSystem();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Licences"))
			{
				DrawLicences(pageSize);
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::PopStyleColor();

		if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

//////////////////////////////////////////////////////////////////////////
void CAboutDialog::DrawAbout() const
{
	std::array<char, 64> buffer{};

	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * TitleScale);
	ImGui::TextUnformatted("Klip");
	ImGui::PopFont();
	DrawDisabledText(FormatTo(buffer, "Version {}", KLIP_VERSION));
	ImGui::Spacing();
	ImGui::TextWrapped("Records your screen, a single window, or a rectangle of either, to a video file.");
	ImGui::Spacing();
	DrawDisabledText("© 2026 moly · MIT License");
}

//////////////////////////////////////////////////////////////////////////
void CAboutDialog::DrawSystem()
{
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextUnformatted(m_systemInfo.data(), m_systemInfo.data() + m_systemInfo.size());
	ImGui::PopTextWrapPos();
	ImGui::Spacing();

	if (ImGui::Button("Copy"))
	{
		m_isResultError = !SDL_SetClipboardText(m_systemInfo.c_str());
		m_result = m_isResultError ? "Cannot copy to the clipboard" : "Copied";

		if (m_isResultError)
		{
			gLog.Warning("Cannot copy the system info to the clipboard: {}", SDL_GetError());
		}
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(m_logsDir.empty());

	if (ImGui::Button("Open logs folder"))
	{
		std::string const uri{ Bus::ToFileUri(m_logsDir) };

		m_isResultError = !SDL_OpenURL(uri.c_str());
		m_result = m_isResultError ? "Cannot open the logs folder" : std::string{};

		if (m_isResultError)
		{
			gLog.Warning("Cannot open the logs folder '{}': {}", m_logsDir, SDL_GetError());
		}
	}

	ImGui::EndDisabled();

	if (!m_result.empty())
	{
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, m_isResultError ? GetErrorColor()
		                                                     : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextUnformatted(m_result.data(), m_result.data() + m_result.size());
		ImGui::PopStyleColor();
	}
}

//////////////////////////////////////////////////////////////////////////
void CAboutDialog::DrawLicences(ImVec2 const& pageSize)
{
	std::span<SLicensedComponent const> const components{ GetLicensedComponents() };
	float const                               listWidth{ ListEm * ImGui::GetFontSize() };

	ImGui::BeginChild("##components", ImVec2{ listWidth, pageSize.y });

	for (size_t index{ 0 }; index < components.size(); ++index)
	{
		ImGui::PushID(static_cast<int>(index));

		if (SelectableText(components[index].name, index == m_licenceIndex))
		{
			m_licenceIndex = index;
		}

		ImGui::PopID();
	}

	ImGui::EndChild();
	ImGui::SameLine();

	SLicensedComponent const& component{ components[m_licenceIndex] };
	char const* const         pText{ reinterpret_cast<char const*>(component.text.data()) };

	ImGui::BeginChild("##licence", ImVec2{ pageSize.x - listWidth - ImGui::GetStyle().ItemSpacing.x, pageSize.y },
	                  ImGuiChildFlags_Borders);
	DrawDisabledText(component.licence);
	ImGui::Spacing();
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextUnformatted(pText, pText + component.text.size());
	ImGui::PopTextWrapPos();
	ImGui::EndChild();
}
} // namespace Klip
