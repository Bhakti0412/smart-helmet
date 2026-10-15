package com.smarthelmet.app

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow

/** Everything the UI shows. The service publishes it, the activity renders it. */
data class UiState(
    val running: Boolean = false,
    val bleConnected: Boolean = false,
    val bleText: String = "idle",
    val night: Boolean? = null,
    val distanceM: Double = 0.0,
    val durationS: Long = 0,
    val hardBrakes: Int = 0,
    val gpsText: String = "waiting for GPS fix",
    val crashCountdown: Boolean = false,
    val crashSecondsLeft: Int = 0,
    val lastSavedPath: String? = null,
    val message: String? = null
)

object RideState {
    val ui = MutableStateFlow(UiState())
}

/** Only setting stored: the pre-set emergency contact. */
object Prefs {
    private fun sp(c: Context) = c.getSharedPreferences("helmet_prefs", Context.MODE_PRIVATE)
    fun contact(c: Context): String = sp(c).getString("contact", "") ?: ""
    fun setContact(c: Context, v: String) = sp(c).edit().putString("contact", v).apply()
}
