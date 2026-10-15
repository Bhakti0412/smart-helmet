# Phone app guidelines (track sessions, receive data, emergency SMS)

A complete Android implementation is in `android-app/`. This guide explains how it works so you can extend it or port it (Flutter, React Native, iOS).

## 1. Permissions (request at "Start ride")

| Permission | Why | Android version |
|---|---|---|
| `BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT` | find and talk to the helmet | 12+ (older: `BLUETOOTH`, `BLUETOOTH_ADMIN`) |
| `ACCESS_FINE_LOCATION` | route and crash location (also needed for BLE scan on 11 and older) | all |
| `SEND_SMS` | emergency SMS | all |
| `POST_NOTIFICATIONS` | crash alert and ride notification | 13+ |
| `FOREGROUND_SERVICE` (+ `_LOCATION`, `_CONNECTED_DEVICE`) | keep running with the screen off | 9+ / 14+ |

Run everything in a **foreground service** with a persistent notification. A plain activity is killed when the screen turns off.

## 2. Receiving data over BLE

1. Scan with a filter on the service UUID `7f1c0001-8a52-4f7e-9d3b-5c1a2b3c4d5e`.
2. `connectGatt(..., TRANSPORT_LE)`, then `requestMtu(185)`, then `discoverServices()`.
3. Enable notifications (write `0x0001` to each CCCD) for **EVENT** and **STATUS**, one at a time (Android allows one pending GATT operation).
4. Parse each notification as JSON (`docs/BLE_PROTOCOL.md`). Always do UI/state work on the main thread.
5. On disconnect, close the GATT object and **re-scan after ~2 s** (auto-reconnect). The helmet re-advertises by itself.

## 3. Session tracking

| Item | How |
|---|---|
| Route | `LocationManager` GPS provider, 1 s interval; store `lat, lon, speed, time offset` |
| Distance | sum of `Location.distanceTo()` between accepted fixes; **drop fixes with accuracy > 25 m and moves < 3 m** (GPS jitter while stopped) |
| Duration | `now - start` (not a sum of fixes, so tunnels do not shorten it) |
| Events | each EVENT notification -> list entry with phone time and last known position (`HARD_BRAKE`, `CRASH_*`) |
| Storage | one JSON file per session in app-private storage when the rider presses **Stop & save** (format: `sample-data/session_sample.json`) |

## 4. Emergency SMS flow (most important)

```
helmet: CRASH_COUNTDOWN  ->  app shows heads-up alert + "I'M OK" button, starts its own 10 s timer
   rider taps I'M OK      ->  app writes "CANCEL" to the helmet, incident = CANCELLED, no SMS
   helmet: CRASH_CONFIRMED (10 s)  OR  app timer expires (10 s + 1 s grace)  ->  send SMS once
```
Rules the implementation follows - keep them if you port it:

1. **One incident, at most one SMS.** Keep a per-incident state (`NONE / COUNTDOWN / CANCELLED / SMS_SENT`).
2. **Cancel always wins** on the phone side, even if the BLE write to the helmet fails.
3. **Do not depend on BLE for the SMS.** The helmet may be thrown or the link may drop; the phone's own timer is the safety net.
4. **Location order:** phone GPS fix -> helmet NEO-6M coordinates (from STATUS) -> "location unavailable". Always send the SMS even without a location.
5. Use `SmsManager.divideMessage` + `sendMultipartTextMessage` (the text contains a long Maps URL).
6. Show the result (sent / failed) to the rider and keep it in the session log (`CRASH_CONFIRMED` event).
7. Pre-set contact: validated phone number stored in `SharedPreferences`; the Start button refuses to run without it.

SMS text: see `sample-data/sms_example.txt`.

**Distribution note:** Google Play restricts apps that request `SEND_SMS`. For the hackathon, install the APK directly; check the current Play policy before any store release.

## 5. Test checklist

- [ ] Helmet connects, "Helmet connected" shows, night flag follows the LDR (cover it with your hand for ~6 s).
- [ ] Walk with the phone: distance grows, the route has points, duration is right after Stop.
- [ ] Tap-brake test: a firm forward jolt of the helmet logs `HARD_BRAKE`.
- [ ] Crash drill: drop the helmet onto a soft surface (>= 4 g) and leave it still -> buzzer + amber flashing + alert on phone.
  - tap **I'M OK** -> buzzer stops, no SMS;
  - do nothing -> SMS arrives at the contact **once** (use your own number first).
- [ ] Start a drill, and as soon as the alert appears on the phone switch the helmet off -> the SMS is still sent by the phone timer (11 s).
- [ ] Screen off for 5 minutes -> session keeps logging.
