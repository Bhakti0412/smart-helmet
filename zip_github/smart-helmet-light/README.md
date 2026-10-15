# Smart Helmet Light

Low-cost smart helmet add-on for two-wheeler riders, built for the **Hack for Social Cause** hackathon.
An ESP32 in the helmet handles lights and crash sensing; the rider's phone adds GPS, logging and the emergency SMS.

| # | Feature | How it works |
|---|---|---|
| 1 | **Auto night mode** | LDR watches ambient light; at dusk/dawn the ESP32 turns on the front warm-white and rear red WS2812B lights |
| 2 | **Crash detection and emergency alert** | MPU6050 spike followed by no movement -> 10 s buzzer countdown -> if not cancelled, the phone app sends an SMS with the GPS location to a pre-set contact |
| 3 | **Session tracking** | Phone app connects over BLE and logs route (phone GPS), duration, distance and events such as hard braking |

Only the specified hardware is used; there are no extra features.

## Hardware

ESP32 (built-in BLE) · MPU6050 · LDR + 10 k resistor · WS2812B strips (front warm white, rear red, amber indicators) ·
reflective tape/film · buzzer · TP4056 + Li-ion cell · optional NEO-6M GPS.
Supporting part you will need: a 3.7 V -> 5 V boost converter (and a transistor + resistor for the buzzer), see [docs/WIRING.md](docs/WIRING.md).

## Repository layout

```
firmware/        ESP32 firmware (PlatformIO)  + host-side tests of the crash/brake logic
android-app/     Android app (Kotlin): BLE, session tracking, emergency SMS
docs/            WIRING.md  ARCHITECTURE.md  BLE_PROTOCOL.md  APP_GUIDE.md
sample-data/     synthetic session JSON, IMU traces, BLE messages, SMS text
tools/           generate_sample_data.py
presentation/    Hack for Social Cause slide deck (PPTX + PDF)
```

## Quick start

### 1. Wire it
Follow [docs/WIRING.md](docs/WIRING.md). Default pins: LEDs 18/19/23, LDR 34, buzzer 25, MPU6050 SDA/SCL 21/22.

### 2. Flash the firmware
```bash
pip install platformio
cd firmware
pio run -t upload          # builds and flashes the ESP32
pio device monitor         # 115200 baud: shows crash / brake logs
```
Arduino IDE users: create a sketch, copy the files from `firmware/src`, rename `main.cpp` to the `.ino`, and install the *Adafruit NeoPixel* library.
Set `ENABLE_GPS` to `1` in `config.h` only if you fitted the NEO-6M (also install *TinyGPSPlus*).

### 3. Build the phone app
Open `android-app/` in Android Studio (Koala or newer), let it sync, and run on an Android 8.0+ phone (a real phone: it needs BLE, GPS and a SIM).
Command line: `cd android-app && gradle wrapper && ./gradlew assembleDebug`.
Enter the emergency contact number, tap **Start ride**, grant the permissions.

### 4. Test the logic without hardware
```bash
cd firmware/test/host
make test        # 8 synthetic scenarios, must print "All tests passed"
make replay      # replays ../../../sample-data/*.csv through the real detector code
```
Expected replay result: crash trace -> `CRASH_COUNTDOWN` then `CRASH_CONFIRMED` 10 s later (or `CRASH_CANCELLED` with `--cancel-after 4000`);
hard-brake trace -> one `HARD_BRAKE`; pothole trace -> nothing.

## Calibration (do this before trusting it)

All thresholds live in `firmware/src/config.h`.

| Setting | Tune by |
|---|---|
| `LDR_DARK_ON`, `LDR_BRIGHT_OFF` | read `ldr` from the BLE status (or Serial) at dusk and in daylight with your LDR/resistor pair |
| `BRAKE_AXIS_SIGN` | mount the MPU6050 with X forward; if braking is not detected, flip the sign |
| `BRAKE_DECEL_G` | ride with a phone log and compare detected events with real hard stops |
| `CRASH_IMPACT_G`, `STILL_*` | log real rides over bad roads (no alert must fire) and soft-surface drop tests (alert must fire); replay the logs with the host tool |

## Safety and limitations

This is a **prototype safety aid**, not a certified helmet or emergency device. It does not replace a certified helmet, and the SMS depends on the phone being
paired, powered, in range, with GPS and SMS credit. Thresholds are untested starting values. Always test the SMS with your own number first. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#6-known-limitations-be-honest-in-the-demo).

## Documentation

[Architecture](docs/ARCHITECTURE.md) · [Wiring](docs/WIRING.md) · [BLE protocol](docs/BLE_PROTOCOL.md) · [Phone app guide](docs/APP_GUIDE.md) · [Sample data](sample-data/README.md)

## License

MIT, see [LICENSE](LICENSE).
