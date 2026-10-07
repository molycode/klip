#pragma once

#include "view_intents.hpp"

#include <tge/non_copyable.hpp>

#include <string>

namespace Klip
{
namespace Recorder
{
class CRecorder;
} // namespace Recorder

class CMainView final : private Tge::SNoCopyNoMove
{
public:

	CMainView() = default;
	~CMainView() = default;

	SViewIntents Draw(Recorder::CRecorder& recorder, float scale, bool isFolderDialogOpen);

private:

	void DrawAudio(Recorder::CRecorder& recorder, float scale, float column);

	std::string m_directory;
};
} // namespace Klip
