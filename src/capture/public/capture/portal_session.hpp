#pragma once

#include <tge/non_copyable.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

#include <cstdint>
#include <functional>

namespace Klip::Capture
{
struct SStreamInfo final
{
	uint32_t nodeId{ 0 };

	// Logical layout size. Mutter streams at this size too, so a scaled display records scaled.
	uint32_t width{ 0 };
	uint32_t height{ 0 };
};

enum class ESourceType : uint8_t
{
	Screen,
	Window
};

enum class EPortalResult : uint8_t
{
	Success,
	Cancelled,
	Failed
};

class CPortalSession final : public QObject, private Tge::SNoCopyNoMove
{
	Q_OBJECT

public:

	// On success the callee owns the descriptor and must close it.
	using ResultCallback = std::function<void(EPortalResult, SStreamInfo const&, int)>;

	using ClosedCallback = std::function<void()>;

	explicit CPortalSession(QObject* pParent = nullptr);
	~CPortalSession() override = default;

	bool Initialize();
	void Terminate();

	// Idempotent; a later Start opens a fresh session.
	void Close();

	void SetClosedCallback(ClosedCallback callback) { m_onClosed = std::move(callback); }

	void Start(ESourceType source, bool rememberWindow, ResultCallback callback);

private Q_SLOTS:

	// uint, not uint32_t: QDBusConnection matches on the signature as written.
	void OnCreateSessionResponse(uint response, QVariantMap const& results);
	void OnSelectSourcesResponse(uint response, QVariantMap const& results);
	void OnStartResponse(uint response, QVariantMap const& results);
	void OnSessionClosed();

private:

	QString MakeToken();


	// Subscribes before the call goes out; the portal can answer first.
	QString PrepareRequest(QString const& token, char const* pSlot);

	void Finish(EPortalResult result, SStreamInfo const& info, int pipeWireFd);

	ResultCallback m_callback;
	ClosedCallback m_onClosed;
	QString        m_requestPath;
	QString        m_sessionHandle;
	QString        m_restoreToken;
	ESourceType    m_source{ ESourceType::Screen };
	uint32_t       m_availableSourceTypes{ 0 };
	uint32_t       m_availableCursorModes{ 0 };
	uint32_t       m_tokenCounter{ 0 };
	bool           m_rememberWindow{ false };
	bool           m_initialized{ false };
	bool           m_busy{ false };
	bool           m_sessionLive{ false };
};
} // namespace Klip::Capture
