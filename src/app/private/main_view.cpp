#include "main_view.hpp"

#include "capture/audio_device.hpp"
#include "format_to.hpp"
#include "icons.hpp"
#include "level_meter.hpp"
#include "recorder/audio_choice.hpp"
#include "recorder/audio_source.hpp"
#include "recorder/frame_rates.hpp"
#include "recorder/labels.hpp"
#include "recorder/recorder.hpp"
#include "recorder/settings.hpp"
#include "theme.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>

namespace Klip
{
namespace
{
constexpr std::array FormLabels{ "Record", "Save to", "Format", "Codec", "Frame rate", "Quality" };

constexpr std::array Sources{ Recorder::ESource::Screen, Recorder::ESource::Window, Recorder::ESource::Region };

constexpr std::array Qualities{ Encode::EQuality::Smallest, Encode::EQuality::Smaller, Encode::EQuality::Balanced,
	                            Encode::EQuality::High, Encode::EQuality::Best };

constexpr std::array AudioSources{ Recorder::EAudioSource::System, Recorder::EAudioSource::Microphone };

constexpr float RecordButtonHeight{ 44.0f };

constexpr char const* WebmAudioTip{ "WebM carries only Opus or Vorbis, which this build cannot write. "
	                                "Choose MP4 or Matroska to record sound." };

using LabelBuffer = std::array<char, 64>;

//////////////////////////////////////////////////////////////////////////
char const* GetAudioLabel(Recorder::EAudioSource source)
{
	return source == Recorder::EAudioSource::System ? KLIP_ICON_VOLUME_HIGH "  System##enabled"
	                                                : KLIP_ICON_MICROPHONE "  Microphone##enabled";
}

//////////////////////////////////////////////////////////////////////////
// Wide enough for every label, the audio checkboxes among them, counted from the window's edge as the
// cursor is.
float GetLabelColumn()
{
	ImGuiStyle const& style{ ImGui::GetStyle() };
	float             widest{ 0.0f };

	for (char const* const pLabel : FormLabels)
	{
		widest = std::max(widest, ImGui::CalcTextSize(pLabel).x);
	}

	for (Recorder::EAudioSource const source : AudioSources)
	{
		widest = std::max(widest, ImGui::GetFrameHeight() + style.ItemInnerSpacing.x +
		                              ImGui::CalcTextSize(GetAudioLabel(source), nullptr, true).x);
	}

	return style.WindowPadding.x + widest + style.ItemSpacing.x * 2.0f;
}

//////////////////////////////////////////////////////////////////////////
// The column is set rather than passed to SameLine, which inside a group would count from the group's edge.
void BeginRow(char const* pLabel, float column)
{
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(pLabel);
	ImGui::SameLine();
	ImGui::SetCursorPosX(column);
	ImGui::SetNextItemWidth(-FLT_MIN);
}

//////////////////////////////////////////////////////////////////////////
void BeginEmptyRow(float column)
{
	ImGui::SetCursorPosX(column);
	ImGui::SetNextItemWidth(-FLT_MIN);
}

//////////////////////////////////////////////////////////////////////////
// The labels are Klip's own, copied so ImGui gets the terminator it needs.
template<std::ranges::input_range TValues>
std::optional<std::ranges::range_value_t<TValues>> Choose(
	char const* pId, TValues const& values, std::ranges::range_value_t<TValues> current,
	std::string_view (*getLabel)(std::ranges::range_value_t<TValues>))
{
	std::optional<std::ranges::range_value_t<TValues>> chosen{};
	LabelBuffer                                         label{};

	FormatTo(label, "{}", getLabel(current));

	if (ImGui::BeginCombo(pId, label.data()))
	{
		for (auto const value : values)
		{
			bool const isSelected{ value == current };

			FormatTo(label, "{}", getLabel(value));

			if (ImGui::Selectable(label.data(), isSelected) && !isSelected)
			{
				chosen = value;
			}

			if (isSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}

	return chosen;
}

//////////////////////////////////////////////////////////////////////////
// A device's description is the system's text, so it is drawn beside an empty label rather than parsed as one.
std::optional<std::string_view> ChooseDevice(std::span<Capture::SAudioDevice const> devices,
                                             std::string_view current)
{
	std::optional<std::string_view> chosen{};
	auto const                      pCurrent{ std::ranges::find(devices, current, &Capture::SAudioDevice::nodeName) };
	char const*                     pPreview{ pCurrent != devices.end() ? pCurrent->description.c_str() : "" };

	if (ImGui::BeginCombo("##device", pPreview))
	{
		for (size_t index{ 0 }; index < devices.size(); ++index)
		{
			Capture::SAudioDevice const& device{ devices[index] };
			bool const                   isSelected{ device.nodeName == current };
			ImVec2 const                 position{ ImGui::GetCursorScreenPos() };

			ImGui::PushID(static_cast<int>(index));

			if (ImGui::Selectable("##item", isSelected) && !isSelected)
			{
				chosen = device.nodeName;
			}

			ImGui::GetWindowDrawList()->AddText(position, ImGui::GetColorU32(ImGuiCol_Text), device.description.c_str());
			ImGui::PopID();

			if (isSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}

	return chosen;
}

//////////////////////////////////////////////////////////////////////////
void DrawHint(std::string const& hint, float column)
{
	ImGui::SetCursorPosX(column);
	ImGui::TextDisabled("%s", hint.c_str());
}

//////////////////////////////////////////////////////////////////////////
float GetButtonWidth(char const* pLabel)
{
	return ImGui::CalcTextSize(pLabel, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

//////////////////////////////////////////////////////////////////////////
// GetColorU32 applies the style's alpha, which BeginDisabled lowers.
bool DrawRecordButton(bool isRecording, float scale)
{
	ImVec4 const accent{ GetAccentColor() };

	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ accent.x, accent.y, accent.z, 0.10f });
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ accent.x, accent.y, accent.z, 0.18f });

	bool const isPressed{ ImGui::Button(isRecording ? KLIP_ICON_STOP "  Stop" : KLIP_ICON_RECORD "  Record",
	                                    ImVec2{ -FLT_MIN, RecordButtonHeight * scale }) };
	float const borderAlpha{ ImGui::IsItemHovered() ? 0.9f : 0.55f };

	ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
	                                    ImGui::GetColorU32(ImVec4{ accent.x, accent.y, accent.z, borderAlpha }),
	                                    ImGui::GetStyle().FrameRounding, scale);
	ImGui::PopStyleColor(2);

	return isPressed;
}

//////////////////////////////////////////////////////////////////////////
void DrawMenuBar(SViewIntents& intents)
{
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Klip"))
		{
			intents.about = ImGui::MenuItem("About Klip");
			ImGui::Separator();
			intents.quit = ImGui::MenuItem("Quit", "Ctrl+Q");
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}
}
} // namespace

//////////////////////////////////////////////////////////////////////////
SViewIntents CMainView::Draw(Recorder::CRecorder& recorder, float scale, bool isFolderDialogOpen)
{
	SViewIntents               intents{};
	Recorder::SSettings const& settings{ recorder.GetSettings() };
	Recorder::EState const     state{ recorder.GetState() };
	bool const                 isIdle{ state == Recorder::EState::Idle };
	float const                column{ GetLabelColumn() };

	DrawMenuBar(intents);
	intents.quit = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal) || intents.quit;

