package com.screenrec;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.MediaRecorder;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Environment;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class RecordService extends Service {

    private static final String TAG = "ScreenRec";
    private static final String CHANNEL_ID = "screenrec_channel";
    private static final int NOTIF_ID = 9001;

    public static final String ACTION_STOP = "com.screenrec.STOP";

    private static volatile boolean sRunning = false;
    private static long sStartTime = 0;

    private MediaProjection projection;
    private VirtualDisplay virtualDisplay;
    private MediaRecorder recorder;
    private String outputPath;

    public static boolean isRunning() { return sRunning; }
    public static long getStartTime() { return sStartTime; }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotifChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            stopSelf();
            return START_NOT_STICKY;
        }

        if (ACTION_STOP.equals(intent.getAction()) || intent.getBooleanExtra("stop", false)) {
            stopRecording();
            return START_NOT_STICKY;
        }

        if (sRunning) return START_NOT_STICKY;

        int code    = intent.getIntExtra("code", -1);
        Intent data = intent.getParcelableExtra("data");
        int width   = intent.getIntExtra("width", 1280);
        int height  = intent.getIntExtra("height", 720);
        int dpi     = intent.getIntExtra("dpi", 320);
        int bitrate = intent.getIntExtra("bitrate", 8_000_000);

        if (code == -1 || data == null) {
            Log.e(TAG, "Missing projection data");
            stopSelf();
            return START_NOT_STICKY;
        }

        // MUST call startForeground BEFORE getMediaProjection on Android 10+
        Notification notif = buildNotification(true);
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(NOTIF_ID, notif,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION);
            } else {
                startForeground(NOTIF_ID, notif);
            }
        } catch (Exception e) {
            Log.e(TAG, "startForeground failed", e);
            stopSelf();
            return START_NOT_STICKY;
        }

        MediaProjectionManager mgr = (MediaProjectionManager)
                getSystemService(MEDIA_PROJECTION_SERVICE);

        try {
            projection = mgr.getMediaProjection(code, data);
        } catch (Exception e) {
            Log.e(TAG, "getMediaProjection failed", e);
            stopSelf();
            return START_NOT_STICKY;
        }

        if (projection == null) {
            Log.e(TAG, "MediaProjection is null");
            stopSelf();
            return START_NOT_STICKY;
        }

        projection.registerCallback(new MediaProjection.Callback() {
            @Override
            public void onStop() {
                Log.i(TAG, "MediaProjection stopped by system");
                stopRecording();
            }
        }, null);

        if (!startRecording(width, height, dpi, bitrate)) {
            Log.e(TAG, "startRecording failed");
            cleanup();
            stopForeground(true);
            stopSelf();
            return START_NOT_STICKY;
        }

        sRunning = true;
        sStartTime = SystemClock.elapsedRealtime();
        Log.i(TAG, "Recording active: " + outputPath);

        return START_NOT_STICKY;
    }

    private boolean startRecording(int w, int h, int dpi, int bitrate) {
        outputPath = getOutputPath();
        if (outputPath == null) return false;

        try {
            if (Build.VERSION.SDK_INT >= 31) {
                recorder = new MediaRecorder(this);
            } else {
                recorder = new MediaRecorder();
            }

            recorder.setVideoSource(MediaRecorder.VideoSource.SURFACE);
            recorder.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4);
            recorder.setVideoEncoder(MediaRecorder.VideoEncoder.H264);
            recorder.setVideoSize(w, h);
            recorder.setVideoFrameRate(30);
            recorder.setVideoEncodingBitRate(bitrate);
            recorder.setOutputFile(outputPath);
            recorder.prepare();

            virtualDisplay = projection.createVirtualDisplay(
                    "ScreenRec", w, h, dpi,
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                    recorder.getSurface(), null, null);

            recorder.start();
            return true;

        } catch (Exception e) {
            Log.e(TAG, "MediaRecorder setup failed", e);
            return false;
        }
    }

    private void stopRecording() {
        if (!sRunning) {
            stopForeground(true);
            stopSelf();
            return;
        }
        sRunning = false;

        String saved = outputPath;

        try {
            if (recorder != null) recorder.stop();
        } catch (Exception e) {
            Log.w(TAG, "recorder.stop() error", e);
        }

        cleanup();

        if (saved != null) {
            File f = new File(saved);
            if (f.exists() && f.length() > 0) {
                long kb = f.length() / 1024;
                Log.i(TAG, "Saved: " + saved + " (" + kb + " KB)");
                try {
                    Toast.makeText(this, "Saved: " + f.getName() + " (" + kb + " KB)",
                            Toast.LENGTH_LONG).show();
                } catch (Exception ignored) {}
            }
        }

        stopForeground(true);
        stopSelf();
    }

    private void cleanup() {
        if (virtualDisplay != null) {
            try { virtualDisplay.release(); } catch (Exception ignored) {}
            virtualDisplay = null;
        }
        if (recorder != null) {
            try { recorder.reset(); } catch (Exception ignored) {}
            try { recorder.release(); } catch (Exception ignored) {}
            recorder = null;
        }
        if (projection != null) {
            try { projection.stop(); } catch (Exception ignored) {}
            projection = null;
        }
    }

    @Override
    public void onDestroy() {
        if (sRunning) stopRecording();
        super.onDestroy();
    }

    private String getOutputPath() {
        File dir;
        if (Build.VERSION.SDK_INT >= 29) {
            dir = new File(getExternalFilesDir(Environment.DIRECTORY_MOVIES), "ScreenRec");
        } else {
            dir = new File(Environment.getExternalStoragePublicDirectory(
                    Environment.DIRECTORY_MOVIES), "ScreenRec");
        }
        if (!dir.exists() && !dir.mkdirs()) {
            Log.e(TAG, "Cannot create output dir: " + dir);
            return null;
        }
        String ts = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
        return new File(dir, "rec_" + ts + ".mp4").getAbsolutePath();
    }

    private void createNotifChannel() {
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel ch = new NotificationChannel(
                    CHANNEL_ID, "Screen Recording",
                    NotificationManager.IMPORTANCE_LOW);
            ch.setDescription("Shows while screen recording is active");
            ch.setShowBadge(false);
            NotificationManager nm = getSystemService(NotificationManager.class);
            if (nm != null) nm.createNotificationChannel(ch);
        }
    }

    private Notification buildNotification(boolean recording) {
        Intent stopIntent = new Intent(this, RecordService.class);
        stopIntent.setAction(ACTION_STOP);

        int piFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) piFlags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent stopPi = PendingIntent.getService(this, 0, stopIntent, piFlags);

        Intent openIntent = new Intent(this, MainActivity.class);
        openIntent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent openPi = PendingIntent.getActivity(this, 1, openIntent, piFlags);

        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            b = new Notification.Builder(this, CHANNEL_ID);
        } else {
            b = new Notification.Builder(this);
        }

        b.setContentTitle("REC ● Screen Recording")
         .setContentText("Recording in progress — tap STOP to finish")
         .setSmallIcon(android.R.drawable.ic_btn_speak_now)
         .setContentIntent(openPi)
         .setOngoing(true)
         .setOnlyAlertOnce(true);

        if (recording && Build.VERSION.SDK_INT >= 24) {
            b.setUsesChronometer(true)
             .setWhen(System.currentTimeMillis());
        }

        if (Build.VERSION.SDK_INT >= 20) {
            b.addAction(new Notification.Action.Builder(
                    null, "⏹ STOP", stopPi).build());
        }

        return b.build();
    }
}
