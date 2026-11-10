package com.screenrec;

import android.app.Application;
import android.content.Intent;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;

public class App extends Application {

    private static final String CRASH_FILE = "last_crash.txt";

    public static int projCode = -1;
    public static Intent projData = null;
    public static long projSetAt = 0;

    public static void setProjection(int code, Intent data) {
        projCode = code;
        projData = data;
        projSetAt = System.currentTimeMillis();
        Log.i("ScreenRec", "setProjection: code=" + code
                + " data=" + (data != null)
                + " hash=" + (data != null ? System.identityHashCode(data) : 0));
    }

    public static Intent consumeProjection() {
        Intent d = projData;
        int c = projCode;
        long age = System.currentTimeMillis() - projSetAt;
        Log.i("ScreenRec", "consumeProjection: code=" + c
                + " data=" + (d != null)
                + " age=" + age + "ms"
                + " hash=" + (d != null ? System.identityHashCode(d) : 0));
        projCode = -1;
        projData = null;
        return d;
    }

    public static int consumeCode() {
        int c = projCode;
        return c;
    }

    @Override
    public void onCreate() {
        super.onCreate();

        final Thread.UncaughtExceptionHandler prev =
                Thread.getDefaultUncaughtExceptionHandler();

        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            try {
                StringWriter sw = new StringWriter();
                e.printStackTrace(new PrintWriter(sw));
                String trace = sw.toString();
                Log.e("ScreenRec", "CRASH: " + trace);

                File f = new File(getFilesDir(), CRASH_FILE);
                FileOutputStream fos = new FileOutputStream(f);
                fos.write(trace.getBytes(StandardCharsets.UTF_8));
                fos.close();
            } catch (Exception ignored) {}

            if (prev != null) prev.uncaughtException(t, e);
        });

        showLastCrash();
    }

    private void showLastCrash() {
        try {
            File f = new File(getFilesDir(), CRASH_FILE);
            if (!f.exists()) return;
            byte[] data = new byte[(int) Math.min(f.length(), 2048)];
            java.io.FileInputStream fis = new java.io.FileInputStream(f);
            int len = fis.read(data);
            fis.close();
            if (len > 0) {
                String msg = new String(data, 0, len, StandardCharsets.UTF_8);
                String firstLine = msg.split("\n")[0];
                Log.w("ScreenRec", "Previous crash: " + firstLine);
                Toast.makeText(this, "Last crash: " + firstLine, Toast.LENGTH_LONG).show();
            }
            f.delete();
        } catch (Exception ignored) {}
    }
}
