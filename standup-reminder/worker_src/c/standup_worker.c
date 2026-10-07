#include <pebble_worker.h>
#include "../../src/c/keys.h"

#define MAX_SAMPLES 16

// Settings (reloaded on WMSG_SETTINGS_CHANGED)
static int s_interval_min;
static int s_activity_threshold;
static bool s_enabled;
static int s_start_hour;
static int s_end_hour;

// Live countdown state: RAM only, shared with the app via WMSG_STATE
static time_t s_deadline;
static bool s_paused;
static AppTimer *s_fire_timer;
static AppTimer *s_ack_timer;

// Step samples, oldest first
typedef struct {
  time_t t;
  HealthValue steps_today;
} StepSample;

static StepSample s_samples[MAX_SAMPLES];
static int s_sample_count;

static void load_settings(void) {
  s_interval_min = settings_load_interval_min();
  s_activity_threshold = settings_load_activity_threshold();
  s_enabled = settings_load_enabled();
  s_start_hour = settings_load_active_start_hour();
  s_end_hour = settings_load_active_end_hour();
  APP_LOG(APP_LOG_LEVEL_INFO, "settings: interval=%d threshold=%d enabled=%d hours=%d-%d",
          s_interval_min, s_activity_threshold, s_enabled, s_start_hour, s_end_hour);
}

static bool now_in_active_hours(time_t now) {
  struct tm *t = localtime(&now);
  return in_active_hours(t->tm_hour, s_start_hour, s_end_hour);
}

// --- Recent-steps measurement ---

static void drop_oldest_samples(int n) {
  if (n <= 0) {
    return;
  }
  memmove(&s_samples[0], &s_samples[n], (s_sample_count - n) * sizeof(StepSample));
  s_sample_count -= n;
}

static void record_sample(time_t now, HealthValue steps_today) {
  if (s_sample_count > 0 && steps_today < s_samples[s_sample_count - 1].steps_today) {
    // Midnight rollover (or data rewrite): differences across it are meaningless
    APP_LOG(APP_LOG_LEVEL_INFO, "steps decreased, clearing samples");
    s_sample_count = 0;
  }

  // Keep only the newest sample at or before the window start (the baseline) and everything after
  int baseline = -1;
  for (int i = 0; i < s_sample_count; i++) {
    if (s_samples[i].t <= now - ACTIVITY_WINDOW_SEC) {
      baseline = i;
    }
  }
  drop_oldest_samples(baseline);

  if (s_sample_count == MAX_SAMPLES) {
    drop_oldest_samples(1);
  }
  s_samples[s_sample_count++] = (StepSample) { .t = now, .steps_today = steps_today };
}

// Steps since the newest sample at least ACTIVITY_WINDOW_SEC old; falls back to the oldest sample.
static HealthValue recent_steps(time_t now) {
  if (s_sample_count == 0) {
    return 0;
  }
  int baseline = 0;
  for (int i = 0; i < s_sample_count; i++) {
    if (s_samples[i].t <= now - ACTIVITY_WINDOW_SEC) {
      baseline = i;
    }
  }
  HealthValue recent = s_samples[s_sample_count - 1].steps_today - s_samples[baseline].steps_today;
  return recent < 0 ? 0 : recent;
}

// --- Messaging ---

static void send_state(void) {
  time_t now = time(NULL);
  AppWorkerMessage msg = { 0 };

  if (s_enabled) {
    int32_t secs = s_deadline - now;
    int32_t mins = secs <= 0 ? 0 : (secs + 59) / 60;
    msg.data0 = mins > UINT16_MAX ? UINT16_MAX : mins;
  }

  HealthValue recent = recent_steps(now);
  msg.data1 = recent > UINT16_MAX ? UINT16_MAX : recent;
  msg.data2 = (s_paused ? WSTATE_FLAG_PAUSED : 0) | (s_enabled ? 0 : WSTATE_FLAG_DISABLED);
  app_worker_send_message(WMSG_STATE, &msg);
}

// --- Scheduling ---

static void schedule_from_now(void);

static void set_fire_timer(time_t now, AppTimerCallback callback) {
  if (s_fire_timer) {
    app_timer_cancel(s_fire_timer);
  }
  int32_t secs = s_deadline - now;
  s_fire_timer = app_timer_register((secs <= 0 ? 1 : secs) * 1000, callback, NULL);
}

static void on_ack_timeout(void *context) {
  s_ack_timer = NULL;
  APP_LOG(APP_LOG_LEVEL_INFO, "no REMIND_ACK, app not open: worker_launch_app");
  worker_launch_app();
}

