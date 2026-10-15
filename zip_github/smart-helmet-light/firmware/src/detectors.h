// detectors.h - crash and hard-brake logic. No Arduino dependencies (unit-testable on a PC).
#pragma once
#include <stdint.h>
#include <math.h>
#include "config.h"

struct ImuSample {
  float ax, ay, az;   // g
  float gx, gy, gz;   // deg/s
};

static inline float accelMagG(const ImuSample& s) {
  return sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
}
static inline float gyroMagDps(const ImuSample& s) {
  return sqrtf(s.gx * s.gx + s.gy * s.gy + s.gz * s.gz);
}

// ---------------------------------------------------------------------------
// Crash detector:  spike  ->  settle  ->  no movement  ->  10 s countdown
// ---------------------------------------------------------------------------
class CrashDetector {
 public:
  enum State { IDLE = 0, WAIT_STILL = 1, COUNTDOWN = 2 };
  enum Event { EV_NONE = 0, EV_COUNTDOWN_STARTED, EV_CANCELLED, EV_CONFIRMED };

  // Call at IMU rate. Returns an event when the state machine reaches a milestone.
  Event update(const ImuSample& s, uint32_t now) {
    const bool cancel = cancelReq_;
    cancelReq_ = false;
    const float g = accelMagG(s);
    pushWindow(g);

    switch (state_) {
      case IDLE:
        if (g >= CRASH_IMPACT_G) {
          state_ = WAIT_STILL;
          tImpact_ = now;
          stillSince_ = 0;
          peakG_ = g;
        }
        break;

      case WAIT_STILL:
        if (g >= CRASH_IMPACT_G) {            // another spike while tumbling: restart the window
          tImpact_ = now;
          stillSince_ = 0;
          if (g > peakG_) peakG_ = g;
          break;
        }
        if (now - tImpact_ > CRASH_STILL_TIMEOUT_MS) { state_ = IDLE; break; }   // rider kept moving
        if (now - tImpact_ < CRASH_SETTLE_MS) break;
        if (isStill(s)) {
          if (stillSince_ == 0) stillSince_ = now;
          if (now - stillSince_ >= CRASH_STILL_HOLD_MS) {
            state_ = COUNTDOWN;
            tCountdown_ = now;
            return EV_COUNTDOWN_STARTED;
          }
        } else {
          stillSince_ = 0;
        }
        break;

      case COUNTDOWN:
        if (cancel) { state_ = IDLE; return EV_CANCELLED; }
        if (now - tCountdown_ >= COUNTDOWN_MS) { state_ = IDLE; return EV_CONFIRMED; }
        break;
    }
    return EV_NONE;
  }

  void requestCancel() { cancelReq_ = true; }          // safe to call from the BLE task
  State state() const { return state_; }
  float lastImpactG() const { return peakG_; }
  uint32_t msRemaining(uint32_t now) const {
    if (state_ != COUNTDOWN) return 0;
    uint32_t e = now - tCountdown_;
    return e >= COUNTDOWN_MS ? 0 : COUNTDOWN_MS - e;
  }
  const char* stateName() const {
    return state_ == IDLE ? "IDLE" : (state_ == WAIT_STILL ? "WAIT_STILL" : "COUNTDOWN");
  }

 private:
  // Still = |a| close to 1 g AND almost no variation over the last 0.5 s AND low rotation.
  // The variance test is what separates "lying on the road" from "riding smoothly".
  void pushWindow(float g) {
    win_[winIdx_] = g;
    winIdx_ = (winIdx_ + 1) % STILL_WINDOW_SAMPLES;
    if (winCount_ < STILL_WINDOW_SAMPLES) winCount_++;
  }
  bool isStill(const ImuSample& s) const {
    if (winCount_ < STILL_WINDOW_SAMPLES) return false;
    float mean = 0;
    for (int i = 0; i < STILL_WINDOW_SAMPLES; i++) mean += win_[i];
    mean /= STILL_WINDOW_SAMPLES;
    float var = 0;
    for (int i = 0; i < STILL_WINDOW_SAMPLES; i++) var += (win_[i] - mean) * (win_[i] - mean);
    var /= STILL_WINDOW_SAMPLES;
    return fabsf(mean - 1.0f) < STILL_ACCEL_TOL_G && sqrtf(var) < STILL_ACCEL_STD_G &&
           gyroMagDps(s) < STILL_GYRO_DPS;
  }
  float win_[STILL_WINDOW_SAMPLES] = {0};
  int winIdx_ = 0, winCount_ = 0;
  State state_ = IDLE;
  uint32_t tImpact_ = 0, stillSince_ = 0, tCountdown_ = 0;
  float peakG_ = 0;
  volatile bool cancelReq_ = false;
};

// ---------------------------------------------------------------------------
// Hard-braking detector: sustained forward deceleration relative to a slow baseline
// ---------------------------------------------------------------------------
class BrakeDetector {
 public:
  // Returns true once per braking event; peakG gets the peak deceleration (positive number).
  bool update(const ImuSample& s, uint32_t now, float& peakG) {
    const float a = BRAKE_AXIS_SIGN * s.ax;
    if (!init_) { base_ = a; init_ = true; }
    base_ += BRAKE_BASELINE_ALPHA * (a - base_);
    const float dec = a - base_;                       // negative while decelerating
    if (dec <= -BRAKE_DECEL_G) {
      if (!below_) { below_ = true; since_ = now; peak_ = dec; }
      else if (dec < peak_) peak_ = dec;
      if (now - since_ >= BRAKE_MIN_MS && (lastEvent_ == 0 || now - lastEvent_ >= BRAKE_COOLDOWN_MS)) {
        lastEvent_ = now;
        below_ = false;
        peakG = -peak_;
        return true;
      }
    } else {
      below_ = false;
    }
    return false;
  }

 private:
  bool init_ = false, below_ = false;
  float base_ = 0, peak_ = 0;
  uint32_t since_ = 0, lastEvent_ = 0;
};
