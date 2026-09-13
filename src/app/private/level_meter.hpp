#pragma once

#include <tge/non_copyable.hpp>

#include <QtCore/QElapsedTimer>
#include <QtWidgets/QWidget>

#include <array>
#include <cstdint>

namespace Klip
{
class CLevelMeter final : public QWidget, private Tge::SNoCopyNoMove
{
	Q_OBJECT

public:

	explicit CLevelMeter(QWidget* pParent = nullptr);
	~CLevelMeter() override = default;

	void SetChannelCount(uint32_t numChannels);

	void SetPeaks(float const* pPeaks, uint32_t numChannels);

	void Reset();

	void SetUnavailable(bool unavailable);

	QSize sizeHint() const override;

protected:

	void paintEvent(QPaintEvent* pEvent) override;

private:

	static constexpr uint32_t MaxChannels{ 2 };

	struct SChannel final
	{
		float         level{ 0.0f };
		float         hold{ 0.0f };
		QElapsedTimer heldSince;
		QElapsedTimer fallingSince;
		QElapsedTimer clippedSince;
		bool          clipped{ false };
	};

	std::array<SChannel, MaxChannels> m_channels;

	uint32_t m_numChannels{ 0 };

	bool m_unavailable{ false };
};
} // namespace Klip
