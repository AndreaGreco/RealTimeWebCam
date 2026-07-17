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
	std::atomic<int>  g_reorderQueue{ 8 };
	std::atomic<int>  g_maxLagMs{ 350 };

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
	return (self && self->_stop.load()) ? 1 : 0;
}

void FfmpegRtspSource::SetTransportPreference(RtspTransport t) { g_transportPref.store((int)t); }
RtspTransport FfmpegRtspSource::TransportPreference() { return (RtspTransport)g_transportPref.load(); }
void FfmpegRtspSource::SetHardwareDecodeEnabled(bool enabled) { g_hwDecodeEnabled.store(enabled); }
bool FfmpegRtspSource::HardwareDecodeEnabled() { return g_hwDecodeEnabled.load(); }

void FfmpegRtspSource::SetSocketTimeoutMs(int ms) { g_socketTimeoutMs.store(ms > 0 ? ms : 1); }
int  FfmpegRtspSource::SocketTimeoutMs() { return g_socketTimeoutMs.load(); }
void FfmpegRtspSource::SetReorderQueueSize(int packets) { g_reorderQueue.store(packets < 0 ? 0 : packets); }
int  FfmpegRtspSource::ReorderQueueSize() { return g_reorderQueue.load(); }
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

	AVDictionary* opts = nullptr;
	// Honor the user's transport preference (Auto = UDP with TCP fallback).
	av_dict_set(&opts, "rtsp_transport", TransportOption(), 0);
	av_dict_set(&opts, "stimeout", "5000000", 0); // 5s socket timeout (microseconds)
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
	if (_running.load() || rtspUrl.empty() || targetWidth == 0 || targetHeight == 0 || !sink)
		return false;

	_stop.store(false);
	_framesDecoded.store(0);
	_hwActive.store(false);
	_lastLagMs.store(0);
	_activeTransport.store(0);
	_sink = std::move(sink);
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
	_sink = nullptr; // release anything the sink captured
	avformat_network_deinit();
}

