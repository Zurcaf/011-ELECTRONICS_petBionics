package com.example.petbionic

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import android.util.Log
import org.json.JSONObject

class MainActivity : AppCompatActivity() {

    companion object {
        private const val TIME_SYNC_RETRY_INTERVAL_MS = 5000L
        private const val STATUS_REFRESH_INTERVAL_MS = 2000L
        // Firmware default: 12500 µs = 80 Hz. Use rate to avoid integer-division drift.
        private const val DISPLAY_SAMPLE_RATE_HZ = 80.0
    }

    private lateinit var tvStatus: TextView
    private lateinit var tvSessionState: TextView
    private lateinit var tvSdState: TextView
    private lateinit var tvImuState: TextView
    private lateinit var tvHx711State: TextView
    private lateinit var tvSamplesState: TextView
    private lateinit var tvTimeSyncState: TextView
    private lateinit var btnConnect: Button
    private lateinit var btnStart: Button
    private lateinit var btnStop: Button
    private lateinit var btnHistory: Button
    private lateinit var btnWifi: Button

    private var lastTimeSyncAttemptElapsedMs: Long = 0
    private var lastSamplesUiValue: Long = -1
    private var runStartElapsedMs: Long = 0
    private var latestAcquisitionEnabled: Boolean = false
    private var pendingSessionCommand: String? = null
    private var statusJsonBuffer: String = ""
    private var finalRunDialogShowing: Boolean = false

    private val statusRefreshHandler = Handler(Looper.getMainLooper())
    private val statusRefreshRunnable = object : Runnable {
        override fun run() {
            if (!BleManager.isConnected) {
                return
            }
            maybeSendTimeSync(force = false)
            BleManager.requestStatusRefresh()
            statusRefreshHandler.postDelayed(this, STATUS_REFRESH_INTERVAL_MS)
        }
    }

