// Smart Helmet Light - ESP32 firmware
//  1. Auto night mode  : LDR -> front warm white + rear red lights
//  2. Crash detection  : MPU6050 spike + stillness -> 10 s buzzer countdown -> phone app sends SMS
//  3. Session tracking : BLE events (hard braking) + status to the phone app
//
// Hardware strictly as specified: ESP32, MPU6050, LDR+resistor, WS2812B strips (front/rear/amber),
// buzzer, TP4056 + Li-ion, optional NEO-6M GPS. See docs/WIRING.md.
#include <Arduino.h>
#include "config.h"
#include "detectors.h"
#include "imu_mpu6050.h"
#include "leds.h"
#include "ble_link.h"

#if ENABLE_GPS
#include <TinyGPSPlus.h>
static TinyGPSPlus gps;
#endif

static Mpu6050 imu;
static CrashDetector crash;
static BrakeDetector brake;
static Leds leds;
static BleLink ble;

static bool nightMode = false;
static float ldrFiltered = -1.0f;
static uint32_t darkSince = 0, brightSince = 0;

static uint32_t lastImu = 0, lastLdr = 0, lastLed = 0, lastStatus = 0;

// ---------------------------------------------------------------------------
// Feature 1: auto night mode (hysteresis + confirmation time => no flicker at dusk/dawn)
// ---------------------------------------------------------------------------
static void updateNightMode(uint32_t now) {
  const int raw = analogRead(PIN_LDR);
  ldrFiltered = (ldrFiltered < 0) ? raw : ldrFiltered + LDR_EMA_ALPHA * (raw - ldrFiltered);

  if (!nightMode) {
    if (ldrFiltered < LDR_DARK_ON) {
      if (darkSince == 0) darkSince = now;
      if (now - darkSince >= NIGHT_CONFIRM_MS) { nightMode = true; darkSince = 0; }
    } else {
      darkSince = 0;
    }
  } else {
    if (ldrFiltered > LDR_BRIGHT_OFF) {
      if (brightSince == 0) brightSince = now;
      if (now - brightSince >= NIGHT_CONFIRM_MS) { nightMode = false; brightSince = 0; }
    } else {
      brightSince = 0;
    }
  }
  leds.setNight(nightMode);
}

// ---------------------------------------------------------------------------
// Buzzer: slow beeps for the first 5 s of the countdown, fast beeps for the last 5 s
// ---------------------------------------------------------------------------
static void updateBuzzer(uint32_t now) {
  bool on = false;
  if (crash.state() == CrashDetector::COUNTDOWN) {
    const uint32_t left = crash.msRemaining(now);
    const uint32_t period = left > 5000 ? 1000 : 400;
    const uint32_t onMs = left > 5000 ? 150 : 200;
    on = (now % period) < onMs;
  }
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
// Feature 2 + 3: IMU processing
// ---------------------------------------------------------------------------
static void processImu(uint32_t now) {
  ImuSample s;
  if (!imu.read(s)) return;

  const CrashDetector::Event ev = crash.update(s, now);
  switch (ev) {
    case CrashDetector::EV_COUNTDOWN_STARTED:
      leds.setAmberFlashing(true);
      ble.sendEvent("CRASH_COUNTDOWN", crash.lastImpactG(), now);
      Serial.printf("[crash] impact %.1f g, still -> countdown started\n", crash.lastImpactG());
      break;
    case CrashDetector::EV_CANCELLED:
      leds.setAmberFlashing(false);
      ble.sendEvent("CRASH_CANCELLED", 0, now);
      Serial.println("[crash] cancelled by rider");
      break;
    case CrashDetector::EV_CONFIRMED:
      leds.setAmberFlashing(false);
      ble.sendEvent("CRASH_CONFIRMED", crash.lastImpactG(), now);   // phone app sends the SMS
      Serial.println("[crash] not cancelled -> confirmed");
      break;
    default: break;
  }

  if (crash.state() == CrashDetector::IDLE) {       // do not log braking while a crash is being evaluated
    float peak;
    if (brake.update(s, now, peak)) {
      ble.sendEvent("HARD_BRAKE", peak, now);
      Serial.printf("[brake] %.2f g\n", peak);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);        // 0..3.3 V range

  leds.begin();
  if (!imu.begin()) Serial.println("MPU6050 not found - check SDA/SCL wiring");
  ble.begin();
#if ENABLE_GPS
  Serial2.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
#endif
  Serial.println("Smart Helmet ready");
}

void loop() {
  const uint32_t now = millis();

#if ENABLE_GPS
  while (Serial2.available()) gps.encode(Serial2.read());
#endif

  if (ble.consumeCancel()) crash.requestCancel();

  if (now - lastImu >= IMU_PERIOD_MS) { lastImu = now; processImu(now); }
  if (now - lastLdr >= LDR_PERIOD_MS) { lastLdr = now; updateNightMode(now); }
  if (now - lastLed >= LED_PERIOD_MS) { lastLed = now; leds.update(now); }
  updateBuzzer(now);

  if (now - lastStatus >= STATUS_PERIOD_MS) {
    lastStatus = now;
#if ENABLE_GPS
    const bool fix = gps.location.isValid();
    ble.sendStatus(nightMode, (int)ldrFiltered, crash.stateName(), fix, gps.location.lat(), gps.location.lng());
#else
    ble.sendStatus(nightMode, (int)ldrFiltered, crash.stateName(), false, 0, 0);
#endif
  }
}