void FfmpegRtspSource::DecodeLoop(std::string url, uint32_t targetW, uint32_t targetH)
{
	// NV12 destination frame, sized to the target once for the whole session.
	AVFrame* nv12 = av_frame_alloc();
	if (!nv12 ||
		av_image_alloc(nv12->data, nv12->linesize, (int)targetW, (int)targetH, AV_PIX_FMT_NV12, 32) < 0)
	{
		DebugLog("FfmpegRtspSource::DecodeLoop - failed to allocate NV12 frame");
		if (nv12) av_frame_free(&nv12);
		return;
	}

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

		AVFormatContext* fmt = avformat_alloc_context();
		if (!fmt) break;
		fmt->interrupt_callback.callback = &FfmpegRtspSource::InterruptCb;
		fmt->interrupt_callback.opaque = this;

		// Snapshot the user-tunable numeric options for this attempt (see the setters).
		const std::string stimeoutUs = std::to_string((int64_t)SocketTimeoutMs() * 1000);
		const std::string reorderQ   = std::to_string(ReorderQueueSize());

		AVDictionary* opts = nullptr;
		av_dict_set(&opts, "rtsp_transport", transportOpt, 0);
		av_dict_set(&opts, "stimeout", stimeoutUs.c_str(), 0); // socket timeout (microseconds)
		// --- Latency-critical demux options ---------------------------------------
		// RTP reorder buffer: on TCP packets never reorder (harmless), but on UDP a
		// value of 0 would drop every out-of-order packet and shred the picture. A few
		// slots tolerate minor UDP reordering at a negligible latency cost.
		av_dict_set(&opts, "reorder_queue_size", reorderQ.c_str(), 0);
		av_dict_set(&opts, "max_delay", "0", 0);              // no demux reorder delay
		av_dict_set(&opts, "fflags", "nobuffer+discardcorrupt", 0); // don't buffer input; drop corrupt frames instead of stalling
		av_dict_set(&opts, "flags", "low_delay", 0);          // ask the demuxer/codec for minimal delay
		av_dict_set(&opts, "avioflags", "direct", 0);         // no read-ahead buffering on the socket
		av_dict_set(&opts, "probesize", "500000", 0);         // small probe → faster startup (steady-state unaffected)
		av_dict_set(&opts, "analyzeduration", "0", 0);        // RTSP carries codec params in the SDP; skip the analyze wait

		int r = avformat_open_input(&fmt, url.c_str(), nullptr, &opts);
		av_dict_free(&opts);
		if (r < 0)
		{
			LogAv("avformat_open_input", r);
			if (fmt) avformat_free_context(fmt);
			if (!_stop.load()) Sleep(1000);
			continue;
		}

		if ((r = avformat_find_stream_info(fmt, nullptr)) < 0)
		{
			LogAv("avformat_find_stream_info", r);
			avformat_close_input(&fmt);
			if (!_stop.load()) Sleep(1000);
			continue;
		}

		int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (vs < 0)
		{
			DebugLog("FfmpegRtspSource::DecodeLoop - no video stream");
			avformat_close_input(&fmt);
			if (!_stop.load()) Sleep(1000);
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
			cc->flags |= AV_CODEC_FLAG_LOW_DELAY;
			cc->thread_type = FF_THREAD_SLICE;
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
			if (!_stop.load()) Sleep(1000);
			continue;
		}

		AVPacket* pkt = av_packet_alloc();
		AVFrame* frame = av_frame_alloc();
		SwsContext* sws = nullptr;

		// --- Latency cap ---------------------------------------------------------
		// If software decode can't keep up with the camera's frame rate, the demuxer
		// backlog (and thus end-to-end latency) grows without bound. We anchor a
		// wall-clock ↔ stream-PTS baseline and, whenever a decoded frame falls more
		// than kMaxLagMs behind where it "should" be, we resync to live: drop the
		// rest of the current GOP (skip packets until the next keyframe) and flush
		// the decoder. Costs a visible jump but keeps latency bounded on both TCP
		// and UDP.
		const int64_t kMaxLagMs = MaxLagMs();
		const AVRational streamTb = fmt->streams[vs]->time_base;
		const AVRational msTb{ 1, 1000 };
		bool haveClockBase = false;
		int64_t ptsBaseMs = 0;
		ULONGLONG wallBaseMs = 0;
		bool skipToKeyframe = false;

		// Inner loop: decode until the stream drops, errors, or we are stopped.
		while (!_stop.load())
		{
			r = av_read_frame(fmt, pkt);
			if (r < 0)
			{
				LogAv("av_read_frame (stream ended / broke)", r);
				break; // reconnect
			}
			if (pkt->stream_index != vs)
			{
				av_packet_unref(pkt);
				continue;
			}

			// Resyncing to live: discard everything until the next keyframe, then flush.
			if (skipToKeyframe)
			{
				if (pkt->flags & AV_PKT_FLAG_KEY)
				{
					skipToKeyframe = false;
					avcodec_flush_buffers(cc);
					haveClockBase = false; // re-anchor the clock at the new live position
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
					bool behind = false;
					if (pts != AV_NOPTS_VALUE)
					{
						const int64_t ptsMs = av_rescale_q(pts, streamTb, msTb);
						if (!haveClockBase) { haveClockBase = true; ptsBaseMs = ptsMs; wallBaseMs = nowMs; }
						const int64_t expectedMs = (int64_t)wallBaseMs + (ptsMs - ptsBaseMs);
						const int64_t lagMs = (int64_t)nowMs - expectedMs;
						_lastLagMs.store(lagMs);
						behind = lagMs > kMaxLagMs;
					}

					if (behind)
					{
						// Too far behind: resync to live instead of publishing stale frames.
						skipToKeyframe = true;
						av_frame_unref(frame);
						break;
					}

					// Hardware frames come back as a D3D11 GPU surface — download to
					// system-memory NV12 (swFrame) for the sink. Software frames used as-is.
					AVFrame* srcFrame = frame;
					if (frame->format == AV_PIX_FMT_D3D11)
					{
						_hwActive.store(true);
						int tr = av_hwframe_transfer_data(swFrame, frame, 0);
						if (tr < 0)
						{
							LogAv("av_hwframe_transfer_data", tr);
							av_frame_unref(frame);
							continue;
						}
						srcFrame = swFrame;
					}
					else
					{
						_hwActive.store(false);
					}

					// sws_getCachedContext recreates the scaler if the source
					// dimensions/format change; otherwise it reuses the existing one.
					sws = sws_getCachedContext(sws,
						srcFrame->width, srcFrame->height, (AVPixelFormat)srcFrame->format,
						(int)targetW, (int)targetH, AV_PIX_FMT_NV12,
						SWS_BILINEAR, nullptr, nullptr, nullptr);
					if (sws)
					{
						sws_scale(sws, srcFrame->data, srcFrame->linesize, 0, srcFrame->height,
							nv12->data, nv12->linesize);
						if (_sink)
							_sink(nv12->data[0], nv12->linesize[0],
								nv12->data[1], nv12->linesize[1], targetW, targetH);
						_framesDecoded.fetch_add(1);
						// First frame of this attempt: the transport actually carries
						// video, so publish it and stop the Auto UDP↔TCP alternation.
						if (!lastAttemptGotFrame)
						{
							lastAttemptGotFrame = true;
							_activeTransport.store((int)attemptTransport);
						}
					}
					if (srcFrame == swFrame)
						av_frame_unref(swFrame);
					av_frame_unref(frame);
				}
			}
			av_packet_unref(pkt);
		}

		if (sws) sws_freeContext(sws);
		av_frame_free(&frame);
		av_packet_free(&pkt);
		avcodec_free_context(&cc);
		avformat_close_input(&fmt);
		_activeTransport.store(0); // connection dropped — no live transport

		if (!_stop.load()) Sleep(1000); // brief pause before reconnecting
	}

	av_frame_free(&swFrame);
	if (hwDeviceCtx)
		av_buffer_unref(&hwDeviceCtx);
	av_freep(&nv12->data[0]);
	av_frame_free(&nv12);
	DebugLog("FfmpegRtspSource::DecodeLoop - exited");
}
