package com.example.audioplayer;

public class FileEntryData {
    public String       name        = "";
    public String       fullPath    = "";
    public boolean      isDirectory = false;
    public long         sizeBytes   = 0;
    public long         modifiedMs  = 0;
    public AudioInfoData audioInfo  = null;  // ファイルのみ。未読み込み時は null

    /** ブラウザのサブテキスト（例: "96kHz / 24bit" or "MP3 (lossy)"）*/
    public String getSubLabel() {
        if (isDirectory || audioInfo == null) return "";
        if (audioInfo.isLossless) {
            return audioInfo.getSampleRateLabel()
                + (audioInfo.bitDepth > 0 ? " / " + audioInfo.bitDepth + "bit" : "");
        }
        return audioInfo.getFormatName();
    }
}
