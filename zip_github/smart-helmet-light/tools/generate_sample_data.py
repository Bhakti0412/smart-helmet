#!/usr/bin/env python3
"""Generate the synthetic sample data in ../sample-data/ (deterministic, no dependencies).

  session_sample.json            one riding session in the exact format the Android app saves
  imu_crash_trace.csv            100 Hz IMU trace: riding -> impact -> tumble -> lying still
  imu_hard_brake.csv             100 Hz IMU trace: riding -> hard brake -> riding
  imu_pothole_false_alarm.csv    100 Hz IMU trace: riding -> pothole spike -> riding (must NOT alert)

All data is synthetic (not recorded from a real ride).
"""
import csv, json, math, os, random
from datetime import datetime, timedelta, timezone

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sample-data")
os.makedirs(OUT, exist_ok=True)
rnd = random.Random(42)


# ----------------------------------------------------------------- IMU traces
class Trace:
    def __init__(self):
        self.t = 1000   # ms (firmware treats 0 as "unset", so start after boot)
        self.rows = []

    def add(self, ax, ay, az, gx, gy, gz):
        self.rows.append([self.t, round(ax, 3), round(ay, 3), round(az, 3), round(gx, 2), round(gy, 2), round(gz, 2)])
        self.t += 10

    def riding(self, ms, ax=0.0):
        for _ in range(ms // 10):
            self.add(ax + rnd.uniform(-.10, .10), rnd.uniform(-.10, .10), 1 + rnd.uniform(-.15, .15),
                     rnd.uniform(-20, 20), rnd.uniform(-20, 20), rnd.uniform(-20, 20))

    def still(self, ms):
        for _ in range(ms // 10):
            self.add(rnd.uniform(-.01, .01), rnd.uniform(-.01, .01), 1 + rnd.uniform(-.01, .01),
                     rnd.uniform(-.5, .5), rnd.uniform(-.5, .5), rnd.uniform(-.5, .5))

    def impact(self, g):
        for _ in range(3):
            self.add(g, .5, .5, 200, 150, 100)

    def tumble(self, ms):
        for _ in range(ms // 10):
            self.add(rnd.uniform(-1.5, 1.5), rnd.uniform(-1.5, 1.5), 1 + rnd.uniform(-1.5, 1.5),
                     rnd.uniform(-250, 250), rnd.uniform(-250, 250), rnd.uniform(-250, 250))

    def save(self, name):
        with open(os.path.join(OUT, name), "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["t_ms", "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps", "gz_dps"])
            w.writerows(self.rows)


tr = Trace(); tr.riding(8000); tr.impact(9.2); tr.tumble(800); tr.still(15000); tr.save("imu_crash_trace.csv")
tr = Trace(); tr.riding(6000); tr.riding(400, ax=-0.6); tr.riding(6000); tr.save("imu_hard_brake.csv")
tr = Trace(); tr.riding(6000); tr.impact(4.6); tr.riding(12000); tr.save("imu_pothole_false_alarm.csv")


# ----------------------------------------------------------------- session JSON
def haversine(a, b):
    R = 6371000.0
    p1, p2 = math.radians(a[0]), math.radians(b[0])
    dp, dl = p2 - p1, math.radians(b[1] - a[1])
    h = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * R * math.asin(math.sqrt(h))


# Synthetic route roughly along Marine Drive, Mumbai (Nariman Point -> Girgaum Chowpatty)
start_pt, end_pt = (18.9256, 72.8242), (18.9548, 72.8135)
N, STEP_S = 180, 3
t0 = datetime(2026, 10, 12, 12, 35, 0, tzinfo=timezone.utc)
route, dist, prev, speeds = [], 0.0, None, []
for i in range(N):
    f = i / (N - 1)
    # gentle curve + tiny GPS jitter
    lat = start_pt[0] + (end_pt[0] - start_pt[0]) * f + 0.0004 * math.sin(f * math.pi * 3) + rnd.uniform(-2e-6, 2e-6)
    lon = start_pt[1] + (end_pt[1] - start_pt[1]) * f + 0.0003 * math.sin(f * math.pi * 2) + rnd.uniform(-2e-6, 2e-6)
    p = (lat, lon)
    if prev:
        d = haversine(prev, p)
        dist += d
        speeds.append(d / STEP_S)
    prev = p
    route.append({"t_offset_s": i * STEP_S, "lat": round(lat, 6), "lon": round(lon, 6),
                  "speed_mps": round(speeds[-1] if speeds else 0.0, 2)})

events = []
for off, g in [(96, 0.52), (261, 0.61), (408, 0.47)]:
    pt = route[min(off // STEP_S, N - 1)]
    events.append({"time_utc": (t0 + timedelta(seconds=off)).strftime("%Y-%m-%dT%H:%M:%SZ"),
                   "type": "HARD_BRAKE", "value_g": g, "lat": pt["lat"], "lon": pt["lon"]})

duration = (N - 1) * STEP_S
session = {
    "session_id": "20261012_123500",
    "synthetic_sample": True,
    "start_time_utc": t0.strftime("%Y-%m-%dT%H:%M:%SZ"),
    "end_time_utc": (t0 + timedelta(seconds=duration)).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "duration_s": duration,
    "distance_m": round(dist, 1),
    "avg_speed_kmh": round(dist / duration * 3.6, 1),
    "max_speed_kmh": round(max(speeds) * 3.6, 1),
    "hard_brake_count": len(events),
    "events": events,
    "route": route,
}
with open(os.path.join(OUT, "session_sample.json"), "w") as f:
    json.dump(session, f, indent=2)

print("Wrote sample data to", os.path.normpath(OUT))
print("session: %.2f km in %d s, %d hard-brake events" % (dist / 1000, duration, len(events)))
