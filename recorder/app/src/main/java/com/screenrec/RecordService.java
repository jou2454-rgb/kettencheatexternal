package com.screenrec;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.MediaRecorder;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Environment;
import android.os.IBinder;
import android.util.Log;

import java.io.File;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class RecordService extends Service {

    private static final String TAG = "ScreenRec";
    private static final String CHANNEL_ID = "rec_channel";
    private static final int NOTIF_ID = 9001;

    private static volatile boolean sRunning = false;

    private MediaProjection projection;
    private VirtualDisplay virtualDisplay;
    private MediaRecorder recorder;
    private String outputPath;

    public static boolean isRunning() { return sRunning; }

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

        if (intent.getBooleanExtra("stop", false)) {
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
            stopSelf();
            return START_NOT_STICKY;
        }

        startForeground(NOTIF_ID, buildNotification());

        MediaProjectionManager mgr = (MediaProjectionManager)
                getSystemService(MEDIA_PROJECTION_SERVICE);
        projection = mgr.getMediaProjection(code, data);

        if (projection == null) {
            Log.e(TAG, "MediaProjection is null");
            stopSelf();
            return START_NOT_STICKY;
        }

        projection.registerCallback(new MediaProjection.Callback() {
            @Override
            public void onStop() {
                stopRecording();
            }
        }, null);

        if (!startRecording(width, height, dpi, bitrate)) {
            stopSelf();
            return START_NOT_STICKY;
        }

        sRunning = true;
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
                    "ScreenRec",
                    w, h, dpi,
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                    recorder.getSurface(),
                    null, null);

            recorder.start();
            Log.i(TAG, "Recording started: " + outputPath);
            return true;

        } catch (Exception e) {
            Log.e(TAG, "startRecording failed", e);
            cleanup();
            return false;
        }
    }

    private void stopRecording() {
        if (!sRunning) return;
        sRunning = false;

        try {
            if (recorder != null) {
                recorder.stop();
                Log.i(TAG, "Saved: " + outputPath);
            }
        } catch (Exception e) {
            Log.w(TAG, "stop error (file may be short)", e);
        }

        cleanup();
        stopForeground(true);
        stopSelf();
    }

    private void cleanup() {
        if (virtualDisplay != null) {
            virtualDisplay.release();
            virtualDisplay = null;
        }
        if (recorder != null) {
            try { recorder.reset(); } catch (Exception ignored) {}
            recorder.release();
            recorder = null;
        }
        if (projection != null) {
            projection.stop();
            projection = null;
        }
    }

    @Override
    public void onDestroy() {
        stopRecording();
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
            Log.e(TAG, "Cannot create dir: " + dir);
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
            ch.setDescription("Active screen recording");
            getSystemService(NotificationManager.class).createNotificationChannel(ch);
        }
    }

    private Notification buildNotification() {
        Intent stopIntent = new Intent(this, RecordService.class);
        stopIntent.putExtra("stop", true);

        int piFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) piFlags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent stopPi = PendingIntent.getService(this, 0, stopIntent, piFlags);

        Intent openIntent = new Intent(this, MainActivity.class);
        PendingIntent openPi = PendingIntent.getActivity(this, 1, openIntent, piFlags);

        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            b = new Notification.Builder(this, CHANNEL_ID);
        } else {
            b = new Notification.Builder(this);
        }

        b.setContentTitle("Screen Recording")
         .setContentText("Tap to open | Recording in progress")
         .setSmallIcon(android.R.drawable.ic_media_play)
         .setContentIntent(openPi)
         .setOngoing(true);

        if (Build.VERSION.SDK_INT >= 20) {
            b.addAction(new Notification.Action.Builder(
                    null, "STOP", stopPi).build());
        }

        return b.build();
    }
}
