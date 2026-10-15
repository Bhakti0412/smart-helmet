package com.smarthelmet.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import kotlinx.coroutines.flow.update
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.ceil
import kotlin.math.max

/**
 * Foreground service that keeps working with the screen off:
 *  - BLE link to the helmet (events + status, CANCEL command)
 *  - phone GPS -> route, distance, duration
 *  - crash alert: if the countdown is not cancelled within 10 s -> SMS with GPS location
 */
class RideService : Service(), BleClient.Listener, LocationListener {

    companion object {
        const val ACTION_START = "com.smarthelmet.app.START"
        const val ACTION_STOP = "com.smarthelmet.app.STOP"
        const val ACTION_CANCEL_ALERT = "com.smarthelmet.app.CANCEL_ALERT"
        private const val CH_RIDE = "ride"
        private const val CH_ALERT = "alert"
        private const val ID_RIDE = 1
        private const val ID_ALERT = 2
        private const val COUNTDOWN_MS = 10_000L   // same as COUNTDOWN_MS in firmware config.h
        private const val GRACE_MS = 1_000L        // fallback timer fires this long after the countdown ends
    }

    private enum class Incident { NONE, COUNTDOWN, CANCELLED, SMS_SENT }

    private val main = Handler(Looper.getMainLooper())
    private val tracker = SessionTracker()
    private var ble: BleClient? = null
    private var lm: LocationManager? = null
    private var running = false

    private var incident = Incident.NONE
    private var countdownStartMs = 0L
    private var espLatLon: Pair<Double, Double>? = null     // from optional NEO-6M, via BLE status
    private var phoneLoc: Location? = null
    private var tick = 0

