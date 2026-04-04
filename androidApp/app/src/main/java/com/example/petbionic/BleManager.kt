package com.example.petbionic

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.util.Log
import java.util.ArrayDeque
import java.util.UUID

@SuppressLint("MissingPermission")
object BleManager {

    private val SERVICE_UUIDS = listOf(
        UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b"),
        UUID.fromString("14f16000-9d9c-470f-9f6a-6e6fe401a001")
    )
    private val CMD_CHAR_UUIDS = listOf(
        UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8"),
        UUID.fromString("14f16001-9d9c-470f-9f6a-6e6fe401a001")
    )
    private val STATUS_CHAR_UUIDS = listOf(
        UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a9"),
        UUID.fromString("14f16002-9d9c-470f-9f6a-6e6fe401a001")
    )
    private val DEVICE_NAMES = listOf("PetBionic", "petBionics")
    private val CCCD_UUID        = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private var gatt: BluetoothGatt? = null
    private var cmdChar: BluetoothGattCharacteristic? = null
    private var statusCharRef: BluetoothGattCharacteristic? = null
    private var lastStatusPayload: String? = null
    private var scanner: BluetoothLeScanner? = null
    private var statusReadInFlight = false
    private var statusReadQueued = false
    private val pendingCommands = ArrayDeque<String>()
    private val handler = Handler(Looper.getMainLooper())
    private val stopScanRunnable = Runnable { stopScan() }

    @SuppressLint("StaticFieldLeak")
    private var context: Context? = null

    var isConnected = false
        private set

    var onConnectionChanged: ((Boolean) -> Unit)? = null
    var onStatusReceived: ((String) -> Unit)? = null

    fun init(ctx: Context) {
        context = ctx.applicationContext
    }

