#pragma once
// Shared between the foreground app and the background worker.
// Include after <pebble.h> (app) or <pebble_worker.h> (worker).

// --- Persist keys (settings written by the app from Clay, read by the worker) ---
#define PKEY_INTERVAL_MIN        1
#define PKEY_ACTIVITY_THRESHOLD  2
#define PKEY_ENABLED             3
#define PKEY_ACTIVE_START_HOUR   4
#define PKEY_ACTIVE_END_HOUR     5

#define DEFAULT_INTERVAL_MIN        30
#define DEFAULT_ACTIVITY_THRESHOLD  50
#define DEFAULT_ENABLED             true
#define DEFAULT_ACTIVE_START_HOUR   9
#define DEFAULT_ACTIVE_END_HOUR     18

// --- Tunables ---
#define ACTIVITY_WINDOW_SEC       120    // rolling window for movement detection
#define REMINDER_AUTO_DISMISS_MS  30000
#define REMIND_ACK_TIMEOUT_MS     1500

// --- Worker message types ---
enum {
  WMSG_SETTINGS_CHANGED = 1,  // app -> worker
  WMSG_REQUEST_STATE,         // app -> worker
  WMSG_STATE,                 // worker -> app: data0 = minutes left, data1 = recent steps, data2 = flags
  WMSG_REMIND_NOW,            // worker -> app
  WMSG_REMIND_ACK,            // app -> worker
};

#define WSTATE_FLAG_PAUSED    (1 << 0)  // outside active hours; data0 = minutes until window opens
#define WSTATE_FLAG_DISABLED  (1 << 1)

// --- Settings ---
static inline int32_t settings_read(uint32_t key, int32_t def) {
  return persist_exists(key) ? persist_read_int(key) : def;
}

static inline int settings_load_interval_min(void) {
  int v = settings_read(PKEY_INTERVAL_MIN, DEFAULT_INTERVAL_MIN);
  return v < 1 ? 1 : (v > 24 * 60 ? 24 * 60 : v);
}

static inline int settings_load_activity_threshold(void) {
  int v = settings_read(PKEY_ACTIVITY_THRESHOLD, DEFAULT_ACTIVITY_THRESHOLD);
  return v < 1 ? 1 : v;
}

static inline bool settings_load_enabled(void) {
  return settings_read(PKEY_ENABLED, DEFAULT_ENABLED) != 0;
}

static inline int settings_clamp_hour(int h) {
  return (h < 0 || h > 23) ? 0 : h;
}

static inline int settings_load_active_start_hour(void) {
  return settings_clamp_hour(settings_read(PKEY_ACTIVE_START_HOUR, DEFAULT_ACTIVE_START_HOUR));
}

static inline int settings_load_active_end_hour(void) {
  return settings_clamp_hour(settings_read(PKEY_ACTIVE_END_HOUR, DEFAULT_ACTIVE_END_HOUR));
}

// --- Active hours ---
// start == end: always active. start < end: [start, end). start > end: wraps past midnight.
static inline bool in_active_hours(int hour, int start, int end) {
  if (start == end) {
    return true;
  }
  if (start < end) {
    return hour >= start && hour < end;
  }
  return hour >= start || hour < end;
}

// Next time the active window opens (today at start:00, or tomorrow if that has passed).
static inline time_t next_active_start(time_t now, int start) {
  time_t t = time_start_of_today() + start * 3600;
  if (t <= now) {
    t += 24 * 3600;
  }
  return t;
}
