#include "FfmpegRtspSource.h"
#include "Logger.h"
#include <windows.h>
#include <sstream>

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

bool FfmpegRtspSource::Start(const std::wstring& rtspUrl, uint32_t targetWidth, uint32_t targetHeight,
                             uint32_t fpsNum, uint32_t fpsDen)
{
	(void)fpsNum; (void)fpsDen; // producer pushes ASAP; the Frame Server paces delivery
	if (_running.load() || rtspUrl.empty() || targetWidth == 0 || targetHeight == 0)
		return false;

	_stop.store(false);
	_framesDecoded.store(0);
	_hwActive.store(false);
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
	_writer.Close();
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
	// (swFrame) for the shared-memory channel — far cheaper than software decoding a
	// high-resolution stream, which is what let the latency grow unbounded. If the
	// device can't be created, hwDeviceCtx stays null and we decode in software.
	AVBufferRef* hwDeviceCtx = nullptr;
	if (av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) < 0)
	{
		hwDeviceCtx = nullptr;
		DebugLog("FfmpegRtspSource::DecodeLoop - d3d11va unavailable, falling back to software decode");
	}
	AVFrame* swFrame = av_frame_alloc(); // receives the GPU->CPU download

	// Outer loop: (re)connect until Stop(). One iteration == one connection attempt.
	while (!_stop.load())
	{
		AVFormatContext* fmt = avformat_alloc_context();
		if (!fmt) break;
		fmt->interrupt_callback.callback = &FfmpegRtspSource::InterruptCb;
		fmt->interrupt_callback.opaque = this;

		AVDictionary* opts = nullptr;
		av_dict_set(&opts, "rtsp_transport", "tcp", 0);       // TCP: reliable, no UDP reordering/loss
		av_dict_set(&opts, "stimeout", "5000000", 0);         // 5s socket timeout (microseconds)
		// --- Latency-critical demux options ---------------------------------------
		av_dict_set(&opts, "reorder_queue_size", "0", 0);     // disable the RTP jitter/reorder buffer (its wait is pure latency)
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
		const int64_t kMaxLagMs = 350;
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
						behind = ((int64_t)nowMs - expectedMs) > kMaxLagMs;
					}

					if (behind)
					{
						// Too far behind: resync to live instead of publishing stale frames.
						skipToKeyframe = true;
						av_frame_unref(frame);
						break;
					}

					// Hardware frames come back as a D3D11 GPU surface — download to
					// system-memory NV12 (swFrame) for the shared-memory channel.
					// Software frames are used as-is.
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
						if (_writer.EnsureOpen())
							_writer.WriteFrame(nv12->data[0], nv12->linesize[0],
								nv12->data[1], nv12->linesize[1]);
						_framesDecoded.fetch_add(1);
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

		if (!_stop.load()) Sleep(1000); // brief pause before reconnecting
	}

	av_frame_free(&swFrame);
	if (hwDeviceCtx)
		av_buffer_unref(&hwDeviceCtx);
	av_freep(&nv12->data[0]);
	av_frame_free(&nv12);
	DebugLog("FfmpegRtspSource::DecodeLoop - exited");
}