	ImGui::BeginDisabled(!isIdle);
	BeginRow("Record", column);

	if (std::optional<Recorder::ESource> const source{
		    Choose("##source", Sources, settings.source, Recorder::GetSourceLabel) };
	    source.has_value())
	{
		recorder.SetSource(*source);
	}

	ImGui::EndDisabled();

	bool rememberWindow{ settings.rememberWindow };

	ImGui::BeginDisabled(settings.source != Recorder::ESource::Window);
	ImGui::SetCursorPosX(column);

	if (ImGui::Checkbox("Remember the window", &rememberWindow))
	{
		recorder.SetRememberWindow(rememberWindow);
	}

	ImGui::SetItemTooltip("Skip the picker and record the same window again. Untick to be asked each time.");
	ImGui::EndDisabled();

	constexpr char const* OpenLabel{ KLIP_ICON_FOLDER_OPEN "  Open" };
	constexpr char const* ChangeLabel{ KLIP_ICON_FOLDER "  Change\xe2\x80\xa6" };

	float const buttonsWidth{ GetButtonWidth(OpenLabel) + GetButtonWidth(ChangeLabel) +
		                      ImGui::GetStyle().ItemSpacing.x * 2.0f };

	// ImGui's read-only field still edits a string of its own.
	if (m_directory != settings.directory)
	{
		m_directory = settings.directory;
	}

