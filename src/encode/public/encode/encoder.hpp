#pragma once

#include "capture/audio_buffer.hpp"
#include "capture/frame.hpp"
#include "encode/settings.hpp"

#include <tge/non_copyable.hpp>

#include <atomic>
#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

struct AVAudioFifo;
struct AVBufferRef;
struct AVCodecContext;
struct AVFilterContext;
struct AVFilterGraph;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
struct SwsContext;

namespace Klip::Encode
{
class CEncoder final : private Tge::SNoCopyNoMove
{
public:

	CEncoder() = default;
	~CEncoder() = default;

	bool Initialize(SSettings const& settings);
	void Terminate();

	bool SubmitFrame(Capture::SFrame const& frame);

	bool SubmitAudio(uint32_t source, Capture::SAudioBuffer const& buffer);

	// Drains the encoder and writes the trailer. The file is not playable until this runs.
	// endTimestampNs holds the last frame until then, so a screen that stopped changing does not
	// shorten the recording. Zero ends the file at the last frame.
	bool Finish(uint64_t endTimestampNs);

	uint64_t GetNumFramesEncoded() const { return m_numFramesEncoded.load(std::memory_order_relaxed); }
	uint64_t GetBytesWritten() const { return m_bytesWritten.load(std::memory_order_relaxed); }

private:

	bool OpenDevice();
	bool OpenVaapiFrames(SSettings const& settings);
	bool OpenDrmDevice();
	bool OpenFilterGraph(SSettings const& settings);

	// Ahead of OpenEncoder: the codec must know whether the muxer wants a global header before it opens.
	bool OpenContainer(SSettings const& settings);
	bool OpenEncoder(SSettings const& settings);
	bool OpenAudioEncoder(SSettings const& settings);
	bool OpenVideoStream(SSettings const& settings);
	bool OpenAudioStream();
	bool OpenOutput(SSettings const& settings);
	bool SubmitMapped(Capture::SFrame const& frame);
	bool SubmitDmaBuf(Capture::SFrame const& frame);
	void WaitForSurface(AVFrame const* pFrame);
	bool SendToEncoder(AVFrame* pFrame, Capture::SFrame const& frame);
	bool DrainPackets(AVCodecContext* pContext, AVStream const* pStream, AVPacket* pPacket);

	void FillSilence(uint32_t source, uint64_t numSamples);
	void LevelSources(int64_t toleranceNs);
	void WriteSilence(uint32_t source, uint64_t nanoseconds);

	// numSamples must not exceed the codec's frame size: that is what the frames are sized for.
	bool EncodeMixedFrame(int numSamples);
	bool EncodeAudioFrames();
	void FinishAudio();

	AVBufferRef*     m_pDeviceRef{ nullptr };
	AVBufferRef*     m_pFramesRef{ nullptr };
	AVBufferRef*     m_pDrmDeviceRef{ nullptr };
	AVBufferRef*     m_pDrmFramesRef{ nullptr };
	AVFilterGraph*   m_pGraph{ nullptr };
	AVFilterContext* m_pGraphSource{ nullptr };
	AVFilterContext* m_pGraphSink{ nullptr };
	AVFrame*         m_pDrmFrame{ nullptr };
	AVFrame*         m_pFilteredFrame{ nullptr };

	// Whichever of the two above went to the encoder last; the paths use different ones.
	AVFrame*         m_pLastEncoded{ nullptr };
	AVCodecContext*  m_pCodecContext{ nullptr };
	AVFormatContext* m_pFormatContext{ nullptr };
	AVStream*        m_pStream{ nullptr };
	AVFrame*         m_pSoftwareFrame{ nullptr };
	AVFrame*         m_pHardwareFrame{ nullptr };
	AVPacket*        m_pVideoPacket{ nullptr };
	SwsContext*      m_pScaler{ nullptr };

	AVCodecContext* m_pAudioCodecContext{ nullptr };
	AVStream*       m_pAudioStream{ nullptr };
	AVFrame*        m_pAudioFrame{ nullptr };
	AVFrame*        m_pMixFrame{ nullptr };
	AVPacket*       m_pAudioPacket{ nullptr };

	struct SAudioSource final
	{
		AVAudioFifo* pFifo{ nullptr };
		uint64_t     numSamples{ 0 };
		uint64_t     numSilent{ 0 };
		uint32_t     numGaps{ 0 };
		bool         warnedStall{ false };
	};

	static constexpr uint32_t MaxAudioSources{ 2 };

	std::array<SAudioSource, MaxAudioSources> m_audioSources;
	std::array<float, MaxAudioSources>        m_audioGain{ 1.0f, 1.0f };

	std::vector<float> m_silence;

	std::mutex m_audioMutex;

	// av_interleaved_write_frame alone: holding this across an encode would put a GPU wait inside it,
	// on the very thread that must hand a PipeWire buffer straight back.
	std::mutex m_muxMutex;

	uint32_t m_numAudioSources{ 0 };
	uint32_t m_audioSampleRate{ 0 };
	uint32_t m_numAudioChannels{ 0 };
	uint64_t m_numAudioSamples{ 0 };
	int64_t  m_lastAudioDriftNs{ 0 };

	uint32_t m_outputWidth{ 0 };
	uint32_t m_outputHeight{ 0 };
	SRegion  m_region;

	uint64_t              m_firstTimestampNs{ 0 };
	std::atomic<uint64_t> m_numFramesEncoded{ 0 };
	std::atomic<uint64_t> m_bytesWritten{ 0 };
	int64_t  m_nextPts{ 0 };
	int64_t  m_lastPts{ 0 };

	Capture::EPixelFormat m_sourceFormat{ Capture::EPixelFormat::BGRx };
	Capture::EFrameMemory m_memory{ Capture::EFrameMemory::Mapped };
	char                  m_devicePath[32]{};

	bool m_hasFirstTimestamp{ false };
	bool m_headerWritten{ false };
	bool m_reportedSyncFailure{ false };
};
} // namespace Klip::Encode
