package com.screenrec;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.RadioGroup;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity {

    private static final int REQ_PROJECTION = 1000;
    private static final int REQ_NOTIFY     = 1001;

    private MediaProjectionManager projMgr;
    private Button btnStart, btnStop;
    private TextView tvStatus, tvTimer;
    private RadioGroup rgQuality;

    private int screenDpi;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable timerRunnable = new Runnable() {
        @Override
        public void run() {
            if (RecordService.isRunning()) {
                long elapsed = SystemClock.elapsedRealtime() - RecordService.getStartTime();
                int secs = (int)(elapsed / 1000);
                int m = secs / 60, s = secs % 60;
                tvTimer.setText(String.format("%02d:%02d", m, s));
                tvTimer.setVisibility(View.VISIBLE);
                handler.postDelayed(this, 1000);
            } else {
                tvTimer.setVisibility(View.GONE);
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        btnStart  = findViewById(R.id.btn_start);
        btnStop   = findViewById(R.id.btn_stop);
        tvStatus  = findViewById(R.id.tv_status);
        tvTimer   = findViewById(R.id.tv_timer);
        rgQuality = findViewById(R.id.rg_quality);

        projMgr = (MediaProjectionManager) getSystemService(Context.MEDIA_PROJECTION_SERVICE);

        DisplayMetrics dm = new DisplayMetrics();
        ((WindowManager) getSystemService(WINDOW_SERVICE))
                .getDefaultDisplay().getMetrics(dm);
        screenDpi = dm.densityDpi;

        btnStart.setOnClickListener(v -> requestRecording());
        btnStop.setOnClickListener(v -> stopRecording());
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateUI(RecordService.isRunning());
    }

    @Override
    protected void onPause() {
        super.onPause();
        handler.removeCallbacks(timerRunnable);
    }

    private void requestRecording() {
        if (Build.VERSION.SDK_INT >= 33) {
            if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(
                        new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTIFY);
                return;
            }
        }
        startProjectionRequest();
    }

    private void startProjectionRequest() {
        try {
            startActivityForResult(projMgr.createScreenCaptureIntent(), REQ_PROJECTION);
        } catch (Exception e) {
            Toast.makeText(this, "Screen capture not supported", Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    public void onRequestPermissionsResult(int req, String[] perms, int[] res) {
        if (req == REQ_NOTIFY) {
            startProjectionRequest();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQ_PROJECTION) return;
        if (resultCode != RESULT_OK || data == null) {
            Toast.makeText(this, "Permission denied", Toast.LENGTH_SHORT).show();
            return;
        }

        int[] res = getSelectedResolution();
        int bitrate = getSelectedBitrate();

        Intent svc = new Intent(this, RecordService.class);
        svc.putExtra("code", resultCode);
        svc.putExtra("data", data);
        svc.putExtra("width", res[0]);
        svc.putExtra("height", res[1]);
        svc.putExtra("dpi", screenDpi);
        svc.putExtra("bitrate", bitrate);

        try {
            if (Build.VERSION.SDK_INT >= 26) {
                startForegroundService(svc);
            } else {
                startService(svc);
            }
            Toast.makeText(this, "Recording started!", Toast.LENGTH_SHORT).show();
            updateUI(true);

            // Go back to home so user can open the game
            handler.postDelayed(() -> moveTaskToBack(true), 300);

        } catch (Exception e) {
            Toast.makeText(this, "Failed to start: " + e.getMessage(),
                    Toast.LENGTH_LONG).show();
        }
    }

    private void stopRecording() {
        Intent svc = new Intent(this, RecordService.class);
        svc.setAction(RecordService.ACTION_STOP);
        startService(svc);

        handler.postDelayed(() -> updateUI(false), 500);
    }

    private void updateUI(boolean recording) {
        btnStart.setEnabled(!recording);
        btnStart.setAlpha(recording ? 0.4f : 1.0f);
        btnStop.setEnabled(recording);
        btnStop.setAlpha(recording ? 1.0f : 0.4f);

        if (recording) {
            tvStatus.setText("● REC");
            tvStatus.setTextColor(0xFFFF4444);
            handler.post(timerRunnable);
        } else {
            tvStatus.setText("READY");
            tvStatus.setTextColor(0xFF88FF88);
            tvTimer.setVisibility(View.GONE);
            handler.removeCallbacks(timerRunnable);
        }
    }

    private int[] getSelectedResolution() {
        int id = rgQuality.getCheckedRadioButtonId();
        if (id == R.id.rb_480)  return new int[]{854, 480};
        if (id == R.id.rb_1080) return new int[]{1920, 1080};
        return new int[]{1280, 720};
    }

    private int getSelectedBitrate() {
        int id = rgQuality.getCheckedRadioButtonId();
        if (id == R.id.rb_480)  return 4_000_000;
        if (id == R.id.rb_1080) return 12_000_000;
        return 8_000_000;
    }
}
