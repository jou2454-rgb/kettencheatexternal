package com.screenrec;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Bundle;
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
    private TextView tvStatus;
    private RadioGroup rgQuality;

    private int screenW, screenH, screenDpi;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        btnStart  = findViewById(R.id.btn_start);
        btnStop   = findViewById(R.id.btn_stop);
        tvStatus  = findViewById(R.id.tv_status);
        rgQuality = findViewById(R.id.rg_quality);

        projMgr = (MediaProjectionManager) getSystemService(Context.MEDIA_PROJECTION_SERVICE);

        DisplayMetrics dm = new DisplayMetrics();
        ((WindowManager) getSystemService(WINDOW_SERVICE))
                .getDefaultDisplay().getMetrics(dm);
        screenW   = dm.widthPixels;
        screenH   = dm.heightPixels;
        screenDpi = dm.densityDpi;

        btnStart.setOnClickListener(v -> requestRecording());
        btnStop.setOnClickListener(v -> stopRecording());

        updateUI(RecordService.isRunning());
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateUI(RecordService.isRunning());
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
        startActivityForResult(projMgr.createScreenCaptureIntent(), REQ_PROJECTION);
    }

    @Override
    public void onRequestPermissionsResult(int req, String[] perms, int[] res) {
        if (req == REQ_NOTIFY) {
            startActivityForResult(projMgr.createScreenCaptureIntent(), REQ_PROJECTION);
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

        if (Build.VERSION.SDK_INT >= 26) {
            startForegroundService(svc);
        } else {
            startService(svc);
        }

        updateUI(true);
        Toast.makeText(this, "Recording started", Toast.LENGTH_SHORT).show();

        moveTaskToBack(true);
    }

    private void stopRecording() {
        Intent svc = new Intent(this, RecordService.class);
        svc.putExtra("stop", true);
        startService(svc);
        updateUI(false);
    }

    private void updateUI(boolean recording) {
        btnStart.setEnabled(!recording);
        btnStop.setEnabled(recording);
        tvStatus.setText(recording ? "REC" : "READY");
        tvStatus.setTextColor(recording ? 0xFFFF4444 : 0xFF88FF88);
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