    private val samplesInterpolationRunnable = object : Runnable {
        override fun run() {
            if (!BleManager.isConnected) {
                return
            }
            updateInterpolatedSamples()
            statusRefreshHandler.postDelayed(this, 100L)
        }
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        if (permissions.values.all { it }) {
            startBLEScan()
        } else {
            Toast.makeText(this, "BLE permissions required", Toast.LENGTH_LONG).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        BleManager.init(this)

        tvStatus = findViewById(R.id.tvStatus)
        tvSessionState = findViewById(R.id.tvSessionState)
        tvSdState = findViewById(R.id.tvSdState)
        tvImuState = findViewById(R.id.tvImuState)
        tvHx711State = findViewById(R.id.tvHx711State)
        tvSamplesState = findViewById(R.id.tvSamplesState)
        tvTimeSyncState = findViewById(R.id.tvTimeSyncState)
        btnConnect = findViewById(R.id.btnConnect)
        btnStart = findViewById(R.id.btnStart)
        btnStop = findViewById(R.id.btnStop)
        btnHistory = findViewById(R.id.btnHistory)
        btnWifi = findViewById(R.id.btnWifi)

        BleManager.onConnectionChanged = { connected ->
            runOnUiThread {
                updateUI(connected)
                if (connected) {
                    sendCurrentTimeToDevice()
                    startStatusRefreshLoop()
                    startSamplesInterpolationLoop()
                    Toast.makeText(this, "Connected to PetBionic!", Toast.LENGTH_SHORT).show()
                } else {
                    stopStatusRefreshLoop()
                    stopSamplesInterpolationLoop()
                    // Reset all run state so no phantom run persists after reconnect
                    pendingSessionCommand = null
                    latestAcquisitionEnabled = false
                    runStartElapsedMs = 0
                    statusJsonBuffer = ""
                    Toast.makeText(this, "Disconnected", Toast.LENGTH_SHORT).show()
                }
            }
        }

        BleManager.onStatusReceived = { status ->
            runOnUiThread {
                renderStatus(status)
            }
        }

        val connectedNow = BleManager.isConnected
        updateUI(connectedNow)
        if (connectedNow) {
            BleManager.getLastStatusSnapshot()?.let { renderStatus(it) }
            BleManager.requestStatusRefresh()
            startStatusRefreshLoop()
            startSamplesInterpolationLoop()
        } else {
            stopStatusRefreshLoop()
            stopSamplesInterpolationLoop()
            resetSamplesDisplay()
        }

        btnConnect.setOnClickListener {
            if (BleManager.isConnected) {
                BleManager.disconnect()
            } else {
                requestBlePermissions()
            }
        }

        btnStart.setOnClickListener {
            pendingSessionCommand = "START"
            runStartElapsedMs = 0  // set on firmware ACK, not on click, to avoid BLE latency drift
            resetSamplesDisplay(initialValue = 0)
            tvSessionState.text = "State: Starting..."
            BleManager.sendCommand("START")
            requestStatusRefreshBurst()
        }

        btnStop.setOnClickListener {
            pendingSessionCommand = "STOP"
            tvSessionState.text = "State: Stopping..."
            BleManager.sendCommand("STOP")
            requestStatusRefreshBurst()
        }

        btnHistory.setOnClickListener {
            startActivity(Intent(this, HistoryActivity::class.java))
        }

        btnWifi.setOnClickListener {
            showWifiDialog()
        }
    }

    override fun onResume() {
        super.onResume()
        if (BleManager.isConnected) {
            startStatusRefreshLoop()
            startSamplesInterpolationLoop()
        }
    }

    override fun onPause() {
        super.onPause()
        stopStatusRefreshLoop()
        stopSamplesInterpolationLoop()
    }

    private fun startStatusRefreshLoop() {
        statusRefreshHandler.removeCallbacks(statusRefreshRunnable)
        statusRefreshHandler.post(statusRefreshRunnable)
    }

    private fun stopStatusRefreshLoop() {
        statusRefreshHandler.removeCallbacks(statusRefreshRunnable)
    }

    private fun startSamplesInterpolationLoop() {
        statusRefreshHandler.removeCallbacks(samplesInterpolationRunnable)
        statusRefreshHandler.post(samplesInterpolationRunnable)
    }

    private fun stopSamplesInterpolationLoop() {
        statusRefreshHandler.removeCallbacks(samplesInterpolationRunnable)
    }

    private fun requestStatusRefreshBurst() {
        statusRefreshHandler.postDelayed({ BleManager.requestStatusRefresh() }, 120)
        statusRefreshHandler.postDelayed({ BleManager.requestStatusRefresh() }, 300)
    }

    private fun resetSamplesDisplay(initialValue: Long = 0L) {
        lastSamplesUiValue = initialValue
        tvSamplesState.text = "Samples: $initialValue"
    }

    private fun updateInterpolatedSamples() {
        if (finalRunDialogShowing) {
            return
        }

        val runActive = latestAcquisitionEnabled || pendingSessionCommand.equals("START", ignoreCase = true)
        if (!runActive || runStartElapsedMs <= 0L) {
            return
        }

        val elapsedMs = (System.currentTimeMillis() - runStartElapsedMs).coerceAtLeast(0L)
        val displayValue = (elapsedMs * DISPLAY_SAMPLE_RATE_HZ / 1000.0).toLong()
        if (displayValue != lastSamplesUiValue) {
            lastSamplesUiValue = displayValue
            tvSamplesState.text = "Samples: $displayValue"
        }
    }

    private fun maybeSendTimeSync(force: Boolean = false) {
        val nowElapsed = System.currentTimeMillis()
        if (!force && (nowElapsed - lastTimeSyncAttemptElapsedMs) < TIME_SYNC_RETRY_INTERVAL_MS) {
            return
        }
        sendCurrentTimeToDevice()
    }

    private fun sendCurrentTimeToDevice() {
        val epochMs = System.currentTimeMillis()
        BleManager.sendCommand("TIME=$epochMs")
        lastTimeSyncAttemptElapsedMs = System.currentTimeMillis()
    }

    private fun showFinalRunDialog(json: JSONObject) {
        if (finalRunDialogShowing) {
            return
        }

        Log.i("MainActivity", "showFinalRunDialog: $json")
        val runName = json.optString("run_name", "run")
        val samplesFinal = json.optLong("samples_final", json.optLong("samples", 0L))
        val durationMs = json.optLong("duration_ms", 0L)
        val imuFailure = json.optBoolean("imu_failure", false)
        val hx711Failure = json.optBoolean("hx711_failure", false)
        val sensorFailures = json.optString("sensor_failures", "none")
        val durationSeconds = durationMs / 1000.0

        val message = buildString {
            appendLine("Run: $runName")
            appendLine("Samples finais: $samplesFinal")
            appendLine("Tempo: ${"%.2f".format(durationSeconds)} s")
            appendLine("Falha IMU: ${if (imuFailure) "sim" else "não"}")
            appendLine("Falha Load Cell: ${if (hx711Failure) "sim" else "não"}")
            appendLine("Falhas sensores: $sensorFailures")
        }

        finalRunDialogShowing = true
        android.app.AlertDialog.Builder(this)
            .setTitle("Fim da run")
            .setMessage(message)
            .setCancelable(false)
            .setPositiveButton("OK") { dialog, _ ->
                dialog.dismiss()
                finalRunDialogShowing = false
                pendingSessionCommand = null
                latestAcquisitionEnabled = false
                runStartElapsedMs = 0
                resetSamplesDisplay(initialValue = 0)
                BleManager.requestStatusRefresh()
            }
            .show()
    }

    private fun handleStatusJson(json: JSONObject): Boolean {
        if (json.optBoolean("run_complete", false)) {
            Log.i("MainActivity", "run_complete received")
            latestAcquisitionEnabled = false
            showFinalRunDialog(json)
            return true
        }

        val acq = json.optBoolean("acq", false)
        val sd = json.optBoolean("sd", false)
        val imu = if (json.has("imu")) json.optBoolean("imu", false) else null
        val hx711 = if (json.has("hx711")) json.optBoolean("hx711", false) else null
        val syncNeeded = json.optBoolean("time_sync_needed", false)
        val cmdAck = json.optString("cmd_ack", "")

        Log.d("MainActivity", "status acq=$acq sd=$sd cmdAck='$cmdAck'")
        latestAcquisitionEnabled = acq

        // Anchor the interpolation clock to when firmware actually ACKs START,
        // not to button-press time, so BLE latency doesn't offset the count.
        if (cmdAck.equals("START", ignoreCase = true)) {
            runStartElapsedMs = System.currentTimeMillis()
            Log.i("MainActivity", "START ack received – interpolation clock anchored")
            resetSamplesDisplay(initialValue = 0)
        } else if (acq && runStartElapsedMs == 0L) {
            // Fallback: firmware already recording (e.g. reconnect mid-run)
            runStartElapsedMs = System.currentTimeMillis()
            Log.i("MainActivity", "acq=true without ACK – interpolation clock anchored (fallback)")
        }

        if (cmdAck.equals("START", ignoreCase = true) || acq) {
            if (pendingSessionCommand.equals("START", ignoreCase = true)) {
                pendingSessionCommand = null
            }
        }

        if (cmdAck.equals("STOP", ignoreCase = true) || !acq) {
            if (pendingSessionCommand.equals("STOP", ignoreCase = true)) {
                pendingSessionCommand = null
            }
        }

        if (syncNeeded) {
            maybeSendTimeSync(force = false)
        }

        val sessionState = when {
            pendingSessionCommand.equals("START", ignoreCase = true) && !acq -> "Starting..."
            pendingSessionCommand.equals("STOP", ignoreCase = true) && acq -> "Stopping..."
            acq -> "Recording"
            else -> "Idle"
        }

        tvSessionState.text = "State: $sessionState"
        tvSdState.text = "SD: " + if (sd) "OK" else "Not ready"
        tvImuState.text = "IMU: " + when (imu) {
            true -> "OK"
            false -> "Not ready"
            null -> "-"
        }
        tvHx711State.text = "HX711: " + when (hx711) {
            true -> "OK"
            false -> "Not ready"
            null -> "-"
        }
        tvTimeSyncState.text = "Time sync: " + if (syncNeeded) "Needed" else "OK"

        return true
    }

    private fun tryConsumeJsonPayloads(cleaned: String): Boolean {
        if (cleaned.isEmpty()) {
            return false
        }

        statusJsonBuffer += cleaned
        if (statusJsonBuffer.length > 4096) {
            statusJsonBuffer = statusJsonBuffer.takeLast(4096)
        }

        var handledAny = false
        while (true) {
            val start = statusJsonBuffer.indexOf('{')
            if (start < 0) {
                statusJsonBuffer = ""
                break
            }

            var depth = 0
            var end = -1
            for (i in start until statusJsonBuffer.length) {
                when (statusJsonBuffer[i]) {
                    '{' -> depth++
                    '}' -> {
                        depth--
                        if (depth == 0) {
                            end = i
                            break
                        }
                    }
                }
            }

            if (end < 0) {
                if (start > 0) {
                    statusJsonBuffer = statusJsonBuffer.substring(start)
                }
                break
            }

            val payload = statusJsonBuffer.substring(start, end + 1)
            statusJsonBuffer = statusJsonBuffer.substring(end + 1)
            val parsed = runCatching { JSONObject(payload) }.getOrNull() ?: continue
            if (handleStatusJson(parsed)) {
                handledAny = true
            }
        }

        return handledAny
    }

    private fun renderStatus(status: String) {
        val cleaned = status.replace("\u0000", "").trim()
        Log.d("MainActivity", "renderStatus(${cleaned.length}): ${cleaned.take(120)}")

        if (tryConsumeJsonPayloads(cleaned)) {
            return
        }

        // If the string looks like a JSON fragment (starts with '{' or '"'), it's already
        // been buffered by tryConsumeJsonPayloads waiting for the rest – don't show it.
        if (cleaned.startsWith("{") || cleaned.startsWith("\"")) {
            Log.d("MainActivity", "renderStatus: buffered JSON fragment, skipping UI update")
            return
        }

        // Legacy plain-text status from older firmware builds.
        val legacyState = when {
            cleaned.contains("recording", ignoreCase = true) -> "Recording"
            cleaned.contains("syncing", ignoreCase = true) -> "Syncing to cloud"
            cleaned.contains("done", ignoreCase = true) -> "Sync complete"
            cleaned.contains("idle", ignoreCase = true) -> "Idle"
            cleaned.contains("sync_failed", ignoreCase = true) -> "Sync failed"
            else -> return  // unknown string – ignore rather than display garbage
        }
        tvSessionState.text = "State: $legacyState"
    }

    private fun updateUI(connected: Boolean) {
        tvStatus.text = if (connected) "Connected" else "Disconnected"
        tvStatus.setTextColor(
            if (connected)
                ContextCompat.getColor(this, android.R.color.holo_green_dark)
            else
                ContextCompat.getColor(this, android.R.color.holo_red_dark)
        )
        btnConnect.text = if (connected) "Disconnect" else "Connect to PetBionic"
        btnStart.isEnabled = connected
        btnStop.isEnabled = connected
        btnWifi.isEnabled = connected
    }

    private fun showWifiDialog() {
        val builder = android.app.AlertDialog.Builder(this)
        builder.setTitle("Configure WiFi")
        val layout = android.widget.LinearLayout(this)
        layout.orientation = android.widget.LinearLayout.VERTICAL
        layout.setPadding(50, 20, 50, 20)

        val etSsid = android.widget.EditText(this)
        etSsid.hint = "WiFi Network Name (SSID)"
        layout.addView(etSsid)

        val etPass = android.widget.EditText(this)
        etPass.hint = "WiFi Password"
        etPass.inputType = android.text.InputType.TYPE_CLASS_TEXT or
                android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
        layout.addView(etPass)

        builder.setView(layout)
        builder.setPositiveButton("Send") { _, _ ->
            val ssid = etSsid.text.toString()
            val pass = etPass.text.toString()
            if (ssid.isNotEmpty()) {
                BleManager.sendCommand("WIFI:$ssid:$pass")
                Toast.makeText(this, "WiFi credentials sent!", Toast.LENGTH_SHORT).show()
            }
        }
        builder.setNegativeButton("Cancel", null)
        builder.show()
    }

    private fun requestBlePermissions() {
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

        val allGranted = permissions.all {
            ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
        }

        if (allGranted) startBLEScan()
        else permissionLauncher.launch(permissions)
    }

    private fun startBLEScan() {
        Toast.makeText(this, "Scanning for PetBionic...", Toast.LENGTH_SHORT).show()
        BleManager.startScan()
    }
}
