// config.h - all pins, thresholds and UUIDs in one place.
// Pure #defines (no Arduino includes) so the detector logic can be unit-tested on a PC.
#pragma once

// ---------------- Pin map (ESP32 DevKit V1) ----------------
#define PIN_LED_FRONT   18   // WS2812B strip: front warm white
#define PIN_LED_REAR    19   // WS2812B strip: rear red
#define PIN_LED_AMBER   23   // WS2812B strip: amber indicators
#define PIN_LDR         34   // ADC1_CH6 (input only) <- LDR / 10k divider
#define PIN_BUZZER      25   // active buzzer (via NPN transistor, see docs/WIRING.md)
#define PIN_I2C_SDA     21   // MPU6050 SDA
#define PIN_I2C_SCL     22   // MPU6050 SCL
#define PIN_GPS_RX      16   // ESP32 RX2  <- NEO-6M TX   (only if ENABLE_GPS = 1)
#define PIN_GPS_TX      17   // ESP32 TX2  -> NEO-6M RX   (only if ENABLE_GPS = 1)

// ---------------- Optional hardware ----------------
#define ENABLE_GPS      0    // 1 = optional NEO-6M fitted; coordinates are added to the BLE status

// ---------------- LED strips ----------------
#define NUM_LEDS_FRONT  8
#define NUM_LEDS_REAR   8
#define NUM_LEDS_AMBER  4
#define LED_BRIGHTNESS  80          // 0..255, keeps current draw low on a single Li-ion cell
#define AMBER_FLASH_HALF_PERIOD_MS 250

// ---------------- Feature 1: auto night mode ----------------
// LDR wired to 3V3, 10k to GND, node on PIN_LDR: darker -> LOWER ADC reading (12-bit, 0..4095).
#define LDR_DARK_ON       1200      // filtered reading below this  => dark   (tune on your hardware)
#define LDR_BRIGHT_OFF    1800      // filtered reading above this  => bright (hysteresis)
#define NIGHT_CONFIRM_MS  5000      // condition must hold this long (ignores tunnels / shadows)
#define LDR_EMA_ALPHA     0.1f

// ---------------- Feature 2: crash detection ----------------
#define IMU_PERIOD_MS            10     // 100 Hz sampling
#define CRASH_IMPACT_G           4.0f   // |a| spike that arms the detector (sensor range is +-16 g)
#define CRASH_SETTLE_MS          1000   // ignore the tumble right after the impact
#define CRASH_STILL_HOLD_MS      2000   // must then stay still for this long
#define CRASH_STILL_TIMEOUT_MS   10000  // give up if not still within this long after the last spike
#define STILL_ACCEL_TOL_G        0.12f  // | mean|a| - 1 g | below this counts as still
#define STILL_ACCEL_STD_G        0.03f  // std-dev of |a| over the window; riding vibration is higher
#define STILL_WINDOW_SAMPLES     50     // 0.5 s at 100 Hz
#define STILL_GYRO_DPS           15.0f  // rotation rate below this counts as still (MPU6050 gyro bias can be several dps)
#define COUNTDOWN_MS             10000  // buzzer countdown before the alert is confirmed

// ---------------- Feature 3: hard-braking events ----------------
#define BRAKE_AXIS_SIGN          1.0f   // +1: sensor X axis points forward; -1 if mounted reversed
#define BRAKE_DECEL_G            0.35f  // deceleration beyond the slow baseline
#define BRAKE_MIN_MS             150    // must persist this long
#define BRAKE_COOLDOWN_MS        3000
#define BRAKE_BASELINE_ALPHA     0.01f  // slow filter that removes gravity / head tilt

// ---------------- Timing ----------------
#define LDR_PERIOD_MS            100
#define STATUS_PERIOD_MS         1000
#define LED_PERIOD_MS            50

// ---------------- BLE (must match android-app BleUuids.kt) ----------------
#define BLE_DEVICE_NAME      "SmartHelmet"
#define BLE_MTU              185
#define SERVICE_UUID         "7f1c0001-8a52-4f7e-9d3b-5c1a2b3c4d5e"
#define CHAR_EVENT_UUID      "7f1c0002-8a52-4f7e-9d3b-5c1a2b3c4d5e"   // notify : events
#define CHAR_STATUS_UUID     "7f1c0003-8a52-4f7e-9d3b-5c1a2b3c4d5e"   // notify/read : 1 Hz status
#define CHAR_COMMAND_UUID    "7f1c0004-8a52-4f7e-9d3b-5c1a2b3c4d5e"   // write : "CANCEL"
