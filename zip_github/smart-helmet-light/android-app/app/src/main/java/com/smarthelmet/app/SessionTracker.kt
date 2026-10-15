package com.smarthelmet.app

import android.location.Location
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * Records one riding session: route (phone GPS), duration, distance and events (hard braking, crash).
 * Saved as JSON in the format shown in sample-data/session_sample.json.
 */
class SessionTracker {
    private class Point(val tOffsetS: Long, val lat: Double, val lon: Double, val speedMps: Float)
    private class Evt(val timeMs: Long, val type: String, val value: Double, val lat: Double?, val lon: Double?)

    private val route = ArrayList<Point>()
    private val events = ArrayList<Evt>()
    private var last: Location? = null

    var startMs = 0L
        private set
    var running = false
        private set
    var distanceM = 0.0
        private set
    private var maxSpeedMps = 0f

    val hardBrakeCount: Int get() = events.count { it.type == "HARD_BRAKE" }
    val lastLatLon: Pair<Double, Double>? get() = last?.let { Pair(it.latitude, it.longitude) }

    fun start(nowMs: Long) {
        route.clear(); events.clear()
        last = null; distanceM = 0.0; maxSpeedMps = 0f
        startMs = nowMs
        running = true
    }

    fun durationS(nowMs: Long): Long = (nowMs - startMs) / 1000

    fun onLocation(loc: Location) {
        if (!running) return
        if (loc.hasAccuracy() && loc.accuracy > 25f) return          // drop poor fixes
        val prev = last
        if (prev != null) {
            val d = prev.distanceTo(loc)
            if (d < 3f) return                                        // ignore GPS jitter while stopped
            distanceM += d
        }
        last = loc
        val speed = if (loc.hasSpeed()) loc.speed else 0f
        if (speed > maxSpeedMps) maxSpeedMps = speed
        route.add(Point((System.currentTimeMillis() - startMs) / 1000, loc.latitude, loc.longitude, speed))
    }

    fun addEvent(type: String, value: Double) {
        val p = last
        events.add(Evt(System.currentTimeMillis(), type, value, p?.latitude, p?.longitude))
    }

    /** Finishes the session and writes it to [dir]. Returns the file, or null if nothing could be written. */
    fun stop(nowMs: Long, dir: File): File? {
        if (!running) return null
        running = false
        val dur = durationS(nowMs)
        val json = JSONObject().apply {
            put("session_id", fmt("yyyyMMdd_HHmmss", startMs))
            put("start_time_utc", fmt("yyyy-MM-dd'T'HH:mm:ss'Z'", startMs))
            put("end_time_utc", fmt("yyyy-MM-dd'T'HH:mm:ss'Z'", nowMs))
            put("duration_s", dur)
            put("distance_m", round1(distanceM))
            put("avg_speed_kmh", round1(if (dur > 0) distanceM / dur * 3.6 else 0.0))
            put("max_speed_kmh", round1(maxSpeedMps * 3.6))
            put("hard_brake_count", hardBrakeCount)
            put("events", JSONArray().also { arr ->
                events.forEach { e ->
                    arr.put(JSONObject().apply {
                        put("time_utc", fmt("yyyy-MM-dd'T'HH:mm:ss'Z'", e.timeMs))
                        put("type", e.type)
                        put("value_g", e.value)
                        put("lat", e.lat ?: JSONObject.NULL)
                        put("lon", e.lon ?: JSONObject.NULL)
                    })
                }
            })
            put("route", JSONArray().also { arr ->
                route.forEach { p ->
                    arr.put(JSONObject().apply {
                        put("t_offset_s", p.tOffsetS)
                        put("lat", p.lat)
                        put("lon", p.lon)
                        put("speed_mps", round1(p.speedMps.toDouble()))
                    })
                }
            })
        }
        return try {
            dir.mkdirs()
            File(dir, "session_${fmt("yyyyMMdd_HHmmss", startMs)}.json").also { it.writeText(json.toString(2)) }
        } catch (e: Exception) {
            null
        }
    }

    private fun round1(v: Double) = Math.round(v * 10.0) / 10.0

    private fun fmt(pattern: String, ms: Long): String =
        SimpleDateFormat(pattern, Locale.US).apply { timeZone = TimeZone.getTimeZone("UTC") }.format(Date(ms))
}
