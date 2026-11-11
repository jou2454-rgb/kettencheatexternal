package local.launcher

import android.os.Bundle
import android.text.method.ScrollingMovementMethod
import android.view.Gravity
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.io.BufferedReader
import java.io.InputStreamReader

// Runs a binary that is already on the device, through su. No network, no downloads.
class MainActivity : AppCompatActivity() {

    private var process: Process? = null
    private lateinit var pathInput: EditText
    private lateinit var logView: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val pad = (16 * resources.displayMetrics.density).toInt()

        pathInput = EditText(this).apply {
            hint = "/data/local/tmp/binary"
            setSingleLine(true)
            setText(prefs().getString("path", "/data/local/tmp/bycmd-1.0.0"))
        }
        val startBtn = Button(this).apply { text = "Start" }
        val stopBtn = Button(this).apply { text = "Stop" }
        val clearBtn = Button(this).apply { text = "Clear log" }
        logView = TextView(this).apply {
            setTextIsSelectable(true)
            movementMethod = ScrollingMovementMethod()
            textSize = 11f
        }

        startBtn.setOnClickListener { start() }
        stopBtn.setOnClickListener { stop() }
        clearBtn.setOnClickListener { logView.text = "" }

        val buttons = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
            addView(startBtn, LinearLayout.LayoutParams(0, -2, 1f))
            addView(stopBtn, LinearLayout.LayoutParams(0, -2, 1f))
            addView(clearBtn, LinearLayout.LayoutParams(0, -2, 1f))
        }

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
            addView(pathInput, LinearLayout.LayoutParams(-1, -2))
            addView(buttons, LinearLayout.LayoutParams(-1, -2))
            addView(ScrollView(this@MainActivity).apply { addView(logView) },
                LinearLayout.LayoutParams(-1, 0, 1f))
        }
        setContentView(root)
        log("ready")
    }

    override fun onStop() {
        super.onStop()
        prefs().edit().putString("path", pathInput.text.toString().trim()).apply()
    }

    private fun start() {
        if (process != null) { log("already running"); return }
        val path = pathInput.text.toString().trim()
        if (path.isEmpty() || !path.startsWith("/")) { log("give an absolute path"); return }

        val cmd = "chmod +x '$path' && exec '$path'"
        try {
            val p = ProcessBuilder("su", "-c", cmd).redirectErrorStream(true).start()
            process = p
            log("started: $path")
            Thread {
                try {
                    BufferedReader(InputStreamReader(p.inputStream)).use { r ->
                        while (true) {
                            val line = r.readLine() ?: break
                            runOnUiThread { log(line) }
                        }
                    }
                } catch (_: Exception) { }
                val code = try { p.waitFor() } catch (_: Exception) { -1 }
                runOnUiThread {
                    if (process === p) process = null
                    log("exited: $code")
                }
            }.apply { isDaemon = true }.start()
        } catch (e: Exception) {
            log("start failed: ${e.message}")
        }
    }

    private fun stop() {
        val p = process ?: run { log("not running"); return }
        val name = pathInput.text.toString().trim().substringAfterLast('/')
        try {
            ProcessBuilder("su", "-c", "pkill -f '$name'").start().waitFor()
        } catch (e: Exception) {
            log("pkill failed: ${e.message}")
        }
        p.destroy()
        process = null
        log("stop sent")
    }

    private fun log(msg: String) {
        logView.append(msg + "\n")
    }

    private fun prefs() = getSharedPreferences("launcher", MODE_PRIVATE)
}