static void on_fire(void *context) {
  s_fire_timer = NULL;
  if (now_in_active_hours(time(NULL))) {
    // Ask an open app to remind; if it doesn't ACK in time, launch it
    APP_LOG(APP_LOG_LEVEL_INFO, "reminder due: sending REMIND_NOW");
    AppWorkerMessage msg = { 0 };
    app_worker_send_message(WMSG_REMIND_NOW, &msg);
    if (s_ack_timer) {
      app_timer_cancel(s_ack_timer);
    }
    s_ack_timer = app_timer_register(REMIND_ACK_TIMEOUT_MS, on_ack_timeout, NULL);
  }
  schedule_from_now();
}

static void on_window_open(void *context) {
  s_fire_timer = NULL;
  APP_LOG(APP_LOG_LEVEL_INFO, "active hours window opened");
  schedule_from_now();
}

static void schedule_from_now(void) {
  time_t now = time(NULL);

  if (!s_enabled) {
    if (s_fire_timer) {
      app_timer_cancel(s_fire_timer);
      s_fire_timer = NULL;
    }
    if (s_ack_timer) {
      app_timer_cancel(s_ack_timer);
      s_ack_timer = NULL;
    }
    s_paused = false;
    s_deadline = 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "disabled: timers cancelled");
  } else if (now_in_active_hours(now)) {
    s_paused = false;
    s_deadline = now + s_interval_min * 60;
    set_fire_timer(now, on_fire);
    APP_LOG(APP_LOG_LEVEL_INFO, "countdown reset: reminder in %d min", s_interval_min);
  } else {
    s_paused = true;
    s_deadline = next_active_start(now, s_start_hour);
    set_fire_timer(now, on_window_open);
    APP_LOG(APP_LOG_LEVEL_INFO, "outside active hours: paused for %d min",
            (int)((s_deadline - now + 59) / 60));
  }

  send_state();
}

// --- Event handlers ---

static void on_health_event(HealthEventType event, void *context) {
  time_t now = time(NULL);

  if (event == HealthEventSignificantUpdate) {
    APP_LOG(APP_LOG_LEVEL_INFO, "significant health update: clearing samples");
    s_sample_count = 0;
    record_sample(now, health_service_sum_today(HealthMetricStepCount));
    return;
  }
  if (event != HealthEventMovementUpdate) {
    return;
  }

  record_sample(now, health_service_sum_today(HealthMetricStepCount));
  HealthValue recent = recent_steps(now);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "movement: recent=%d threshold=%d", (int)recent, s_activity_threshold);

  if (s_enabled && !s_paused && recent >= s_activity_threshold) {
    APP_LOG(APP_LOG_LEVEL_INFO, "active (recent=%d): resetting", (int)recent);
    schedule_from_now();
  }
}

// Guards against clock changes (AppTimers are relative, the active-hours window is wall-clock).
// Returns true if it rescheduled (which also sends WMSG_STATE).
static bool resync_with_clock(void) {
  if (!s_enabled) {
    return false;
  }
  time_t now = time(NULL);
  bool active = now_in_active_hours(now);
  int32_t max_wait = s_paused ? 24 * 3600 : s_interval_min * 60;
  bool deadline_inconsistent = s_deadline - now > max_wait || s_deadline < now - 60;
  if (active == s_paused || deadline_inconsistent) {
    APP_LOG(APP_LOG_LEVEL_INFO, "schedule out of sync with clock, rescheduling");
    schedule_from_now();
    return true;
  }
  return false;
}

// Window boundaries are whole hours, so checking hourly is enough.
static void on_hour_tick(struct tm *tick_time, TimeUnits units_changed) {
  resync_with_clock();
}

static void on_app_msg(uint16_t type, AppWorkerMessage *data) {
  switch (type) {
    case WMSG_SETTINGS_CHANGED:
      APP_LOG(APP_LOG_LEVEL_INFO, "WMSG_SETTINGS_CHANGED");
      load_settings();
      schedule_from_now();
      break;
    case WMSG_REQUEST_STATE:
      APP_LOG(APP_LOG_LEVEL_DEBUG, "WMSG_REQUEST_STATE");
      if (!resync_with_clock()) {
        send_state();
      }
      break;
    case WMSG_REMIND_ACK:
      APP_LOG(APP_LOG_LEVEL_INFO, "WMSG_REMIND_ACK");
      if (s_ack_timer) {
        app_timer_cancel(s_ack_timer);
        s_ack_timer = NULL;
      }
      break;
  }
}

static void init(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "worker started");
  load_settings();
  if (!health_service_events_subscribe(on_health_event, NULL)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "health_service_events_subscribe failed");
  }
  app_worker_message_subscribe(on_app_msg);
  tick_timer_service_subscribe(HOUR_UNIT, on_hour_tick);
  schedule_from_now();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  app_worker_message_unsubscribe();
  health_service_events_unsubscribe();
}

int main(void) {
  init();
  worker_event_loop();
  deinit();
}
