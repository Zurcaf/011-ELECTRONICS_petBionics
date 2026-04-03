package com.example.petbionic

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import java.util.UUID

@SuppressLint("MissingPermission")
object BleManager {

    private val SERVICE_UUID     = UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b")
    private val CMD_CHAR_UUID    = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8")
    private val STATUS_CHAR_UUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a9")
    private val CCCD_UUID        = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private var gatt: BluetoothGatt? = null
    private var cmdChar: BluetoothGattCharacteristic? = null
    private var scanner: BluetoothLeScanner? = null
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
        val filters = listOf(
            ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE_UUID)).build(),
            ScanFilter.Builder().setDeviceName("PetBionic").build()
        )
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
        isConnected = false
        gatt?.disconnect()
        closeGatt()
        handler.post { onConnectionChanged?.invoke(false) }
    }

    private fun closeGatt() {
        gatt?.close()
        gatt = null
    }

    fun sendCommand(command: String) {
        cmdChar?.let {
            it.value = command.toByteArray()
            gatt?.writeCharacteristic(it)
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
                gatt.requestMtu(185)
                gatt.discoverServices()
                return
            }

            cmdChar = null
            isConnected = false
            closeGatt()
            handler.post { onConnectionChanged?.invoke(false) }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                disconnect()
                return
            }

            val service = gatt.getService(SERVICE_UUID) ?: return
            val control = service.getCharacteristic(CMD_CHAR_UUID)
            val statusChar = service.getCharacteristic(STATUS_CHAR_UUID)
            if (control == null || statusChar == null) {
                disconnect()
                return
            }

            cmdChar = control
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
            if (descriptor.uuid == CCCD_UUID && status == BluetoothGatt.GATT_SUCCESS) {
                isConnected = true
                handler.post { onConnectionChanged?.invoke(true) }
            } else if (descriptor.uuid == CCCD_UUID) {
                disconnect()
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                return
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid == STATUS_CHAR_UUID) {
                val status = characteristic.value.toString(Charsets.UTF_8)
                handler.post { onStatusReceived?.invoke(status) }
            }
        }
    }
}