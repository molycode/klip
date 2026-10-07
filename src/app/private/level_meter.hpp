#pragma once

#include <tge/non_copyable.hpp>

#include <QtWidgets/QWidget>

#include <cstdint>

namespace Klip
{
namespace Recorder
{
class CLevelBallistics;
} // namespace Recorder

class CLevelMeter final : public QWidget, private Tge::SNoCopyNoMove
{
	Q_OBJECT

public:

	explicit CLevelMeter(QWidget* pParent = nullptr);
	~CLevelMeter() override = default;

	void SetChannelCount(uint32_t numChannels);

	// Painted as it is at each repaint; the recorder moves it.
	void SetBallistics(Recorder::CLevelBallistics const* pBallistics);

	QSize sizeHint() const override;

protected:

	void paintEvent(QPaintEvent* pEvent) override;

private:

	static constexpr uint32_t MaxChannels{ 2 };

	Recorder::CLevelBallistics const* m_pBallistics{ nullptr };

	uint32_t m_numChannels{ 0 };
};
} // namespace Klip
