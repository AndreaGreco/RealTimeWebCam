namespace RTVirtualCamera
{
    using System;

    public static class MFVideoSubtypeNames
    {
        // ===== RGB =====
        public static readonly Guid RGB8 = new Guid("E436EB7A-524F-11CE-9F53-0020AF0BA770");
        public static readonly Guid RGB555 = new Guid("E436EB7B-524F-11CE-9F53-0020AF0BA770");
        public static readonly Guid RGB565 = new Guid("E436EB7C-524F-11CE-9F53-0020AF0BA770");
        public static readonly Guid RGB24 = new Guid("E436EB7D-524F-11CE-9F53-0020AF0BA770");
        public static readonly Guid RGB32 = new Guid("00000016-0000-0010-8000-00AA00389B71");
        public static readonly Guid ARGB32 = new Guid("00000020-0000-0010-8000-00AA00389B71");

        // ===== YUV 4:2:0 =====
        public static readonly Guid NV12 = new Guid("3231564E-0000-0010-8000-00AA00389B71");
        public static readonly Guid NV21 = new Guid("3132564E-0000-0010-8000-00AA00389B71");
        public static readonly Guid I420 = new Guid("30323449-0000-0010-8000-00AA00389B71");
        public static readonly Guid YV12 = new Guid("32315659-0000-0010-8000-00AA00389B71");

        // ===== YUV 4:2:2 =====
        public static readonly Guid YUY2 = new Guid("32595559-0000-0010-8000-00AA00389B71");
        public static readonly Guid UYVY = new Guid("59565955-0000-0010-8000-00AA00389B71");
        public static readonly Guid YVYU = new Guid("55595659-0000-0010-8000-00AA00389B71");

        // ===== Encoded =====
        public static readonly Guid H264 = new Guid("34363248-0000-0010-8000-00AA00389B71");
        public static readonly Guid H264_ES = new Guid("31435648-0000-0010-8000-00AA00389B71");

        public static readonly Guid HEVC = new Guid("43564548-0000-0010-8000-00AA00389B71");
        public static readonly Guid HEVC_ES = new Guid("53564548-0000-0010-8000-00AA00389B71");

        public static readonly Guid MJPG = new Guid("47504A4D-0000-0010-8000-00AA00389B71");

        public static readonly Guid VP80 = new Guid("30385056-0000-0010-8000-00AA00389B71");
        public static readonly Guid VP90 = new Guid("30395056-0000-0010-8000-00AA00389B71");

        // ===== Helper =====
        public static string ToName(Guid g)
        {
            if (g == NV12) return "NV12";
            if (g == YUY2) return "YUY2";
            if (g == RGB32) return "RGB32";
            if (g == ARGB32) return "ARGB32";
            if (g == I420) return "I420";
            if (g == YV12) return "YV12";
            if (g == NV21) return "NV21";
            if (g == UYVY) return "UYVY";
            if (g == YVYU) return "YVYU";
            if (g == H264) return "H264";
            if (g == H264_ES) return "H264_ES";
            if (g == HEVC) return "HEVC";
            if (g == HEVC_ES) return "HEVC_ES";
            if (g == MJPG) return "MJPEG";
            if (g == VP80) return "VP8";
            if (g == VP90) return "VP9";

            return g.ToString();
        }
    }
}