	BeginRow("Save to", column);
	ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - buttonsWidth, 1.0f));
	ImGui::BeginDisabled(!isIdle);
	ImGui::InputText("##directory", &m_directory, ImGuiInputTextFlags_ReadOnly);
	ImGui::EndDisabled();
	ImGui::SameLine();

	intents.open = ImGui::Button(OpenLabel);
	ImGui::SetItemTooltip("Show the recordings in your file manager");
	ImGui::SameLine();
	ImGui::BeginDisabled(!isIdle || isFolderDialogOpen);
	intents.browse = ImGui::Button(ChangeLabel);
	ImGui::EndDisabled();

	ImGui::BeginDisabled(!isIdle);
	BeginRow("Format", column);

	if (std::optional<Encode::EContainer> const container{
		    Choose("##container", recorder.GetContainers(), settings.container, Recorder::GetContainerLabel) };
	    container.has_value())
	{
		recorder.SetContainer(*container);
	}

	BeginRow("Codec", column);

	if (std::optional<Encode::ECodec> const codec{
		    Choose("##codec", recorder.GetCodecs(), settings.codec, Recorder::GetCodecLabel) };
	    codec.has_value())
	{
		recorder.SetCodec(*codec);
	}

	BeginRow("Frame rate", column);

	if (std::optional<uint32_t> const frameRate{
		    Choose("##frame-rate", Recorder::FrameRateCaps, settings.maxFrameRate, Recorder::GetFrameRateLabel) };
	    frameRate.has_value())
	{
		recorder.SetMaxFrameRate(*frameRate);
	}

	BeginRow("Quality", column);

	if (std::optional<Encode::EQuality> const quality{
		    Choose("##quality", Qualities, settings.quality, Recorder::GetQualityLabel) };
	    quality.has_value())
	{
		recorder.SetQuality(*quality);
	}

	ImGui::EndDisabled();
	DrawHint(recorder.GetQualityHint(), column);

	DrawAudio(recorder, scale, column);

	ImGui::Spacing();
	ImGui::BeginDisabled(state == Recorder::EState::Starting);
	intents.toggle = DrawRecordButton(state == Recorder::EState::Recording, scale);
	ImGui::EndDisabled();

	std::string const& status{ recorder.GetStatus() };

	ImGui::TextUnformatted(recorder.GetElapsed().c_str());
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
	                     std::max(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(status.c_str()).x, 0.0f));
	ImGui::TextUnformatted(status.c_str());

	return intents;
}

//////////////////////////////////////////////////////////////////////////
// Source by source, each one's rows together; the gain and the meter only while it is ticked and can be recorded.
void CMainView::DrawAudio(Recorder::CRecorder& recorder, float scale, float column)
{
	Recorder::SSettings const& settings{ recorder.GetSettings() };
	bool const                 isIdle{ recorder.GetState() == Recorder::EState::Idle };
	bool const                 carries{ recorder.CarriesAudio() };

	ImGui::SeparatorText("Audio");
	ImGui::BeginGroup();

	for (Recorder::EAudioSource const source : AudioSources)
	{
		Recorder::SAudioChoice const& choice{ settings.audio[static_cast<size_t>(source)] };
		bool                          isEnabled{ choice.enabled };

		ImGui::PushID(static_cast<int>(source));
		ImGui::BeginDisabled(!isIdle || !carries);
		ImGui::AlignTextToFramePadding();

		if (ImGui::Checkbox(GetAudioLabel(source), &isEnabled))
		{
			recorder.SetAudioEnabled(source, isEnabled);
		}

		if (source == Recorder::EAudioSource::System)
		{
			ImGui::SetItemTooltip("Record what the machine plays, whatever the speakers are set to.");
		}

		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SetCursorPosX(column);
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::BeginDisabled(!isIdle || !carries || !choice.enabled);

		if (std::optional<std::string_view> const device{ ChooseDevice(recorder.GetDevices(source), choice.device) };
		    device.has_value())
		{
			recorder.SetAudioDevice(source, std::string{ *device });
		}

		if (carries && choice.enabled)
		{
			int gain{ choice.gainDecibels };

			BeginEmptyRow(column);

			if (ImGui::SliderInt("##gain", &gain, Recorder::MinimumGainDecibels, Recorder::MaximumGainDecibels, "%d dB"))
			{
				recorder.SetGain(source, gain);
			}

			ImGui::SetItemTooltip("Klip's own level for this source. Your system volumes are left alone.");
		}

		ImGui::EndDisabled();

		if (carries && choice.enabled)
		{
			ImGui::SetCursorPosX(column);
			DrawLevelMeter(recorder.GetMeter(source), scale);
		}

		ImGui::PopID();
	}

	ImGui::BeginDisabled(!isIdle || !carries);
	BeginRow("Quality", column);

	if (std::optional<Encode::EQuality> const quality{
		    Choose("##audio-quality", Qualities, settings.audioQuality, Recorder::GetQualityLabel) };
	    quality.has_value())
	{
		recorder.SetAudioQuality(*quality);
	}

	ImGui::EndDisabled();
	DrawHint(recorder.GetAudioQualityHint(), column);
	ImGui::EndGroup();

	if (!carries)
	{
		ImGui::SetItemTooltip("%s", WebmAudioTip);
	}
}
} // namespace Klip
