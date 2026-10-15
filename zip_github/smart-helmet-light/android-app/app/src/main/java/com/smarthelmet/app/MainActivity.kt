package com.smarthelmet.app

import android.Manifest
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.view.View
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.smarthelmet.app.databinding.ActivityMainBinding
import kotlinx.coroutines.launch
import java.util.Locale

class MainActivity : AppCompatActivity() {

    private lateinit var b: ActivityMainBinding

    private val required: Array<String>
        get() {
            val l = mutableListOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.SEND_SMS)
            if (Build.VERSION.SDK_INT >= 31) {
                l += Manifest.permission.BLUETOOTH_SCAN
                l += Manifest.permission.BLUETOOTH_CONNECT
            }
            return l.toTypedArray()
        }

    private val permLauncher = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { res ->
        val missing = required.filter { res[it] != true && ContextCompat.checkSelfPermission(this, it) != 0 }
        if (missing.isEmpty()) launchService() else
            Toast.makeText(this, "Permissions required: " + missing.joinToString { it.substringAfterLast('.') }, Toast.LENGTH_LONG).show()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        b = ActivityMainBinding.inflate(layoutInflater)
        setContentView(b.root)
        b.edtContact.setText(Prefs.contact(this))

        b.btnStart.setOnClickListener {
            val number = b.edtContact.text.toString().replace(" ", "")
            if (!Regex("^\\+?[0-9]{7,15}$").matches(number)) {
                b.edtContact.error = "Enter the emergency contact number"
                return@setOnClickListener
            }
            Prefs.setContact(this, number)
            val toAsk = mutableListOf(*required)
            if (Build.VERSION.SDK_INT >= 33) toAsk += Manifest.permission.POST_NOTIFICATIONS
            permLauncher.launch(toAsk.toTypedArray())
        }
        b.btnStop.setOnClickListener {
            startService(Intent(this, RideService::class.java).setAction(RideService.ACTION_STOP))
        }
        b.btnCancelAlert.setOnClickListener {
            startService(Intent(this, RideService::class.java).setAction(RideService.ACTION_CANCEL_ALERT))
        }

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) { RideState.ui.collect { render(it) } }
        }
    }

    private fun launchService() {
        ContextCompat.startForegroundService(
            this, Intent(this, RideService::class.java).setAction(RideService.ACTION_START)
        )
    }

    private fun render(s: UiState) {
        b.txtBle.text = "Helmet: ${s.bleText}"
        b.txtNight.text = "Night mode: " + when (s.night) { true -> "ON"; false -> "off"; null -> "--" }
        b.btnStart.isEnabled = !s.running
        b.btnStop.isEnabled = s.running
        b.edtContact.isEnabled = !s.running

        b.btnCancelAlert.visibility = if (s.crashCountdown) View.VISIBLE else View.GONE
        b.txtAlert.visibility = if (s.crashCountdown) View.VISIBLE else View.GONE
        b.txtAlert.text = "Crash detected - emergency SMS in ${s.crashSecondsLeft} s"

        val h = s.durationS / 3600
        val m = (s.durationS % 3600) / 60
        val sec = s.durationS % 60
        b.txtStats.text = String.format(
            Locale.US, "Duration:  %02d:%02d:%02d\nDistance:  %.2f km\nHard braking:  %d\nGPS:  %s",
            h, m, sec, s.distanceM / 1000.0, s.hardBrakes, s.gpsText
        )
        b.txtSaved.text = listOfNotNull(s.message, s.lastSavedPath?.let { "Saved: $it" }).joinToString("\n")
    }
}
