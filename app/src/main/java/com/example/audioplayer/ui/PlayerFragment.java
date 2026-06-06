package com.example.audioplayer.ui;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageButton;
import android.widget.ProgressBar;
import android.widget.SeekBar;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import com.example.audioplayer.AudioInfoData;
import com.example.audioplayer.MainActivity;
import com.example.audioplayer.PlayerService;
import com.example.audioplayer.R;

public class PlayerFragment extends Fragment {

    // UI 更新インターバル: 100ms（10fps）
    private static final int UPDATE_INTERVAL_MS = 100;

    private PlayerService svc;

    // ── 再生情報 UI ───────────────────────────────────────────────────────────
    private TextView tvTitle, tvArtistAlbum;
    private TextView tvFormat, tvSampleRate, tvBitDepth;
    private TextView tvHiresBadge, tvMqaBadge, tvMqaSub;
    private TextView tvStreamMode;
    private TextView tvTime, tvDuration;

    // ── シークバー ────────────────────────────────────────────────────────────
    private SeekBar seekBar;
    private boolean userSeeking = false;

    // ── ボタン ────────────────────────────────────────────────────────────────
    private ImageButton btnPrev, btnPlayPause, btnNext;

    // ── レベルメーター ────────────────────────────────────────────────────────
    private LevelMeterView meterView;
    private TextView tvLevelL, tvLevelR;

    // ── ポーリング ────────────────────────────────────────────────────────────
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable updateRunnable = this::updateUI;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
            @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_player, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View root, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(root, savedInstanceState);

        tvTitle       = root.findViewById(R.id.tv_title);
        tvArtistAlbum = root.findViewById(R.id.tv_artist_album);
        tvFormat      = root.findViewById(R.id.tv_format);
        tvSampleRate  = root.findViewById(R.id.tv_sample_rate);
        tvBitDepth    = root.findViewById(R.id.tv_bit_depth);
        tvHiresBadge  = root.findViewById(R.id.tv_hires_badge);
        tvMqaBadge    = root.findViewById(R.id.tv_mqa_badge);
        tvMqaSub      = root.findViewById(R.id.tv_mqa_sub);
        tvStreamMode  = root.findViewById(R.id.tv_stream_mode);
        tvTime        = root.findViewById(R.id.tv_time);
        tvDuration    = root.findViewById(R.id.tv_duration);
        seekBar       = root.findViewById(R.id.seek_bar);
        btnPrev       = root.findViewById(R.id.btn_prev);
        btnPlayPause  = root.findViewById(R.id.btn_play_pause);
        btnNext       = root.findViewById(R.id.btn_next);
        meterView     = root.findViewById(R.id.level_meter_view);
        tvLevelL      = root.findViewById(R.id.tv_level_l);
        tvLevelR      = root.findViewById(R.id.tv_level_r);

        // 戻るボタン（ブラウザへ）
        root.findViewById(R.id.btn_back).setOnClickListener(v -> {
            if (getActivity() != null)
                getActivity().onBackPressed();
        });

        root.findViewById(R.id.btn_settings).setOnClickListener(v -> {
            // TODO: 設定画面
        });

