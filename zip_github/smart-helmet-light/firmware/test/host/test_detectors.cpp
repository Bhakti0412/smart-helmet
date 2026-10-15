// Host-side tests for CrashDetector and BrakeDetector.
//   ./test_detectors                                   run synthetic scenarios (exit code != 0 on failure)
//   ./test_detectors --replay file.csv [--cancel-after MS]   replay a recorded IMU trace
//
// CSV columns: t_ms,ax,ay,az,gx,gy,gz   (g and deg/s, 100 Hz)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../../src/detectors.h"

struct Row { uint32_t t; ImuSample s; };

struct Result {
  int brakes = 0, countdowns = 0, cancelled = 0, confirmed = 0;
  uint32_t tCountdown = 0, tConfirmed = 0;
};

static Result run(const std::vector<Row>& rows, long cancelAfterMs, bool verbose) {
  CrashDetector crash;
  BrakeDetector brake;
  Result r;
  for (const Row& row : rows) {
    if (cancelAfterMs >= 0 && crash.state() == CrashDetector::COUNTDOWN &&
        (long)(row.t - r.tCountdown) >= cancelAfterMs)
      crash.requestCancel();
    switch (crash.update(row.s, row.t)) {
      case CrashDetector::EV_COUNTDOWN_STARTED:
        r.countdowns++; r.tCountdown = row.t;
        if (verbose) printf("  t=%6u ms  CRASH_COUNTDOWN (impact %.1f g)\n", row.t, crash.lastImpactG());
        break;
      case CrashDetector::EV_CANCELLED:
        r.cancelled++;
        if (verbose) printf("  t=%6u ms  CRASH_CANCELLED\n", row.t);
        break;
      case CrashDetector::EV_CONFIRMED:
        r.confirmed++; r.tConfirmed = row.t;
        if (verbose) printf("  t=%6u ms  CRASH_CONFIRMED  -> phone sends SMS\n", row.t);
        break;
      default: break;
    }
    if (crash.state() == CrashDetector::IDLE) {
      float peak;
      if (brake.update(row.s, row.t, peak)) {
        r.brakes++;
        if (verbose) printf("  t=%6u ms  HARD_BRAKE %.2f g\n", row.t, peak);
      }
    }
  }
  return r;
}

// ---------- tiny deterministic noise ----------
static uint32_t rng = 12345;
static float noise(float amp) {
  rng = rng * 1664525u + 1013904223u;
  return ((rng >> 8) & 0xFFFF) / 65535.0f * 2.0f * amp - amp;
}

// ---------- synthetic scenario builders (100 Hz) ----------
static void riding(std::vector<Row>& v, uint32_t& t, int ms, float ax = 0.0f) {
  for (int i = 0; i < ms; i += 10, t += 10)
    v.push_back({t, {ax + noise(0.10f), noise(0.10f), 1.0f + noise(0.15f), noise(20), noise(20), noise(20)}});
}
static void still(std::vector<Row>& v, uint32_t& t, int ms) {
  for (int i = 0; i < ms; i += 10, t += 10)
    v.push_back({t, {noise(0.01f), noise(0.01f), 1.0f + noise(0.01f), noise(0.5f), noise(0.5f), noise(0.5f)}});
}
static void impact(std::vector<Row>& v, uint32_t& t, float g) {
  for (int i = 0; i < 3; i++, t += 10) v.push_back({t, {g, 0.5f, 0.5f, 200, 150, 100}});
}
static void tumble(std::vector<Row>& v, uint32_t& t, int ms) {
  for (int i = 0; i < ms; i += 10, t += 10)
    v.push_back({t, {noise(1.5f), noise(1.5f), 1.0f + noise(1.5f), noise(250), noise(250), noise(250)}});
}

static int failures = 0;
static void check(const char* name, bool ok) {
  printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) failures++;
}

static int replay(const char* path, long cancelAfter) {
  FILE* f = fopen(path, "r");
  if (!f) { perror(path); return 2; }
  std::vector<Row> rows;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    unsigned t; float a, b, c, d, e, g;
    if (sscanf(line, "%u,%f,%f,%f,%f,%f,%f", &t, &a, &b, &c, &d, &e, &g) == 7) rows.push_back({t, {a, b, c, d, e, g}});
  }
  fclose(f);
  printf("Replaying %s (%zu samples, cancel-after=%ld ms)\n", path, rows.size(), cancelAfter);
  Result r = run(rows, cancelAfter, true);
  printf("  summary: brakes=%d countdowns=%d cancelled=%d confirmed=%d\n", r.brakes, r.countdowns, r.cancelled, r.confirmed);
  return 0;
}

int main(int argc, char** argv) {
  if (argc >= 3 && strcmp(argv[1], "--replay") == 0) {
    long cancelAfter = -1;
    for (int i = 3; i + 1 < argc; i++)
      if (strcmp(argv[i], "--cancel-after") == 0) cancelAfter = atol(argv[i + 1]);
    return replay(argv[2], cancelAfter);
  }

  { // 1. normal riding: nothing fires
    std::vector<Row> v; uint32_t t = 1000;
    riding(v, t, 30000);
    Result r = run(v, -1, false);
    check("normal riding -> no events", r.brakes == 0 && r.countdowns == 0);
  }
  { // 2. hard braking: one event
    std::vector<Row> v; uint32_t t = 1000;
    riding(v, t, 5000); riding(v, t, 400, -0.6f); riding(v, t, 5000);
    Result r = run(v, -1, false);
    check("hard braking -> exactly one HARD_BRAKE", r.brakes == 1 && r.countdowns == 0);
  }
  { // 3. gentle slowdown: ignored
    std::vector<Row> v; uint32_t t = 1000;
    riding(v, t, 5000); riding(v, t, 2000, -0.15f); riding(v, t, 5000);
    check("gentle slowdown -> no HARD_BRAKE", run(v, -1, false).brakes == 0);
  }
  { // 4. crash, no cancel: countdown then confirmed after 10 s
    std::vector<Row> v; uint32_t t = 1000;
    riding(v, t, 5000); impact(v, t, 9.0f); tumble(v, t, 800); still(v, t, 16000);
    Result r = run(v, -1, false);
    check("crash + stillness -> countdown starts", r.countdowns == 1);
    check("no cancel -> CRASH_CONFIRMED after 10 s",
          r.confirmed == 1 && r.tConfirmed - r.tCountdown >= COUNTDOWN_MS && r.tConfirmed - r.tCountdown <= COUNTDOWN_MS + 20);
  }
  { // 5. crash, rider cancels at 4 s
    std::vector<Row> v; uint32_t t = 1000;
    riding(v, t, 5000); impact(v, t, 9.0f); tumble(v, t, 800); still(v, t, 16000);
    Result r = run(v, 4000, false);
    check("cancel within 10 s -> CRASH_CANCELLED, never confirmed", r.cancelled == 1 && r.confirmed == 0);
  }
  { // 6. pothole spike while riding: no countdown
    std::vector<Row> v; uint32_t t = 1000;
    riding(v, t, 5000); impact(v, t, 4.6f); riding(v, t, 15000);
    Result r = run(v, -1, false);
    check("spike but rider keeps moving -> no countdown", r.countdowns == 0);
  }
  { // 7. helmet put down gently (no spike): no countdown
    std::vector<Row> v; uint32_t t = 1000;
    riding(v, t, 5000); still(v, t, 20000);
    check("stationary without impact -> no countdown", run(v, -1, false).countdowns == 0);
  }

  printf(failures ? "\n%d test(s) FAILED\n" : "\nAll tests passed\n", failures);
  return failures ? 1 : 0;
}
