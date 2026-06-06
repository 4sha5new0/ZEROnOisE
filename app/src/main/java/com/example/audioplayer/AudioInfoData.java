package com.example.audioplayer;

/** C++ AudioInfo の Java ミラー。JNI から直接フィールドに書き込む。 */
public class AudioInfoData {
    public String  path          = "";
    public String  filename      = "";
    public String  title         = "";
    public String  artist        = "";
    public String  album         = "";
    public String  codecDetail   = "";
    public String  mqaEncoder    = "";

    public int     formatOrdinal = 0;   // AudioFormat 列挙値
    public int     sampleRate    = 44100;
    public int     bitDepth      = 16;
    public int     channels      = 2;
    public int     durationMs    = 0;
    public int     bitrateKbps   = 0;
    public int     mqaOriginalSr = 0;
    public long    totalSamples  = 0;
    public long    fileSizeBytes = 0;

    public boolean isLossless    = false;
    public boolean isHires       = false;
    public boolean isMqa         = false;
    public boolean isMqaStudio   = false;

    // ── 表示用ヘルパー ────────────────────────────────────────────────────────

    public String getFormatName() {
        switch (formatOrdinal) {
            case 1: return codecDetail.isEmpty() ? "FLAC" : "FLAC (" + codecDetail + ")";
            case 2: return "WAV";
            case 3: return "MP3 (lossy)";
            case 4: return "AAC (lossy)";
            case 5: return codecDetail.isEmpty() ? "WebM (lossy)" : "WebM/" + codecDetail;
            default: return "Unknown";
        }
    }

    public String getSampleRateLabel() {
        if (sampleRate >= 1000) return (sampleRate / 1000.0f) + "kHz";
        return sampleRate + "Hz";
    }

    /** サンプルレートの表示色クラス: "normal" / "hires" / "ultra" */
    public String getSrColorClass() {
        if (sampleRate >= 352800) return "ultra";
        if (sampleRate >= 88200)  return "hires";
        return "normal";
    }

    public String getBitDepthLabel() {
        if (!isLossless || bitDepth == 0) return "";
        return bitDepth + "bit";
    }

    /** ファイルサイズを人間が読みやすい形式で返す */
    public String getFileSizeLabel() {
        if (fileSizeBytes >= 1024 * 1024)
            return String.format("%.1f MB", fileSizeBytes / (1024.0 * 1024.0));
        if (fileSizeBytes >= 1024)
            return String.format("%.1f KB", fileSizeBytes / 1024.0);
        return fileSizeBytes + " B";
    }

    /** mm:ss 形式の再生時間 */
    public String getDurationLabel() {
        final int total_sec = durationMs / 1000;
        return String.format("%d:%02d", total_sec / 60, total_sec % 60);
    }

    /** MQA 表示文字列（バッジテキスト）*/
    public String getMqaBadgeText() {
        if (!isMqa) return null;
        if (isMqaStudio && mqaOriginalSr > 0)
            return "MQA STUDIO " + (mqaOriginalSr / 1000) + "kHz";
        return "MQA";
    }
}