        btnPrev.setOnClickListener(v -> {
            if (svc != null) svc.bridge.nativePrevTrack(svc.engineHandle);
        });
        btnPrev.setOnLongClickListener(v -> {
            if (svc != null) svc.bridge.nativeSeek(svc.engineHandle, 0L);
            return true;
        });
        btnPlayPause.setOnClickListener(v -> {
            if (svc == null) return;
            if (svc.bridge.nativeIsPlaying(svc.engineHandle))
                svc.bridge.nativePause(svc.engineHandle);
            else
                svc.bridge.nativePlay(svc.engineHandle);
        });
        btnNext.setOnClickListener(v -> {
            if (svc != null) svc.bridge.nativeNextTrack(svc.engineHandle);
        });

        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int progress, boolean fromUser) {
                if (fromUser && svc != null) {
                    final AudioInfoData info =
                        svc.bridge.nativeGetCurrentInfo(svc.engineHandle);
                    if (info != null && info.totalSamples > 0) {
                        final long target =
                            (long)(progress / 1000.0 * info.totalSamples);
                        tvTime.setText(formatMs((long)(progress / 1000.0 * info.durationMs)));
                    }
                }
            }
            @Override public void onStartTrackingTouch(SeekBar s) { userSeeking = true; }
            @Override public void onStopTrackingTouch(SeekBar s) {
                if (svc != null) {
                    final AudioInfoData info =
                        svc.bridge.nativeGetCurrentInfo(svc.engineHandle);
                    if (info != null && info.totalSamples > 0) {
                        final long target =
                            (long)(s.getProgress() / 1000.0 * info.totalSamples);
                        svc.bridge.nativeSeek(svc.engineHandle, target);
                    }
                }
                userSeeking = false;
            }
        });
    }

    @Override
    public void onResume() {
        super.onResume();
        if (getActivity() instanceof MainActivity)
            svc = ((MainActivity) getActivity()).service;
        handler.post(updateRunnable);
    }

    @Override
    public void onPause() {
        super.onPause();
        handler.removeCallbacks(updateRunnable);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // UI 更新（100ms 周期でポーリング）
    // ─────────────────────────────────────────────────────────────────────────
    private void updateUI() {
        if (svc == null || svc.engineHandle == 0L) {
            handler.postDelayed(updateRunnable, UPDATE_INTERVAL_MS);
            return;
        }

        final AudioInfoData info  = svc.bridge.nativeGetCurrentInfo(svc.engineHandle);
        final long          pos   = svc.bridge.nativeGetPosition(svc.engineHandle);
        final boolean       playing = svc.bridge.nativeIsPlaying(svc.engineHandle);
        final int           mode  = svc.bridge.nativeGetStreamMode(svc.engineHandle);
        final float[]       level = svc.bridge.nativeGetLevelMeter(svc.engineHandle);

        if (info != null) {
            // トラック情報
            final String displayTitle = info.title.isEmpty() ? info.filename : info.title;
            tvTitle.setText(displayTitle);
            final String artistAlbum  = info.artist.isEmpty() ? "" :
                info.artist + (info.album.isEmpty() ? "" : "  /  " + info.album);
            tvArtistAlbum.setText(artistAlbum);

            // 信号情報バー
            tvFormat.setText(info.getFormatName());
            tvSampleRate.setText(info.getSampleRateLabel());
            tvBitDepth.setText(info.getBitDepthLabel());
            tvHiresBadge.setVisibility(info.isHires ? View.VISIBLE : View.GONE);

            final String mqaBadge = info.getMqaBadgeText();
            tvMqaBadge.setVisibility(mqaBadge != null ? View.VISIBLE : View.GONE);
            if (mqaBadge != null) tvMqaBadge.setText(mqaBadge);
            tvMqaSub.setVisibility(info.isMqa ? View.VISIBLE : View.GONE);
            if (info.isMqa) {
                tvMqaSub.setText(info.isMqaStudio && info.mqaOriginalSr > 0
                    ? "→ " + (info.mqaOriginalSr / 1000) + "kHz (HW Render)"
                    : "HW Render via ES9219C");
            }

            // シークバー
            if (!userSeeking && info.totalSamples > 0) {
                final int progress = (int)(pos * 1000L / info.totalSamples);
                seekBar.setMax(1000);
                seekBar.setProgress(progress);
                final long pos_ms = (info.sampleRate > 0)
                    ? pos * 1000L / info.sampleRate : 0;
                tvTime.setText(formatMs(pos_ms));
                tvDuration.setText(info.getDurationLabel());
            }
        }

        // 再生ボタン切替
        btnPlayPause.setImageResource(playing
            ? android.R.drawable.ic_media_pause
            : android.R.drawable.ic_media_play);

        // ストリームモード表示
        switch (mode) {
            case 0: tvStreamMode.setText("EXCLUSIVE ✓"); tvStreamMode.setTextColor(0xFF4CAF50); break;
            case 1: tvStreamMode.setText("SHARED ⚠");   tvStreamMode.setTextColor(0xFFFFC107); break;
            default: tvStreamMode.setText("—"); break;
        }

        // レベルメーター更新
        if (level != null && level.length >= 8) {
            meterView.setLevels(level[0], level[1], level[2], level[3]);
            tvLevelL.setText(String.format("L  %.1f dBFS", level[2]));
            tvLevelR.setText(String.format("R  %.1f dBFS", level[3]));
        }

        // 通知更新
        if (info != null) {
            svc.updateNotification(
                info.title.isEmpty() ? info.filename : info.title, playing);
        }

        handler.postDelayed(updateRunnable, UPDATE_INTERVAL_MS);
    }

    private static String formatMs(long ms) {
        final long total_sec = ms / 1000;
        return String.format("%d:%02d", total_sec / 60, total_sec % 60);
    }
}
