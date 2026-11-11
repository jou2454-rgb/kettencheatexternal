package com.dolp.riselauncher;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

// One-button launcher: copies rise_esp out of the APK, starts it as root in the background
// (it attaches to the game and draws the overlay) and opens the game. No adb, no terminal.
public class MainActivity extends Activity {
    private static final String GAME_PACKAGE = "com.SKITLSE.StandRise";
    private static final String REMOTE_BIN = "/data/local/tmp/rise_esp";
    private static final String REMOTE_LOG = "/data/local/tmp/rise_esp.log";

    private interface Task {
        void go() throws Exception;
    }

    private final Handler ui = new Handler(Looper.getMainLooper());
    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(48, 48, 48, 48);

        root.addView(button("ЗАПУСТИТЬ", new Task() {
            public void go() throws Exception { start(); }
        }));
        root.addView(button("ОСТАНОВИТЬ", new Task() {
            public void go() throws Exception { stop(); }
        }));
        root.addView(button("ПОКАЗАТЬ ЛОГ", new Task() {
            public void go() throws Exception { showLog(); }
        }));

        status = new TextView(this);
        status.setTextSize(16f);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(status);
        root.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);
        say("Игра " + GAME_PACKAGE + " должна быть установлена. Нажми ЗАПУСТИТЬ.");
    }

    private Button button(String text, final Task task) {
        Button b = new Button(this);
        b.setText(text);
        b.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                background(task);
            }
        });
        return b;
    }

    private void background(final Task task) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    task.go();
                } catch (Exception e) {
                    say("ошибка: " + e.getMessage());
                }
            }
        }).start();
    }

    private void start() throws Exception {
        say("копирую бинарник...");
        File bin = extractBinary();
        say("запускаю rise_esp под root...");
        su("cp '" + bin.getAbsolutePath() + "' " + REMOTE_BIN + " || exit 1; chmod 755 " + REMOTE_BIN
                + "; (killall rise_esp 2>/dev/null; true); nohup " + REMOTE_BIN + " " + GAME_PACKAGE
                + " > " + REMOTE_LOG + " 2>&1 < /dev/null &");
        Thread.sleep(1500);
        String pid = su("pidof rise_esp || echo none").trim();
        say("rise_esp запущен, pid: " + pid + ".\nоткрываю игру...");
        ui.post(new Runnable() {
            @Override
            public void run() {
                openGame();
            }
        });
    }

    private void openGame() {
        Intent launch = getPackageManager().getLaunchIntentForPackage(GAME_PACKAGE);
        if (launch == null) {
            say("игра " + GAME_PACKAGE + " не установлена");
            return;
        }
        startActivity(launch);
    }

    private void stop() throws Exception {
        su("killall rise_esp 2>/dev/null; true");
        say("остановлено");
    }

    private void showLog() throws Exception {
        String log = su("tail -n 200 " + REMOTE_LOG + " 2>&1; true");
        say(log.trim().isEmpty() ? "лог пуст или rise_esp ещё не запускался" : log);
    }

    private File extractBinary() throws Exception {
        File out = new File(getFilesDir(), "rise_esp");
        InputStream in = getAssets().open("rise_esp");
        try {
            OutputStream os = new FileOutputStream(out);
            try {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) {
                    os.write(buf, 0, n);
                }
            } finally {
                os.close();
            }
        } finally {
            in.close();
        }
        out.setExecutable(true, false);
        return out;
    }

    // Runs a shell command as root. Throws with the error text when the command fails.
    private String su(String cmd) throws Exception {
        Process p;
        try {
            p = Runtime.getRuntime().exec(new String[] {"su", "-c", cmd});
        } catch (IOException e) {
            throw new Exception("нет root (su). дай приложению root в Magisk или KernelSU");
        }
        p.getOutputStream().close();
        String out = readAll(p.getInputStream());
        String err = readAll(p.getErrorStream());
        int code = p.waitFor();
        if (code != 0) {
            throw new Exception("su код " + code + ": " + (err.isEmpty() ? out : err));
        }
        return out;
    }

    private static String readAll(InputStream in) throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        byte[] buf = new byte[4096];
        int n;
        while ((n = in.read(buf)) > 0) {
            bos.write(buf, 0, n);
        }
        in.close();
        return bos.toString("UTF-8");
    }

    private void say(final String text) {
        ui.post(new Runnable() {
            @Override
            public void run() {
                status.setText(text);
            }
        });
    }
}
