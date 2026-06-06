package com.example.audioplayer;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.PowerManager;
import androidx.core.app.NotificationCompat;

public class PlayerService extends Service {

    public static final String ACTION_PLAY   = "com.example.audioplayer.PLAY";
    public static final String ACTION_PAUSE  = "com.example.audioplayer.PAUSE";
    public static final String ACTION_NEXT   = "com.example.audioplayer.NEXT";
    public static final String ACTION_PREV   = "com.example.audioplayer.PREV";
    public static final String ACTION_STOP   = "com.example.audioplayer.STOP";

    private static final String CHANNEL_ID = "playback";
    private static final int    NOTIF_ID   = 1;

    private final IBinder binder = new LocalBinder();
    private PowerManager.WakeLock wakeLock;

    // NativeBridge と engine ハンドルはサービスが保持する
    public  NativeBridge bridge;
    public  long         engineHandle = 0L;

    public class LocalBinder extends Binder {
        PlayerService getService() { return PlayerService.this; }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        bridge = new NativeBridge();
        engineHandle = bridge.nativeCreate();

        // WakeLock（PARTIAL: 画面消灯中も再生継続。最大 24h タイムアウト付き）
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,
            "ZEROnOisE::PlaybackWakeLock");
        wakeLock.acquire(24L * 60 * 60 * 1000);

        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && intent.getAction() != null) {
            switch (intent.getAction()) {
                case ACTION_PLAY:  bridge.nativePlay(engineHandle);  break;
                case ACTION_PAUSE: bridge.nativePause(engineHandle); break;
                case ACTION_NEXT:  bridge.nativeNextTrack(engineHandle); break;
                case ACTION_PREV:  bridge.nativePrevTrack(engineHandle); break;
                case ACTION_STOP:
                    bridge.nativeStop(engineHandle);
                    stopForeground(true);
                    stopSelf();
                    return START_NOT_STICKY;
            }
        }
        startForeground(NOTIF_ID, buildNotification("Ready", false));
        return START_STICKY;
    }

    /** 再生中通知を更新する（PlayerFragment から呼ぶ）*/
    public void updateNotification(String trackTitle, boolean isPlaying) {
        NotificationManager nm =
            (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        nm.notify(NOTIF_ID, buildNotification(trackTitle, isPlaying));
    }

    private Notification buildNotification(String title, boolean isPlaying) {
        Intent mainIntent = new Intent(this, MainActivity.class);
        mainIntent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent mainPi = PendingIntent.getActivity(this, 0, mainIntent,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        PendingIntent prevPi  = buildActionIntent(ACTION_PREV,  10);
        PendingIntent playPi  = buildActionIntent(isPlaying ? ACTION_PAUSE : ACTION_PLAY, 11);
        PendingIntent nextPi  = buildActionIntent(ACTION_NEXT,  12);

        return new NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle(title.isEmpty() ? getString(R.string.app_name) : title)
            .setContentText(isPlaying ? getString(R.string.notif_playing)
                                      : getString(R.string.notif_paused))
            .setContentIntent(mainPi)
            .addAction(android.R.drawable.ic_media_previous,
                getString(R.string.prev), prevPi)
            .addAction(isPlaying ? android.R.drawable.ic_media_pause
                                 : android.R.drawable.ic_media_play,
                isPlaying ? getString(R.string.pause) : getString(R.string.play), playPi)
            .addAction(android.R.drawable.ic_media_next,
                getString(R.string.next), nextPi)
            .setStyle(new androidx.media.app.NotificationCompat.MediaStyle()
                .setShowActionsInCompactView(0, 1, 2))
            .setOngoing(isPlaying)
            .setSilent(true)
            .build();
    }

    private PendingIntent buildActionIntent(String action, int requestCode) {
        Intent intent = new Intent(this, PlayerService.class);
        intent.setAction(action);
        return PendingIntent.getService(this, requestCode, intent,
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private void createNotificationChannel() {
        NotificationChannel ch = new NotificationChannel(
            CHANNEL_ID, "ZEROnOisE Playback", NotificationManager.IMPORTANCE_LOW);
        ch.setDescription("Audio playback controls");
        ch.setSound(null, null);
        NotificationManager nm =
            (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        nm.createNotificationChannel(ch);
    }

    @Override
    public IBinder onBind(Intent intent) { return binder; }

    @Override
    public void onDestroy() {
        if (engineHandle != 0L) {
            bridge.nativeStop(engineHandle);
            bridge.nativeDestroy(engineHandle);
            engineHandle = 0L;
        }
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        super.onDestroy();
    }
}