    fun startScan() {
        val ctx = context ?: return
        val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE)
                as BluetoothManager).adapter
        if (!adapter.isEnabled) {
            return
        }
        // Ensure a clean state before attempting a new discovery/connection cycle.
        closeGatt()
        cmdChar = null
        isConnected = false
        scanner = adapter.bluetoothLeScanner
        val filters = buildList {
            SERVICE_UUIDS.forEach { uuid ->
                add(ScanFilter.Builder().setServiceUuid(ParcelUuid(uuid)).build())
            }
            DEVICE_NAMES.forEach { name ->
                add(ScanFilter.Builder().setDeviceName(name).build())
            }
        }
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        handler.removeCallbacks(stopScanRunnable)
        scanner?.startScan(filters, settings, scanCallback)
        handler.postDelayed(stopScanRunnable, 10000)
    }

    fun stopScan() {
        handler.removeCallbacks(stopScanRunnable)
        scanner?.stopScan(scanCallback)
    }

    fun disconnect() {
        stopScan()
        cmdChar = null
        statusCharRef = null
        lastStatusPayload = null
        statusReadInFlight = false
        statusReadQueued = false
        pendingCommands.clear()
        isConnected = false
        gatt?.disconnect()
        closeGatt()
        handler.post { onConnectionChanged?.invoke(false) }
    }

    fun getLastStatusSnapshot(): String? = lastStatusPayload

    fun requestStatusRefresh() {
        val g = gatt ?: return
        val s = statusCharRef ?: return
        if (statusReadInFlight) {
            statusReadQueued = true
            return
        }

        statusReadInFlight = true
        if (!g.readCharacteristic(s)) {
            statusReadInFlight = false
        }
    }

    private fun closeGatt() {
        gatt?.close()
        gatt = null
    }

    fun sendCommand(command: String) {
        val g = gatt
        val c = cmdChar
        if (g == null || c == null) {
            if (pendingCommands.size >= 8) {
                pendingCommands.removeFirst()
            }
            pendingCommands.addLast(command)
            return
        }

        c.writeType = if ((c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0) {
            BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        } else {
            BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        }
        c.value = command.toByteArray()
        g.writeCharacteristic(c)
    }

    private fun flushPendingCommands() {
        val g = gatt ?: return
        val c = cmdChar ?: return
        while (pendingCommands.isNotEmpty()) {
            c.writeType = if ((c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0) {
                BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            } else {
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            }
            c.value = pendingCommands.removeFirst().toByteArray()
            g.writeCharacteristic(c)
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            stopScan()
            val ctx = context ?: return
            closeGatt()
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                gatt = result.device.connectGatt(ctx, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
            } else {
                gatt = result.device.connectGatt(ctx, false, gattCallback)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            isConnected = false
            handler.post { onConnectionChanged?.invoke(false) }
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                this@BleManager.gatt = gatt
                cmdChar = null
                statusCharRef = null
                lastStatusPayload = null
                statusReadInFlight = false
                statusReadQueued = false
                pendingCommands.clear()
                isConnected = true
                handler.post { onConnectionChanged?.invoke(true) }
                // Request larger MTU before service discovery so that large BLE notify
                // payloads (e.g. run_complete JSON ~200 bytes) are not truncated.
                // discoverServices() is called from onMtuChanged.
                Log.d("BleManager", "Connected – requesting MTU 512")
                gatt.requestMtu(512)
                return
            }

            cmdChar = null
            statusCharRef = null
            statusReadInFlight = false
            statusReadQueued = false
            pendingCommands.clear()
            isConnected = false
            closeGatt()
            handler.post { onConnectionChanged?.invoke(false) }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                disconnect()
                return
            }

            val service = SERVICE_UUIDS
                .asSequence()
                .mapNotNull { uuid -> gatt.getService(uuid) }
                .firstOrNull()
            if (service == null) {
                disconnect()
                return
            }

            val control = CMD_CHAR_UUIDS
                .asSequence()
                .mapNotNull { uuid -> service.getCharacteristic(uuid) }
                .firstOrNull()
            val statusChar = STATUS_CHAR_UUIDS
                .asSequence()
                .mapNotNull { uuid -> service.getCharacteristic(uuid) }
                .firstOrNull()
            if (control == null || statusChar == null) {
                disconnect()
                return
            }

            cmdChar = control
            statusCharRef = statusChar
            flushPendingCommands()

            // Some devices are flaky with notifications; force one immediate read for UI bootstrap.
            requestStatusRefresh()

            gatt.setCharacteristicNotification(statusChar, true)
            val descriptor = statusChar.getDescriptor(CCCD_UUID)
            if (descriptor == null) {
                disconnect()
                return
            }
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt.writeDescriptor(descriptor)
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (descriptor.uuid == CCCD_UUID) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    disconnect()
                    return
                }

                requestStatusRefresh()
            }
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w("BleManager", "onCharacteristicRead failed status=$status")
                return
            }

            if (STATUS_CHAR_UUIDS.contains(characteristic.uuid)) {
                val payload = characteristic.value.toString(Charsets.UTF_8)
                Log.d("BleManager", "READ payload(${payload.length}): $payload")
                lastStatusPayload = payload
                statusReadInFlight = false
                handler.post { onStatusReceived?.invoke(payload) }

                if (statusReadQueued) {
                    statusReadQueued = false
                    handler.postDelayed({ requestStatusRefresh() }, 120)
                }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            Log.d("BleManager", "MTU negotiated: $mtu (status=$status)")
            // Proceed with service discovery regardless – if negotiation failed we
            // fall back to the default MTU but the connection is still usable.
            gatt.discoverServices()
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (STATUS_CHAR_UUIDS.contains(characteristic.uuid)) {
                val status = characteristic.value.toString(Charsets.UTF_8)
                Log.d("BleManager", "NOTIFY payload(${status.length}): $status")
                lastStatusPayload = status
                statusReadInFlight = false
                handler.post { onStatusReceived?.invoke(status) }

                if (statusReadQueued) {
                    statusReadQueued = false
                    handler.postDelayed({ requestStatusRefresh() }, 120)
                }
            }
        }
    }
}