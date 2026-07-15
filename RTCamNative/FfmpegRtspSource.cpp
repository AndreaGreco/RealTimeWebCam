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

	// Outer loop: (re)connect until Stop(). One iteration == one connection attempt.
	while (!_stop.load())
	{
		AVFormatContext* fmt = avformat_alloc_context();
		if (!fmt) break;
		fmt->interrupt_callback.callback = &FfmpegRtspSource::InterruptCb;
		fmt->interrupt_callback.opaque = this;

		AVDictionary* opts = nullptr;
		av_dict_set(&opts, "rtsp_transport", "tcp", 0); // TCP: reliable, no UDP reordering
		av_dict_set(&opts, "stimeout", "5000000", 0);   // 5s socket timeout (microseconds)
		av_dict_set(&opts, "max_delay", "500000", 0);
		av_dict_set(&opts, "fflags", "nobuffer", 0);    // low latency

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
		if (!dec || !cc ||
			avcodec_parameters_to_context(cc, par) < 0 ||
			avcodec_open2(cc, dec, nullptr) < 0)
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

		// Inner loop: decode until the stream drops, errors, or we are stopped.
		while (!_stop.load())
		{
			r = av_read_frame(fmt, pkt);
			if (r < 0)
			{
				LogAv("av_read_frame (stream ended / broke)", r);
				break; // reconnect
			}
			if (pkt->stream_index == vs && avcodec_send_packet(cc, pkt) == 0)
			{
				while (avcodec_receive_frame(cc, frame) == 0)
				{
					// sws_getCachedContext recreates the scaler if the source
					// dimensions/format change; otherwise it reuses the existing one.
					sws = sws_getCachedContext(sws,
						frame->width, frame->height, (AVPixelFormat)frame->format,
						(int)targetW, (int)targetH, AV_PIX_FMT_NV12,
						SWS_BILINEAR, nullptr, nullptr, nullptr);
					if (sws)
					{
						sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
							nv12->data, nv12->linesize);
						if (_writer.EnsureOpen())
							_writer.WriteFrame(nv12->data[0], nv12->linesize[0],
								nv12->data[1], nv12->linesize[1]);
						_framesDecoded.fetch_add(1);
					}
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

	av_freep(&nv12->data[0]);
	av_frame_free(&nv12);
	DebugLog("FfmpegRtspSource::DecodeLoop - exited");
}
