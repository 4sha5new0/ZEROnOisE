package com.example.audioplayer;

public class NativeBridge {

    static {
        System.loadLibrary("audioplayer");
    }

    // ── エンジンライフサイクル ────────────────────────────────────────────────
    public native long    nativeCreate();
    public native void    nativeDestroy(long handle);

    // ── 再生制御 ─────────────────────────────────────────────────────────────
    public native boolean nativeLoadFile(long handle, String path);
    public native void    nativePlay(long handle);
    public native void    nativePause(long handle);
    public native void    nativeStop(long handle);
    public native void    nativeSeek(long handle, long targetSample);
    public native void    nativeNextTrack(long handle);
    public native void    nativePrevTrack(long handle);

    // ── 状態取得 ─────────────────────────────────────────────────────────────
    public native long         nativeGetPosition(long handle);
    public native boolean      nativeIsPlaying(long handle);
    public native int          nativeGetStreamMode(long handle);  // 0=EXCLUSIVE,1=SHARED,2=NONE
    public native int          nativeGetUnderrunCount(long handle);
    public native AudioInfoData nativeGetCurrentInfo(long handle);
    /** float[8] = {rmsL, rmsR, peakL, peakR, holdL, holdR, clipL(0/1), clipR(0/1)} */
    public native float[]      nativeGetLevelMeter(long handle);

    // ── ファイルシステム ──────────────────────────────────────────────────────
    public native FileEntryData[] nativeScanDirectory(String dirPath);
    public native String[]        nativeGetStorageRoots();
    public native AudioInfoData   nativeReadMetadata(String path);
}
