namespace RTVirtualCamera
{
    /// <summary>
    /// One row of help metadata for a tunable FFmpeg engine option. Single source of
    /// truth shared by the settings dialog (the "?" glyph tooltip) and the in-app guide
    /// window (<see cref="EngineGuideForm"/>), so the two never drift apart.
    ///
    /// The strings are resource KEYS resolved through <see cref="AppStrings"/> at display
    /// time (localized), not literal text:
    ///   - <see cref="TitleKey"/> reuses the option's own settings label.
    ///   - <see cref="TipKey"/> is the short one-liner shown on hover.
    ///   - <see cref="BodyKey"/> is the fuller explanation shown in the guide.
    /// <see cref="DocUrl"/> points at the relevant FFmpeg documentation (the guide opens
    /// it in the browser); it is empty when no single FFmpeg option maps to the setting.
    /// </summary>
    internal sealed class EngineOptionHelp
    {
        public EngineOptionHelp(string key, string titleKey, string tipKey, string bodyKey, string docUrl)
        {
            Key = key;
            TitleKey = titleKey;
            TipKey = tipKey;
            BodyKey = bodyKey;
            DocUrl = docUrl;
        }

        public string Key { get; }
        public string TitleKey { get; }
        public string TipKey { get; }
        public string BodyKey { get; }
        public string DocUrl { get; }

        // FFmpeg documentation anchors. RTSP/UDP demux options live in the protocols
        // page; max_delay is an AVFormatContext (format) option; hardware decode has no
        // single option (it is a device attached to the decoder), so it points at the
        // FFmpeg HWAccel wiki. Latency cap is our own resync-to-live, not an FFmpeg
        // option, so it has no external link.
        private const string DocRtsp = "https://ffmpeg.org/ffmpeg-protocols.html#rtsp";
        private const string DocUdp = "https://ffmpeg.org/ffmpeg-protocols.html#udp";
        private const string DocFormat = "https://ffmpeg.org/ffmpeg-formats.html#Format-Options";
        private const string DocHwAccel = "https://trac.ffmpeg.org/wiki/HWAccelIntro";

        /// <summary>
        /// The engine options in the order they appear in the settings dialog and guide.
        /// </summary>
        public static readonly EngineOptionHelp[] All =
        {
            new EngineOptionHelp("transport",     "Settings_Transport",      "OptHelp_transport_Tip",     "OptHelp_transport_Body",     DocRtsp),
            new EngineOptionHelp("hwdecode",      "Settings_HardwareDecode", "OptHelp_hwdecode_Tip",      "OptHelp_hwdecode_Body",      DocHwAccel),
            new EngineOptionHelp("sockettimeout", "Settings_SocketTimeout",  "OptHelp_sockettimeout_Tip", "OptHelp_sockettimeout_Body", DocRtsp),
            new EngineOptionHelp("reorder",       "Settings_ReorderQueue",   "OptHelp_reorder_Tip",       "OptHelp_reorder_Body",       DocRtsp),
            new EngineOptionHelp("buffersize",    "Settings_UdpBufferSize",  "OptHelp_buffersize_Tip",    "OptHelp_buffersize_Body",    DocUdp),
            new EngineOptionHelp("maxdelay",      "Settings_MaxDelay",       "OptHelp_maxdelay_Tip",      "OptHelp_maxdelay_Body",      DocFormat),
            new EngineOptionHelp("latencycap",    "Settings_LatencyCap",     "OptHelp_latencycap_Tip",    "OptHelp_latencycap_Body",    ""),
        };

        public static EngineOptionHelp Find(string key)
        {
            foreach (EngineOptionHelp h in All)
            {
                if (h.Key == key)
                {
                    return h;
                }
            }
            return null;
        }
    }
}
