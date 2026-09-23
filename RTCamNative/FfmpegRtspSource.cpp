#include "FfmpegRtspSource.h"
#include "Logger.h"
#include <windows.h>
#include <sstream>
#include <cstring>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libavutil/frame.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/mathematics.h>
#include <libavutil/hwcontext.h>
#include <libavutil/buffer.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace
{
	std::string ToUtf8(const std::wstring& w)
	{
		if (w.empty()) return std::string();
		int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
		return s;
	}

	void LogAv(const char* what, int err)
	{
		char buf[AV_ERROR_MAX_STRING_SIZE] = {};
		av_strerror(err, buf, sizeof(buf));
		std::ostringstream oss;
		oss << "FfmpegRtspSource - " << what << ": " << buf << " (" << err << ")";
		DebugLog(oss.str().c_str());
	}

	// NUL-terminated copy of src into a fixed dst[N] buffer (never overruns).
	template <size_t N>
	void CopyField(char (&dst)[N], const char* src)
	{
		if (!src) { dst[0] = '\0'; return; }
		size_t i = 0;
		for (; src[i] != '\0' && i + 1 < N; ++i)
			dst[i] = src[i];
		dst[i] = '\0';
	}

	// Process-wide engine options (see FfmpegRtspSource::SetTransportPreference /
	// SetHardwareDecodeEnabled). Set from the app's settings dialog; read when a new
	// connection opens. Atomic because the setters run on the UI thread while the
	// decode thread reads them.
	std::atomic<int>  g_transportPref{ (int)RtspTransport::Auto };
	std::atomic<bool> g_hwDecodeEnabled{ true };
	std::atomic<int>  g_socketTimeoutMs{ 5000 };
	std::atomic<int>  g_reorderQueue{ 512 };
	std::atomic<int>  g_udpBufferSize{ 2 * 1024 * 1024 }; // 2 MB
	std::atomic<int>  g_maxDelayMs{ 0 };
	std::atomic<int>  g_maxLagMs{ 350 };

	// Pause between two connection attempts. Short on purpose: a camera that reboots
	// or a cable that is re-plugged should be back on the virtual camera within a
	// couple of seconds, and a failed attempt already costs the socket timeout.
	constexpr uint32_t kReconnectDelayMs = 1000;

	// The rtsp_transport value libav wants for the current preference. "udp+tcp" is a
	// flag mask: libav tries the lower transports in enum order (UDP before TCP) and
	// keeps the first whose SETUP succeeds, so Auto gets UDP with a TCP fallback.
	const char* TransportOption()
	{
		switch ((RtspTransport)g_transportPref.load())
		{
		case RtspTransport::Udp: return "udp";
		case RtspTransport::Tcp: return "tcp";
		default:                 return "udp+tcp";
		}
	}

	// Decoder pixel-format negotiation: pick the D3D11 hardware surface when the
	// decoder offers it (DXVA path), otherwise fall back to whatever it prefers
	// (software). Called by libavcodec during avcodec_open2 / first decode.
	enum AVPixelFormat SelectHwFormat(AVCodecContext* /*ctx*/, const enum AVPixelFormat* fmts)
	{
		for (const enum AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
			if (*p == AV_PIX_FMT_D3D11)
				return AV_PIX_FMT_D3D11;
		return fmts[0]; // hardware not available for this codec — let it use software
	}
}

FfmpegRtspSource::~FfmpegRtspSource()
{
	Stop();
}

int FfmpegRtspSource::InterruptCb(void* opaque)
{
	auto* self = static_cast<FfmpegRtspSource*>(opaque);
	if (!self) return 0;
	if (self->_stop.load()) return 1;
	// Watchdog: abort the blocking call once its deadline has passed. This is what
	// guarantees av_read_frame / avformat_open_input return in bounded time even if
	// the camera vanishes silently (no RST/FIN) — the libav-side "timeout" option is
	// belt, this is braces.
	const uint64_t deadline = self->_ioDeadlineTick.load();
	return (deadline != 0 && GetTickCount64() > deadline) ? 1 : 0;
}

void FfmpegRtspSource::ArmWatchdog(uint32_t timeoutMs)
{
	_ioDeadlineTick.store(GetTickCount64() + timeoutMs);
}

void FfmpegRtspSource::SleepInterruptible(uint32_t ms)
{
	const ULONGLONG until = GetTickCount64() + ms;
	while (!_stop.load() && GetTickCount64() < until)
		Sleep(50);
}

void FfmpegRtspSource::NoteConnectionLost(int avError, bool wasStreaming)
{
	_lastError.store(avError);
	_activeTransport.store(0); // no live transport
	_bitrateBps.store(0);      // no throughput while disconnected
	if (wasStreaming)
	{
		_disconnects.fetch_add(1);
		_connState.store((int)ConnectionState::Reconnecting);
	}
	// else: still Connecting (never streamed) or already Reconnecting — keep it.
}

void FfmpegRtspSource::SetTransportPreference(RtspTransport t) { g_transportPref.store((int)t); }
RtspTransport FfmpegRtspSource::TransportPreference() { return (RtspTransport)g_transportPref.load(); }
void FfmpegRtspSource::SetHardwareDecodeEnabled(bool enabled) { g_hwDecodeEnabled.store(enabled); }
bool FfmpegRtspSource::HardwareDecodeEnabled() { return g_hwDecodeEnabled.load(); }

void FfmpegRtspSource::SetSocketTimeoutMs(int ms) { g_socketTimeoutMs.store(ms > 0 ? ms : 1); }
int  FfmpegRtspSource::SocketTimeoutMs() { return g_socketTimeoutMs.load(); }
void FfmpegRtspSource::SetReorderQueueSize(int packets) { g_reorderQueue.store(packets < 0 ? 0 : packets); }
int  FfmpegRtspSource::ReorderQueueSize() { return g_reorderQueue.load(); }
void FfmpegRtspSource::SetUdpBufferSize(int bytes) { g_udpBufferSize.store(bytes < 0 ? 0 : bytes); }
int  FfmpegRtspSource::UdpBufferSize() { return g_udpBufferSize.load(); }
void FfmpegRtspSource::SetMaxDelayMs(int ms) { g_maxDelayMs.store(ms < 0 ? 0 : ms); }
int  FfmpegRtspSource::MaxDelayMs() { return g_maxDelayMs.load(); }
void FfmpegRtspSource::SetMaxLagMs(int ms) { g_maxLagMs.store(ms > 0 ? ms : 1); }
int  FfmpegRtspSource::MaxLagMs() { return g_maxLagMs.load(); }

FfmpegProbeInfo FfmpegRtspSource::Probe(const std::wstring& rtspUrl)
{
	FfmpegProbeInfo info;
	if (rtspUrl.empty())
		return info;

	avformat_network_init();
	std::string url = ToUtf8(rtspUrl);

	AVFormatContext* fmt = avformat_alloc_context();
	if (!fmt)
	{
		avformat_network_deinit();
		return info;
	}

	// Hard bound on the whole probe (open + find_stream_info), enforced through the
	// interrupt callback so it holds regardless of which socket op is blocking.
	ULONGLONG probeDeadline = GetTickCount64() + (ULONGLONG)SocketTimeoutMs() * 2;
	fmt->interrupt_callback.callback = [](void* opaque) -> int
	{
		return GetTickCount64() > *static_cast<const ULONGLONG*>(opaque) ? 1 : 0;
	};
	fmt->interrupt_callback.opaque = &probeDeadline;

	AVDictionary* opts = nullptr;
	// Honor the user's transport preference (Auto = UDP with TCP fallback).
	av_dict_set(&opts, "rtsp_transport", TransportOption(), 0);
	// RTSP socket I/O timeout in microseconds. NOTE: the option is "timeout" — the
	// pre-5.0 name "stimeout" is gone from this libav and would be silently ignored.
	av_dict_set_int(&opts, "timeout", (int64_t)SocketTimeoutMs() * 1000, 0);
	av_dict_set(&opts, "probesize", "500000", 0);

	int r = avformat_open_input(&fmt, url.c_str(), nullptr, &opts);
	av_dict_free(&opts);
	if (r < 0)
	{
		LogAv("Probe/avformat_open_input", r);
		if (fmt) avformat_free_context(fmt);
		avformat_network_deinit();
		return info;
	}

	if (avformat_find_stream_info(fmt, nullptr) >= 0)
	{
		int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (vs >= 0)
		{
			AVStream* s = fmt->streams[vs];
			AVCodecParameters* par = s->codecpar;
			info.width = (uint32_t)(par->width > 0 ? par->width : 0);
			info.height = (uint32_t)(par->height > 0 ? par->height : 0);
			AVRational fr = (s->avg_frame_rate.num > 0) ? s->avg_frame_rate : s->r_frame_rate;
			info.fpsNum = (uint32_t)(fr.num > 0 ? fr.num : 30);
			info.fpsDen = (uint32_t)(fr.den > 0 ? fr.den : 1);
			info.codecId = (int)par->codec_id;
			info.valid = (info.width > 0 && info.height > 0);

			// Connection characteristics for the UI. Bitrate is often 0 on live RTSP
			// (the SDP rarely advertises one); fall back to the container total.
			info.bitrate = (par->bit_rate > 0) ? par->bit_rate : fmt->bit_rate;

			if (fmt->iformat)
				CopyField(info.container, fmt->iformat->long_name ? fmt->iformat->long_name
				                                                   : fmt->iformat->name);

			// Report the requested transport preference. libav doesn't expose which
			// lower transport was actually negotiated in Auto mode, so show "UDP/TCP"
			// there. Only meaningful for RTSP sources.
			if (fmt->iformat && fmt->iformat->name && std::strstr(fmt->iformat->name, "rtsp"))
			{
				const char* label = "UDP/TCP";
				switch (TransportPreference())
				{
				case RtspTransport::Udp: label = "UDP"; break;
				case RtspTransport::Tcp: label = "TCP"; break;
				default: break;
				}
				CopyField(info.transport, label);
			}

			CopyField(info.videoCodec, avcodec_get_name(par->codec_id));

			const char* prof = avcodec_profile_name(par->codec_id, par->profile);
			if (prof) CopyField(info.profile, prof);

			const char* pix = av_get_pix_fmt_name((AVPixelFormat)par->format);
			if (pix) CopyField(info.pixelFormat, pix);
		}
	}

	avformat_close_input(&fmt);
	avformat_network_deinit();
	return info;
}

bool FfmpegRtspSource::Start(const std::wstring& rtspUrl, uint32_t targetWidth, uint32_t targetHeight,
                             uint32_t fpsNum, uint32_t fpsDen, FrameSink sink)
{
	(void)fpsNum; (void)fpsDen; // producer pushes ASAP; the consumer paces delivery
	if (_running.load() || !sink)
		return false;
	_sink = std::move(sink);
	_target = FrameTarget{};
	return StartThread(rtspUrl, targetWidth, targetHeight);
}

bool FfmpegRtspSource::Start(const std::wstring& rtspUrl, uint32_t targetWidth, uint32_t targetHeight,
                             uint32_t fpsNum, uint32_t fpsDen, FrameTarget target)
{
	(void)fpsNum; (void)fpsDen; // producer pushes ASAP; the consumer paces delivery
	if (_running.load() || !target.acquire || !target.release)
		return false;
	_sink = nullptr;
	_target = std::move(target);
	return StartThread(rtspUrl, targetWidth, targetHeight);
}

bool FfmpegRtspSource::StartThread(const std::wstring& rtspUrl, uint32_t targetWidth, uint32_t targetHeight)
{
	if (rtspUrl.empty() || targetWidth == 0 || targetHeight == 0)
	{
		_sink = nullptr;
		_target = FrameTarget{};
		return false;
	}

	_stop.store(false);
	_framesDecoded.store(0);
	_hwActive.store(false);
	_lastLagMs.store(0);
	_bitrateBps.store(0);
	_activeTransport.store(0);
	_ioDeadlineTick.store(0);
	_connState.store((int)ConnectionState::Connecting);
	_connectAttempt.store(0);
	_disconnects.store(0);
	_lastError.store(0);
	avformat_network_init();

	std::string url = ToUtf8(rtspUrl);
	_running.store(true);
	_thread = std::thread(&FfmpegRtspSource::DecodeLoop, this, url, targetWidth, targetHeight);
	return true;
}

void FfmpegRtspSource::Stop()
{
	_stop.store(true); // makes InterruptCb abort any blocking libav call
	if (_thread.joinable())
		_thread.join();
	_running.store(false);
	_connState.store((int)ConnectionState::Idle);
	_connectAttempt.store(0);
	_activeTransport.store(0);
	_sink = nullptr; // release anything the sink/target captured
	_target = FrameTarget{};
	avformat_network_deinit();
}

void FfmpegRtspSource::DecodeLoop(std::string url, uint32_t targetW, uint32_t targetH)
{
	// Direct-target mode (producer): frames are written straight into the caller's
	// destination (the shared-memory slot), so no intermediate buffer is needed.
	const bool directTarget = (bool)_target.acquire;

	// NV12 destination frame for the sink path (preview), sized to the target once
	// for the whole session. Not allocated in direct-target mode.
	AVFrame* nv12 = nullptr;
	if (!directTarget)
	{
		nv12 = av_frame_alloc();
		if (!nv12 ||
			av_image_alloc(nv12->data, nv12->linesize, (int)targetW, (int)targetH, AV_PIX_FMT_NV12, 32) < 0)
		{
			DebugLog("FfmpegRtspSource::DecodeLoop - failed to allocate NV12 frame");
			if (nv12) av_frame_free(&nv12);
			return;
		}
	}
	// Wrapper AVFrame describing the target's planes for the zero-intermediate GPU
	// download (see below); its data is reset per frame.
	AVFrame* targetFrame = directTarget ? av_frame_alloc() : nullptr;

	// Hardware decode (d3d11va / DXVA): create the GPU device once for the whole
	// session. When attached to the decoder, H.264/H.265 decode runs on the GPU and
	// each frame comes back as a D3D11 surface; we download it to system-memory NV12
	// (swFrame) for the sink — far cheaper than software decoding a high-resolution
	// stream, which is what let the latency grow unbounded. If the device can't be
	// created, hwDeviceCtx stays null and we decode in software. The user can also
	// force software decode from the settings dialog (some d3d11va drivers misbehave).
	AVBufferRef* hwDeviceCtx = nullptr;
	if (!HardwareDecodeEnabled())
	{
		DebugLog("FfmpegRtspSource::DecodeLoop - hardware decode disabled by settings; using software decode");
	}
	else if (av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) < 0)
	{
		hwDeviceCtx = nullptr;
		DebugLog("FfmpegRtspSource::DecodeLoop - d3d11va unavailable, falling back to software decode");
	}
	AVFrame* swFrame = av_frame_alloc(); // receives the GPU->CPU download

	// Auto-transport state: unlike libav's "udp+tcp" (which hides which lower
	// transport won), we drive the fallback ourselves so ActiveTransport() is always
	// definitive. Start on UDP; if an attempt connects but never delivers a frame
	// (UDP blocked by NAT/firewall — the socket timeout bounds the wait), flip to TCP
	// on the next attempt, and keep alternating until one carries frames. A forced
	// UDP-only / TCP-only preference never flips.
	int  autoTransport = (int)RtspTransport::Udp;
	bool lastAttemptGotFrame = true; // don't flip before the first attempt

	// Outer loop: (re)connect until Stop(). One iteration == one connection attempt.
	while (!_stop.load())
	{
		const RtspTransport pref = TransportPreference();
		if (pref == RtspTransport::Auto && !lastAttemptGotFrame)
			autoTransport = (autoTransport == (int)RtspTransport::Udp)
				? (int)RtspTransport::Tcp : (int)RtspTransport::Udp;
		lastAttemptGotFrame = false;

		// The single, explicit transport for THIS attempt (never the "udp+tcp" mask,
		// so we know exactly what connected).
		const RtspTransport attemptTransport =
			(pref == RtspTransport::Auto) ? (RtspTransport)autoTransport : pref;
		const char* transportOpt = (attemptTransport == RtspTransport::Tcp) ? "tcp" : "udp";
		_activeTransport.store(0); // not connected until the first frame arrives

		// One more attempt since the last time frames flowed (1 = first try). The UI
		// shows this as "retrying (attempt N)".
		const uint32_t attemptNo = _connectAttempt.fetch_add(1) + 1;
		{
			std::ostringstream oss;
			oss << "FfmpegRtspSource::DecodeLoop - connection attempt " << attemptNo
			    << " over " << transportOpt;
			DebugLog(oss.str().c_str());
		}

		AVFormatContext* fmt = avformat_alloc_context();
		if (!fmt) break;
		fmt->interrupt_callback.callback = &FfmpegRtspSource::InterruptCb;
		fmt->interrupt_callback.opaque = this;

		// Snapshot the user-tunable numeric options for this attempt (see the setters).
		// The socket timeout doubles as the watchdog period: a connection that goes
		// silent for that long is torn down and reconnected.
		const uint32_t    socketTimeoutMs = (uint32_t)SocketTimeoutMs();
		const std::string reorderQ   = std::to_string(ReorderQueueSize());
		const std::string maxDelayUs = std::to_string((int64_t)MaxDelayMs() * 1000);
		const int         udpBufBytes = UdpBufferSize();
		const std::string udpBuf     = std::to_string(udpBufBytes);

		AVDictionary* opts = nullptr;
		av_dict_set(&opts, "rtsp_transport", transportOpt, 0);
		// RTSP socket I/O timeout in microseconds (covers connect, the RTSP handshake,
		// TCP recv and the UDP poll). NOTE: the option is "timeout" — the pre-5.0 name
		// "stimeout" no longer exists in this libav and was being silently ignored,
		// which left every read unbounded: a camera that vanished without RST/FIN made
		// av_read_frame block forever and the reconnect loop never ran.
		av_dict_set_int(&opts, "timeout", (int64_t)socketTimeoutMs * 1000, 0);
		// --- Latency-critical demux options ---------------------------------------
		// RTP reorder buffer: on TCP packets never reorder (harmless), but on UDP a
		// value of 0 would drop every out-of-order packet and shred the picture. A
		// larger window absorbs the packet bursts of a high-bitrate stream.
		av_dict_set(&opts, "reorder_queue_size", reorderQ.c_str(), 0);
		// UDP receive buffer: the default socket buffer overflows on the bursts of a
		// high-bitrate 1080p stream (green bands). 0 leaves libav's default alone.
		if (udpBufBytes > 0)
			av_dict_set(&opts, "buffer_size", udpBuf.c_str(), 0);
		av_dict_set(&opts, "max_delay", maxDelayUs.c_str(), 0); // demux reorder delay (µs; 0 = none)
		av_dict_set(&opts, "fflags", "nobuffer+discardcorrupt", 0); // don't buffer input; drop corrupt frames instead of stalling
		av_dict_set(&opts, "flags", "low_delay", 0);          // ask the demuxer/codec for minimal delay
		av_dict_set(&opts, "avioflags", "direct", 0);         // no read-ahead buffering on the socket
		av_dict_set(&opts, "probesize", "500000", 0);         // small probe → faster startup (steady-state unaffected)
		av_dict_set(&opts, "analyzeduration", "0", 0);        // RTSP carries codec params in the SDP; skip the analyze wait

		// Bound the whole open (DNS + connect + DESCRIBE/SETUP/PLAY): the per-op
		// "timeout" above covers each socket call, the watchdog covers their sum.
		ArmWatchdog(socketTimeoutMs * 3);
		int r = avformat_open_input(&fmt, url.c_str(), nullptr, &opts);
		av_dict_free(&opts);
		if (r < 0)
		{
			LogAv("avformat_open_input", r);
			if (fmt) avformat_free_context(fmt);
			NoteConnectionLost(r, false);
			SleepInterruptible(kReconnectDelayMs);
			continue;
		}

		ArmWatchdog(socketTimeoutMs * 2);
		if ((r = avformat_find_stream_info(fmt, nullptr)) < 0)
		{
			LogAv("avformat_find_stream_info", r);
			avformat_close_input(&fmt);
			NoteConnectionLost(r, false);
			SleepInterruptible(kReconnectDelayMs);
			continue;
		}

		int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (vs < 0)
		{
			DebugLog("FfmpegRtspSource::DecodeLoop - no video stream");
			avformat_close_input(&fmt);
			NoteConnectionLost(AVERROR_STREAM_NOT_FOUND, false);
			SleepInterruptible(kReconnectDelayMs);
			continue;
		}

		AVCodecParameters* par = fmt->streams[vs]->codecpar;
		const AVCodec* dec = avcodec_find_decoder(par->codec_id);
		AVCodecContext* cc = dec ? avcodec_alloc_context3(dec) : nullptr;
		const bool decCtxReady = cc && avcodec_parameters_to_context(cc, par) >= 0;
		if (decCtxReady)
		{
			// Latency-critical decoder setup, applied BEFORE avcodec_open2:
			//  - LOW_DELAY: emit each frame as soon as it is decoded (for streams
			//    without B-frames; if the camera encodes B-frames, that reordering
			//    latency is inherent and can only be removed encoder-side).
			//  - SLICE threading instead of the default FRAME threading, which would
			//    otherwise delay output by thread_count frames — the single biggest
			//    avoidable latency in the decode path.
			//  - thread_count = 0 (auto, one per core): libavcodec's default is 1, which
			//    left slice threading with no parallelism at all. Slice threads add no
			//    latency; they only help streams encoded with multiple slices per frame,
			//    and are irrelevant to hardware (d3d11va) decode.
			cc->flags |= AV_CODEC_FLAG_LOW_DELAY;
			cc->thread_type = FF_THREAD_SLICE;
			cc->thread_count = 0;
			if (hwDeviceCtx)
			{
				// Attach the GPU device + hw-format selector so the decoder uses DXVA.
				cc->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
				cc->get_format = SelectHwFormat;
			}
		}
		if (!dec || !decCtxReady || avcodec_open2(cc, dec, nullptr) < 0)
		{
			DebugLog("FfmpegRtspSource::DecodeLoop - failed to open decoder");
			if (cc) avcodec_free_context(&cc);
			avformat_close_input(&fmt);
			NoteConnectionLost(AVERROR_DECODER_NOT_FOUND, false);
			SleepInterruptible(kReconnectDelayMs);
			continue;
		}

		AVPacket* pkt = av_packet_alloc();
		AVFrame* frame = av_frame_alloc();
		SwsContext* sws = nullptr;

		// --- Latency cap ---------------------------------------------------------
		// If software decode can't keep up with the camera's frame rate, the demuxer
		// backlog (and thus end-to-end latency) grows without bound. We anchor a
		// wall-clock ↔ stream-PTS baseline and, whenever a decoded frame falls more
		// than kMaxLagMs behind where it "should" be, we catch up in two levels:
		//  1. Catch-up: keep decoding but stop presenting (no scale/copy/sink) and let
		//     the decoder skip non-reference frames (AVDISCARD_NONREF), so the backlog
		//     drains faster than real time while the picture just holds its last frame
		//     briefly. Exits with hysteresis once lag <= kMaxLagMs / 2.
		//  2. Keyframe resync (fallback): if catch-up lasts longer than
		//     kCatchUpMaxMs or the lag keeps growing (decode itself can't keep up),
		//     drop the rest of the GOP (skip packets until the next keyframe) and
		//     flush the decoder. Costs a freeze until the next keyframe (the whole GOP,
		//     2-4 s on many cameras), which is why it is only the fallback.
		// Keeps latency bounded on both TCP and UDP.
		const int64_t kMaxLagMs = MaxLagMs();
		constexpr ULONGLONG kCatchUpMaxMs = 1000;
		const AVRational streamTb = fmt->streams[vs]->time_base;
		const AVRational msTb{ 1, 1000 };
		bool haveClockBase = false;
		int64_t ptsBaseMs = 0;
		ULONGLONG wallBaseMs = 0;
		bool skipToKeyframe = false;
		bool catchingUp = false;        // level 1 active: decode but don't present
		ULONGLONG catchUpStartMs = 0;   // when level 1 started
		int64_t catchUpStartLagMs = 0;  // lag at entry, to detect a lag that keeps growing

		// Received-bitrate meter: sum the demuxed video packet sizes over a ~1s window
		// and publish bytes*8/elapsed. This is the real throughput (what the SDP omits).
		int64_t   bitrateWindowBytes = 0;
		ULONGLONG bitrateWindowStart = GetTickCount64();

		// Decoded-frame stall detector: packets may keep arriving while the decoder
		// emits nothing (e.g. UDP losing every keyframe, or a source that only sends
		// RTCP). Treat "no decoded frame for 2× the socket timeout" as a dead stream.
		const ULONGLONG frameStallMs = (ULONGLONG)socketTimeoutMs * 2;
		ULONGLONG lastFrameTick = GetTickCount64();
		int lossError = 0; // the error that ended this connection (0 = Stop())

		// Inner loop: decode until the stream drops, errors, or we are stopped.
		while (!_stop.load())
		{
			// Watchdog for this read: if nothing arrives within the socket timeout the
			// interrupt callback aborts av_read_frame with AVERROR_EXIT and we reconnect.
			ArmWatchdog(socketTimeoutMs);
			r = av_read_frame(fmt, pkt);
			if (r < 0)
			{
				if (_stop.load()) break;
				LogAv(r == AVERROR_EXIT ? "av_read_frame (watchdog: no data within socket timeout)"
				                        : "av_read_frame (stream ended / broke)", r);
				lossError = (r == AVERROR_EXIT) ? AVERROR(ETIMEDOUT) : r;
				break; // reconnect
			}
			if (GetTickCount64() - lastFrameTick > frameStallMs)
			{
				DebugLog("FfmpegRtspSource::DecodeLoop - packets but no decoded frame within stall window; reconnecting");
				av_packet_unref(pkt);
				lossError = AVERROR(ETIMEDOUT);
				break; // reconnect
			}
			if (pkt->stream_index != vs)
			{
				av_packet_unref(pkt);
				continue;
			}

			// Bitrate meter: count this video packet, publish once per ~1s window.
			bitrateWindowBytes += pkt->size;
			{
				const ULONGLONG bnow = GetTickCount64();
				const ULONGLONG belapsed = bnow - bitrateWindowStart;
				if (belapsed >= 1000)
				{
					_bitrateBps.store((int64_t)(bitrateWindowBytes * 8 * 1000 / (int64_t)belapsed));
					bitrateWindowBytes = 0;
					bitrateWindowStart = bnow;
				}
			}

			// Resyncing to live: discard everything until the next keyframe, then flush.
			if (skipToKeyframe)
			{
				if (pkt->flags & AV_PKT_FLAG_KEY)
				{
					skipToKeyframe = false;
					avcodec_flush_buffers(cc);
					haveClockBase = false; // re-anchor the clock at the new live position
					DebugLog("FfmpegRtspSource::DecodeLoop - keyframe reached; resynced to live");
				}
				else
				{
					av_packet_unref(pkt);
					continue;
				}
			}

			if (avcodec_send_packet(cc, pkt) == 0)
			{
				while (avcodec_receive_frame(cc, frame) == 0)
				{
					// How far is this frame behind real time? (best-effort PTS.)
					int64_t pts = frame->best_effort_timestamp;
					if (pts == AV_NOPTS_VALUE) pts = frame->pts;
					const ULONGLONG nowMs = GetTickCount64();
					bool haveLag = false;
					int64_t lagMs = 0;
					if (pts != AV_NOPTS_VALUE)
					{
						const int64_t ptsMs = av_rescale_q(pts, streamTb, msTb);
						if (!haveClockBase) { haveClockBase = true; ptsBaseMs = ptsMs; wallBaseMs = nowMs; }
						const int64_t expectedMs = (int64_t)wallBaseMs + (ptsMs - ptsBaseMs);
						lagMs = (int64_t)nowMs - expectedMs;
						_lastLagMs.store(lagMs);
						haveLag = true;
					}

					// Level 1 entry: too far behind → stop presenting, skip non-ref frames.
					if (!catchingUp && haveLag && lagMs > kMaxLagMs)
					{
						catchingUp = true;
						catchUpStartMs = nowMs;
						catchUpStartLagMs = lagMs;
						cc->skip_frame = AVDISCARD_NONREF;
						std::ostringstream oss;
						oss << "FfmpegRtspSource::DecodeLoop - lag " << lagMs << " ms > cap "
						    << kMaxLagMs << " ms; catching up (not presenting)";
						DebugLog(oss.str().c_str());
					}

					if (catchingUp)
					{
						if (haveLag && lagMs <= kMaxLagMs / 2)
						{
							// Caught up (hysteresis): present this frame and resume normally.
							catchingUp = false;
							cc->skip_frame = AVDISCARD_DEFAULT;
							std::ostringstream oss;
							oss << "FfmpegRtspSource::DecodeLoop - caught up in " << (nowMs - catchUpStartMs)
							    << " ms (lag " << lagMs << " ms)";
							DebugLog(oss.str().c_str());
						}
						else if (nowMs - catchUpStartMs > kCatchUpMaxMs ||
							(haveLag && lagMs > catchUpStartLagMs + kMaxLagMs))
						{
							// Level 2: catch-up isn't winning (too long, or the lag keeps
							// growing) → resync to the next keyframe instead of publishing
							// stale frames.
							catchingUp = false;
							cc->skip_frame = AVDISCARD_DEFAULT;
							skipToKeyframe = true;
							std::ostringstream oss;
							oss << "FfmpegRtspSource::DecodeLoop - catch-up failed after " << (nowMs - catchUpStartMs)
							    << " ms (lag " << lagMs << " ms); resyncing to the next keyframe";
							DebugLog(oss.str().c_str());
							av_frame_unref(frame);
							break;
						}
						else
						{
							// Still catching up: decoded but not presented. It still counts
							// as a live frame for the stall detector, or a long catch-up
							// would trigger a spurious reconnect.
							lastFrameTick = nowMs;
							av_frame_unref(frame);
							continue;
						}
					}

					const bool isHw = (frame->format == AV_PIX_FMT_D3D11);
					_hwActive.store(isHw);

					// Hardware frames come back as a D3D11 GPU surface — download to
					// system-memory (swFrame) before scaling. Software frames used as-is.
					// Returns nullptr if the download failed.
					auto toSystemMemory = [&]() -> AVFrame*
					{
						if (!isHw)
							return frame;
						int tr = av_hwframe_transfer_data(swFrame, frame, 0);
						if (tr < 0)
						{
							LogAv("av_hwframe_transfer_data", tr);
							return nullptr;
						}
						return swFrame;
					};
					// Converts/scales src into the NV12 planes dst/dstLs (4-entry arrays:
					// sws_scale reads all four). sws_getCachedContext recreates the scaler
					// if the source dimensions/format change; otherwise it reuses it.
					// SWS_FAST_BILINEAR: the flag only matters when actually scaling; at
					// equal size it is a plain format conversion either way.
					auto scaleInto = [&](const AVFrame* src, uint8_t* const dst[4], const int dstLs[4]) -> bool
					{
						sws = sws_getCachedContext(sws,
							src->width, src->height, (AVPixelFormat)src->format,
							(int)targetW, (int)targetH, AV_PIX_FMT_NV12,
							SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
						if (!sws)
							return false;
						sws_scale(sws, src->data, src->linesize, 0, src->height, dst, dstLs);
						return true;
					};

					bool produced = false; // frame reached its destination (counts as decoded)
					if (directTarget)
					{
						// Producer: write straight into the caller's slot, no intermediate
						// NV12 buffer and no extra memcpy.
						uint8_t* dst[4] = {};
						int dstLs[4] = {};
						if (!_target.acquire(targetW, targetH, dst, dstLs))
						{
							// Destination not ready (mapping not open yet / geometry
							// mismatch): drop the frame, but it was decoded fine.
							produced = true;
						}
						else
						{
							bool ok = false;
							// GPU frame already NV12 at the target size: download the
							// texture straight into the slot. av_hwframe_transfer_data only
							// uses our planes if dst->buf[0] is set (otherwise it allocates
							// its own) — hence the non-owning buffer ref (no-op free). With
							// it, the d3d11va path av_image_copy2()s staging → slot
							// (libavutil 8.1 hwcontext.c / hwcontext_d3d11va.c).
							const AVHWFramesContext* hwfc = (isHw && frame->hw_frames_ctx)
								? (const AVHWFramesContext*)frame->hw_frames_ctx->data : nullptr;
							if (hwfc && targetFrame && hwfc->sw_format == AV_PIX_FMT_NV12 &&
								frame->width == (int)targetW && frame->height == (int)targetH)
							{
								const size_t slotBytes = (size_t)dstLs[0] * targetH * 3 / 2;
								targetFrame->format = AV_PIX_FMT_NV12;
								targetFrame->width = (int)targetW;
								targetFrame->height = (int)targetH;
								targetFrame->data[0] = dst[0];
								targetFrame->data[1] = dst[1];
								targetFrame->linesize[0] = dstLs[0];
								targetFrame->linesize[1] = dstLs[1];
								targetFrame->buf[0] = av_buffer_create(dst[0], slotBytes,
									[](void*, uint8_t*) {}, nullptr, 0);
								if (targetFrame->buf[0])
								{
									int tr = av_hwframe_transfer_data(targetFrame, frame, 0);
									if (tr < 0)
										LogAv("av_hwframe_transfer_data (direct to slot)", tr);
									ok = tr >= 0;
								}
								av_frame_unref(targetFrame); // drops the non-owning ref; resets fields
							}
							else if (AVFrame* src = toSystemMemory())
							{
								// Software, other pixel format, or scaling: sws_scale
								// directly into the slot.
								ok = scaleInto(src, dst, dstLs);
							}
							_target.release(ok);
							produced = ok;
						}
					}
					else if (AVFrame* src = toSystemMemory())
					{
						// Preview: intermediate NV12 buffer handed to the sink.
						if (scaleInto(src, nv12->data, nv12->linesize))
						{
							if (_sink)
								_sink(nv12->data[0], nv12->linesize[0],
									nv12->data[1], nv12->linesize[1], targetW, targetH);
							produced = true;
						}
					}

					if (produced)
					{
						_framesDecoded.fetch_add(1);
						lastFrameTick = GetTickCount64();
						// First frame of this attempt: the transport actually carries
						// video, so publish it and stop the Auto UDP↔TCP alternation.
						if (!lastAttemptGotFrame)
						{
							lastAttemptGotFrame = true;
							_activeTransport.store((int)attemptTransport);
							_connectAttempt.store(0);
							_connState.store((int)ConnectionState::Streaming);
							DebugLog("FfmpegRtspSource::DecodeLoop - streaming (first frame received)");
						}
					}
					av_frame_unref(swFrame); // no-op unless a GPU download filled it
					av_frame_unref(frame);
				}
			}
			av_packet_unref(pkt);
		}

		if (sws) sws_freeContext(sws);
		av_frame_free(&frame);
		av_packet_free(&pkt);
		avcodec_free_context(&cc);
		DisarmWatchdog();
		avformat_close_input(&fmt);

		// Connection dropped (or Stop()): publish it so the UI can show "connection
		// lost, retrying" and the Frame Server side sees the heartbeat go stale.
		if (!_stop.load())
		{
			NoteConnectionLost(lossError, lastAttemptGotFrame);
			SleepInterruptible(kReconnectDelayMs); // brief pause before reconnecting
		}
	}

	av_frame_free(&swFrame);
	av_frame_free(&targetFrame);
	if (hwDeviceCtx)
		av_buffer_unref(&hwDeviceCtx);
	if (nv12)
	{
		av_freep(&nv12->data[0]);
		av_frame_free(&nv12);
	}
	DebugLog("FfmpegRtspSource::DecodeLoop - exited");
}
