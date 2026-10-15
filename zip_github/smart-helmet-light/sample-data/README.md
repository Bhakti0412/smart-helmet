# Sample data (all synthetic)

| File | What it is | How to use it |
|---|---|---|
| `session_sample.json` | One ~9 min ride (3.4 km, 3 hard-braking events) in the exact format the Android app saves | Schema reference, demo for the report/slides, plotting the route |
| `imu_crash_trace.csv` | 100 Hz MPU6050-style trace: riding -> 9.2 g impact -> tumble -> lying still | `make replay` in `firmware/test/host` -> countdown, then CRASH_CONFIRMED after 10 s |
| `imu_hard_brake.csv` | Riding -> 0.6 g braking for 0.4 s -> riding | Replay -> one HARD_BRAKE |
| `imu_pothole_false_alarm.csv` | Riding -> 4.6 g pothole spike -> riding | Replay -> no countdown (false-alarm rejection) |
| `ble_messages.jsonl` | Example BLE notification payloads and the CANCEL command | Protocol reference, testing the app parser |
| `sms_example.txt` | The emergency SMS text | Reference |

Columns of the IMU CSVs: `t_ms, ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps`.
Regenerate everything with `python3 tools/generate_sample_data.py`.
