package com.smarthelmet.app

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import org.json.JSONObject
import java.util.UUID

/** Must match firmware/src/config.h */
object BleUuids {
    val SERVICE: UUID = UUID.fromString("7f1c0001-8a52-4f7e-9d3b-5c1a2b3c4d5e")
    val EVENT: UUID = UUID.fromString("7f1c0002-8a52-4f7e-9d3b-5c1a2b3c4d5e")
    val STATUS: UUID = UUID.fromString("7f1c0003-8a52-4f7e-9d3b-5c1a2b3c4d5e")
    val COMMAND: UUID = UUID.fromString("7f1c0004-8a52-4f7e-9d3b-5c1a2b3c4d5e")
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}

/**
 * BLE central: scans for the helmet's service, connects, subscribes to EVENT + STATUS
 * notifications, writes "CANCEL" to COMMAND, and reconnects automatically.
 * Listener callbacks are always delivered on the main thread.
 */
@SuppressLint("MissingPermission") // permissions are requested by MainActivity before the service starts
class BleClient(private val ctx: Context, private val listener: Listener) {

    interface Listener {
        fun onBleState(connected: Boolean, text: String)
        fun onBleEvent(json: JSONObject)
        fun onBleStatus(json: JSONObject)
    }

    private val main = Handler(Looper.getMainLooper())
    private val adapter: BluetoothAdapter? =
        (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    private var gatt: BluetoothGatt? = null
    private var cmdChar: BluetoothGattCharacteristic? = null
    private var stopped = true
    private var scanning = false
    private val pendingNotify = ArrayDeque<BluetoothGattCharacteristic>()

    fun start() {
        stopped = false
        scan()
    }

    fun stop() {
        stopped = true
        main.removeCallbacksAndMessages(null)
        stopScan()
        gatt?.close()
        gatt = null
        cmdChar = null
    }

    /** Ask the helmet to stop its buzzer countdown. Returns false if the write could not be issued. */
    fun sendCancel(): Boolean {
        val g = gatt ?: return false
        val c = cmdChar ?: return false
        val data = "CANCEL".toByteArray(Charsets.UTF_8)
        return try {
            if (Build.VERSION.SDK_INT >= 33) {
                g.writeCharacteristic(c, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) ==
                    BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                run {
                    c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                    c.value = data
                    g.writeCharacteristic(c)
                }
            }
        } catch (e: SecurityException) {
            false
        }
    }

    // ---------------------------------------------------------------- scanning
    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            stopScan()
            connect(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            listener.onBleState(false, "Scan failed ($errorCode)")
            retry()
        }
    }

    private fun scan() {
        if (stopped || scanning) return
        val a = adapter
        if (a == null || !a.isEnabled) {
            listener.onBleState(false, "Bluetooth is off")
            retry()
            return
        }
        val scanner = a.bluetoothLeScanner
        if (scanner == null) {
            retry()
            return
        }
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(BleUuids.SERVICE)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        try {
            scanning = true
            listener.onBleState(false, "Searching for helmet...")
            scanner.startScan(listOf(filter), settings, scanCb)
        } catch (e: SecurityException) {
            scanning = false
            listener.onBleState(false, "Bluetooth permission missing")
        }
    }

    private fun stopScan() {
        if (!scanning) return
        scanning = false
        try {
            adapter?.bluetoothLeScanner?.stopScan(scanCb)
        } catch (_: Exception) {
        }
    }

    private fun retry() {
        if (!stopped) main.postDelayed({ scan() }, 2000)
    }

    private fun connect(device: BluetoothDevice) {
        listener.onBleState(false, "Connecting...")
        gatt = device.connectGatt(ctx, false, gattCb, BluetoothDevice.TRANSPORT_LE)
    }

    // ---------------------------------------------------------------- GATT
    private val gattCb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                if (!g.requestMtu(185)) g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                g.close()
                if (gatt === g) gatt = null
                cmdChar = null
                main.post { listener.onBleState(false, "Helmet disconnected - reconnecting") }
                retry()
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(BleUuids.SERVICE)
            if (status != BluetoothGatt.GATT_SUCCESS || svc == null) {
                g.disconnect()
                return
            }
            cmdChar = svc.getCharacteristic(BleUuids.COMMAND)
            pendingNotify.clear()
            svc.getCharacteristic(BleUuids.EVENT)?.let { pendingNotify.add(it) }
            svc.getCharacteristic(BleUuids.STATUS)?.let { pendingNotify.add(it) }
            enableNext(g)
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            enableNext(g)
        }

        // Android 13+
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            handle(c.uuid, value)
        }

        // Android 12 and older
        @Deprecated("Deprecated in Java")
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT < 33) handle(c.uuid, c.value ?: return)
        }
    }

    /** Enable notifications one characteristic at a time (GATT allows one pending operation). */
    private fun enableNext(g: BluetoothGatt) {
        val c = pendingNotify.removeFirstOrNull()
        if (c == null) {
            main.post { listener.onBleState(true, "Helmet connected") }
            return
        }
        g.setCharacteristicNotification(c, true)
        val d = c.getDescriptor(BleUuids.CCCD)
        if (d == null) {
            enableNext(g)
            return
        }
        if (Build.VERSION.SDK_INT >= 33) {
            g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        } else {
            @Suppress("DEPRECATION")
            run {
                d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                g.writeDescriptor(d)
            }
        }
    }

    private fun handle(uuid: UUID, value: ByteArray) {
        val json = try {
            JSONObject(String(value, Charsets.UTF_8))
        } catch (e: Exception) {
            return
        }
        main.post {
            when (uuid) {
                BleUuids.EVENT -> listener.onBleEvent(json)
                BleUuids.STATUS -> listener.onBleStatus(json)
            }
        }
    }
}