    private fun publish(block: UiState.() -> UiState) = RideState.ui.update { it.block() }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> startRide()
            ACTION_STOP -> stopRide()
            ACTION_CANCEL_ALERT -> cancelByRider()
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        ble?.stop()
        lm?.removeUpdates(this)
        main.removeCallbacksAndMessages(null)
        super.onDestroy()
    }

    // ------------------------------------------------------------------ ride lifecycle
    private fun startRide() {
        if (running) return
        createChannels()
        ServiceCompat.startForeground(
            this, ID_RIDE, rideNotification("Starting..."),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION or ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        )
        running = true
        incident = Incident.NONE
        tracker.start(System.currentTimeMillis())
        RideState.ui.value = UiState(running = true, bleText = "Starting...")

        lm = getSystemService(LOCATION_SERVICE) as LocationManager
        try {
            lm?.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f, this, Looper.getMainLooper())
        } catch (e: SecurityException) {
            publish { copy(gpsText = "Location permission missing") }
        }
        ble = BleClient(this, this).also { it.start() }
        main.post(ticker)
    }

    private fun stopRide() {
        if (!running) { stopSelf(); return }
        running = false
        main.removeCallbacks(ticker)
        ble?.stop(); ble = null
        lm?.removeUpdates(this)
        val file = tracker.stop(System.currentTimeMillis(), File(filesDir, "sessions"))
        publish { copy(running = false, bleConnected = false, bleText = "idle", crashCountdown = false,
            lastSavedPath = file?.absolutePath, message = if (file != null) "Session saved" else "Could not save session") }
        notificationManager().cancel(ID_ALERT)
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    /** 1 Hz: duration, countdown display, fallback timer for the SMS, notification text. */
    private val ticker = object : Runnable {
        override fun run() {
            if (!running) return
            val now = System.currentTimeMillis()
            if (incident == Incident.COUNTDOWN) {
                val left = max(0L, countdownStartMs + COUNTDOWN_MS - now)
                publish { copy(crashSecondsLeft = ceil(left / 1000.0).toInt()) }
                // Fallback: BLE may drop after a crash. If CONFIRMED never arrives, send anyway.
                if (now >= countdownStartMs + COUNTDOWN_MS + GRACE_MS) confirmCrash()
            }
            publish { copy(durationS = tracker.durationS(now)) }
            if (++tick % 5 == 0) {
                val s = RideState.ui.value
                notificationManager().notify(ID_RIDE, rideNotification(
                    "%.2f km  |  %s".format(Locale.US, s.distanceM / 1000.0, s.bleText)))
            }
            main.postDelayed(this, 1000)
        }
    }

    // ------------------------------------------------------------------ BLE callbacks (main thread)
    override fun onBleState(connected: Boolean, text: String) {
        publish { copy(bleConnected = connected, bleText = text) }
    }

    override fun onBleStatus(json: JSONObject) {
        publish { copy(night = json.optInt("night", 0) == 1) }
        if (json.has("lat") && json.has("lon")) espLatLon = Pair(json.getDouble("lat"), json.getDouble("lon"))
    }

    override fun onBleEvent(json: JSONObject) {
        when (json.optString("e")) {
            "HARD_BRAKE" -> {
                tracker.addEvent("HARD_BRAKE", json.optDouble("v", 0.0))
                publish { copy(hardBrakes = tracker.hardBrakeCount) }
            }
            "CRASH_COUNTDOWN" -> startAlert(json.optDouble("v", 0.0))
            "CRASH_CANCELLED" -> if (incident == Incident.COUNTDOWN) {
                incident = Incident.CANCELLED
                tracker.addEvent("CRASH_CANCELLED", 0.0)
                clearAlertUi("Alert cancelled")
            }
            "CRASH_CONFIRMED" -> confirmCrash()
        }
    }

    // ------------------------------------------------------------------ crash alert
    private fun startAlert(impactG: Double) {
        incident = Incident.COUNTDOWN
        countdownStartMs = System.currentTimeMillis()
        tracker.addEvent("CRASH_COUNTDOWN", impactG)
        publish { copy(crashCountdown = true, crashSecondsLeft = 10, message = null) }
        val cancelIntent = PendingIntent.getService(
            this, 0, Intent(this, RideService::class.java).setAction(ACTION_CANCEL_ALERT),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val n = NotificationCompat.Builder(this, CH_ALERT)
            .setSmallIcon(android.R.drawable.ic_dialog_alert)
            .setContentTitle("Crash detected")
            .setContentText("Emergency SMS in 10 s unless you cancel")
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setPriority(NotificationCompat.PRIORITY_MAX)
            .setOngoing(true)
            .addAction(android.R.drawable.ic_menu_close_clear_cancel, "I'M OK - CANCEL", cancelIntent)
            .build()
        notificationManager().notify(ID_ALERT, n)
    }

    /** Rider pressed "I'm OK" (app button or notification action). */
    private fun cancelByRider() {
        if (incident != Incident.COUNTDOWN) return
        incident = Incident.CANCELLED          // phone side is cancelled even if the BLE write fails
        ble?.sendCancel()
        tracker.addEvent("CRASH_CANCELLED", 0.0)
        clearAlertUi("Alert cancelled")
    }

    /** Countdown ended without a cancel -> SMS with location to the pre-set contact. */
    private fun confirmCrash() {
        if (incident == Incident.CANCELLED || incident == Incident.SMS_SENT) return
        incident = Incident.SMS_SENT
        tracker.addEvent("CRASH_CONFIRMED", 0.0)

        val number = Prefs.contact(this)
        val loc: Pair<Double, Double>? = phoneLoc?.let { Pair(it.latitude, it.longitude) } ?: espLatLon
        val where = loc?.let { "https://maps.google.com/?q=%.6f,%.6f".format(Locale.US, it.first, it.second) }
            ?: "location unavailable"
        val time = SimpleDateFormat("dd MMM yyyy HH:mm", Locale.getDefault()).format(Date())
        val text = "EMERGENCY: Smart Helmet detected a possible crash and the rider did not cancel the alert. " +
            "Location: $where (time: $time)"
        val sent = number.isNotBlank() && SmsSender.send(this, number, text)
        clearAlertUi(if (sent) "Emergency SMS sent to $number" else "SMS FAILED - check permission / SIM / contact")
    }

    private fun clearAlertUi(msg: String) {
        notificationManager().cancel(ID_ALERT)
        publish { copy(crashCountdown = false, message = msg) }
    }

    // ------------------------------------------------------------------ location
    override fun onLocationChanged(location: Location) {
        phoneLoc = location
        tracker.onLocation(location)
        publish {
            copy(distanceM = tracker.distanceM,
                gpsText = "%.5f, %.5f (+/-%.0f m)".format(Locale.US, location.latitude, location.longitude, location.accuracy))
        }
    }

    // Needed on Android 9 and older (abstract there)
    @Deprecated("Deprecated in Java")
    override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {}
    override fun onProviderEnabled(provider: String) {}
    override fun onProviderDisabled(provider: String) {
        publish { copy(gpsText = "GPS is off - enable location") }
    }

    // ------------------------------------------------------------------ notifications
    private fun notificationManager() = getSystemService(NOTIFICATION_SERVICE) as NotificationManager

    private fun createChannels() {
        val nm = notificationManager()
        nm.createNotificationChannel(NotificationChannel(CH_RIDE, "Ride tracking", NotificationManager.IMPORTANCE_LOW))
        nm.createNotificationChannel(NotificationChannel(CH_ALERT, "Crash alert", NotificationManager.IMPORTANCE_HIGH))
    }

    private fun rideNotification(text: String): Notification {
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        return NotificationCompat.Builder(this, CH_RIDE)
            .setSmallIcon(android.R.drawable.ic_menu_mylocation)
            .setContentTitle("Smart Helmet - ride in progress")
            .setContentText(text)
            .setOngoing(true)
            .setContentIntent(open)
            .build()
    }
}
