package com.example.petbionic

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import org.json.JSONObject

class MainActivity : AppCompatActivity() {

    private lateinit var tvStatus: TextView
    private lateinit var tvSessionState: TextView
    private lateinit var tvSdState: TextView
    private lateinit var tvImuState: TextView
    private lateinit var tvHx711State: TextView
    private lateinit var tvSamplesState: TextView
    private lateinit var tvEventsState: TextView
    private lateinit var tvTimeSyncState: TextView
    private lateinit var btnConnect: Button
    private lateinit var btnStart: Button
    private lateinit var btnStop: Button
    private lateinit var btnHistory: Button
    private lateinit var btnWifi: Button

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
        tvEventsState = findViewById(R.id.tvEventsState)
        tvTimeSyncState = findViewById(R.id.tvTimeSyncState)
        btnConnect = findViewById(R.id.btnConnect)
        btnStart = findViewById(R.id.btnStart)
        btnStop = findViewById(R.id.btnStop)
        btnHistory = findViewById(R.id.btnHistory)
        btnWifi = findViewById(R.id.btnWifi)

        updateUI(false)

        BleManager.onConnectionChanged = { connected ->
            runOnUiThread {
                updateUI(connected)
                if (connected) {
                    sendCurrentTimeToDevice()
                    Toast.makeText(this, "Connected to PetBionic!", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, "Disconnected", Toast.LENGTH_SHORT).show()
                }
            }
        }

        BleManager.onStatusReceived = { status ->
            runOnUiThread {
                renderStatus(status)
            }
        }

        btnConnect.setOnClickListener {
            if (BleManager.isConnected) {
                BleManager.disconnect()
            } else {
                requestBlePermissions()
            }
        }

        btnStart.setOnClickListener {
            BleManager.sendCommand("START")
            tvSessionState.text = "State: Waiting device update..."
        }

        btnStop.setOnClickListener {
            BleManager.sendCommand("STOP")
            tvSessionState.text = "State: Waiting device update..."
        }

        btnHistory.setOnClickListener {
            startActivity(Intent(this, HistoryActivity::class.java))
        }

        btnWifi.setOnClickListener {
            showWifiDialog()
        }
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

    private fun sendCurrentTimeToDevice() {
        val epochMs = System.currentTimeMillis()
        BleManager.sendCommand("TIME=$epochMs")
    }

    private fun renderStatus(status: String) {
        val cleaned = status.replace("\u0000", "").trim()
        val jsonPayload = run {
            val start = cleaned.indexOf('{')
            val end = cleaned.lastIndexOf('}')
            if (start >= 0 && end > start) cleaned.substring(start, end + 1) else null
        }

        if (jsonPayload != null) {
            runCatching {
                val json = JSONObject(jsonPayload)
                val acq = json.optBoolean("acq", false)
                val sd = json.optBoolean("sd", false)
                val imu = json.optBoolean("imu", false)
                val hx711 = json.optBoolean("hx711", false)
                val samples = json.optLong("samples", 0)
                val events = json.optLong("events", 0)
                val syncNeeded = json.optBoolean("time_sync_needed", false)

                val state = if (acq) "Recording" else "Idle"
                val sdState = if (sd) "OK" else "Not ready"
                val imuState = if (imu) "OK" else "Not ready"
                val hx711State = if (hx711) "OK" else "Not ready"
                val syncState = if (syncNeeded) "Needed" else "OK"

                tvSessionState.text = "State: $state"
                tvSdState.text = "SD: $sdState"
                tvImuState.text = "IMU: $imuState"
                tvHx711State.text = "HX711: $hx711State"
                tvSamplesState.text = "Samples: $samples"
                tvEventsState.text = "Events: $events"
                tvTimeSyncState.text = "Time sync: $syncState"
                return
            }
        }

        // Some BLE stacks deliver fragmented notify payloads. Handle partial JSON text too.
        if (cleaned.contains("\"acq\"")) {
            val acq = Regex("\"acq\"\\s*:\\s*(true|false)", RegexOption.IGNORE_CASE)
                .find(cleaned)
                ?.groupValues
                ?.getOrNull(1)
                ?.equals("true", ignoreCase = true)

            val sd = Regex("\"sd\"\\s*:\\s*(true|false)", RegexOption.IGNORE_CASE)
                .find(cleaned)
                ?.groupValues
                ?.getOrNull(1)
                ?.equals("true", ignoreCase = true)

            val imu = Regex("\"imu\"\\s*:\\s*(true|false)", RegexOption.IGNORE_CASE)
                .find(cleaned)
                ?.groupValues
                ?.getOrNull(1)
                ?.equals("true", ignoreCase = true)

            val hx711 = Regex("\"hx711\"\\s*:\\s*(true|false)", RegexOption.IGNORE_CASE)
                .find(cleaned)
                ?.groupValues
                ?.getOrNull(1)
                ?.equals("true", ignoreCase = true)

            acq?.let {
                tvSessionState.text = "State: " + if (it) "Recording" else "Idle"
            }

            sd?.let {
                tvSdState.text = "SD: " + if (it) "OK" else "Not ready"
            }

            imu?.let {
                tvImuState.text = "IMU: " + if (it) "OK" else "Not ready"
            }

            hx711?.let {
                tvHx711State.text = "HX711: " + if (it) "OK" else "Not ready"
            }

            return
        }

        val legacyState = when {
            cleaned.contains("recording", ignoreCase = true) -> "Recording"
            cleaned.contains("syncing", ignoreCase = true) -> "Syncing to cloud"
            cleaned.contains("done", ignoreCase = true) -> "Sync complete"
            cleaned.contains("idle", ignoreCase = true) -> "Idle"
            cleaned.contains("sync_failed", ignoreCase = true) -> "Sync failed"
            else -> cleaned
        }
        tvSessionState.text = "State: $legacyState"
        tvSdState.text = "SD: -"
        tvImuState.text = "IMU: -"
        tvHx711State.text = "HX711: -"
        tvSamplesState.text = "Samples: -"
        tvEventsState.text = "Events: -"
        tvTimeSyncState.text = "Time sync: -"
    }
}