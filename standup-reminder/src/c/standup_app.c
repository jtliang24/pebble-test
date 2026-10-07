#include <pebble.h>
#include "keys.h"

#define STATUS_REFRESH_DELAY_MS 500

static Window *s_main_window;
static TextLayer *s_title_layer;
static TextLayer *s_countdown_layer;
static TextLayer *s_detail_layer;

static Window *s_reminder_window;
static TextLayer *s_reminder_layer;
static AppTimer *s_dismiss_timer;

static bool s_launched_by_worker;

// Last WMSG_STATE from the worker
static bool s_have_state;
static AppWorkerMessage s_state;

// --- Status UI ---

static void update_status(void) {
  static char s_countdown_buffer[32];
  static char s_detail_buffer[96];

  bool enabled = settings_load_enabled();
  bool running = app_worker_is_running();
  int threshold = settings_load_activity_threshold();

  if (!enabled) {
    snprintf(s_countdown_buffer, sizeof(s_countdown_buffer), "Off");
  } else if (!running || !s_have_state) {
    snprintf(s_countdown_buffer, sizeof(s_countdown_buffer), "Starting...");
  } else if (s_state.data2 & WSTATE_FLAG_PAUSED) {
    snprintf(s_countdown_buffer, sizeof(s_countdown_buffer), "Paused until %02d:00",
             settings_load_active_start_hour());
  } else {
    snprintf(s_countdown_buffer, sizeof(s_countdown_buffer), "Stand up in %d min",
             s_state.data0);
  }

  int recent = s_have_state ? s_state.data1 : 0;
  snprintf(s_detail_buffer, sizeof(s_detail_buffer),
           "Recent steps: %d/%d\nEvery %d min, %02d:00-%02d:00\nWorker: %s",
           recent, threshold, settings_load_interval_min(),
           settings_load_active_start_hour(), settings_load_active_end_hour(),
           running ? "running" : "stopped");

  text_layer_set_text(s_countdown_layer, s_countdown_buffer);
  text_layer_set_text(s_detail_layer, s_detail_buffer);
}

static void request_state(void) {
  if (app_worker_is_running()) {
    AppWorkerMessage msg = { 0 };
    app_worker_send_message(WMSG_REQUEST_STATE, &msg);
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  request_state();
  update_status();
}

static void delayed_refresh_callback(void *context) {
  if (window_stack_contains_window(s_main_window)) {
    request_state();
    update_status();
  }
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = grect_inset(layer_get_bounds(window_layer),
                             GEdgeInsets(PBL_IF_ROUND_ELSE(20, 4), PBL_IF_ROUND_ELSE(16, 4)));

  s_title_layer = text_layer_create(GRect(bounds.origin.x, bounds.origin.y, bounds.size.w, 24));
  text_layer_set_text(s_title_layer, "Standup Reminder");
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_title_layer, GColorClear);

  s_countdown_layer = text_layer_create(
      GRect(bounds.origin.x, bounds.origin.y + 26, bounds.size.w, 64));
  text_layer_set_font(s_countdown_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_countdown_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_countdown_layer, GTextOverflowModeWordWrap);
  text_layer_set_background_color(s_countdown_layer, GColorClear);

  s_detail_layer = text_layer_create(
      GRect(bounds.origin.x, bounds.origin.y + 92, bounds.size.w, bounds.size.h - 92));
  text_layer_set_font(s_detail_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_detail_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_detail_layer, GTextOverflowModeWordWrap);
  text_layer_set_background_color(s_detail_layer, GColorClear);

  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_countdown_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_detail_layer));

  update_status();
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_countdown_layer);
  text_layer_destroy(s_detail_layer);
}

// --- Reminder ---

static void dismiss_timer_callback(void *context) {
  s_dismiss_timer = NULL;
  if (s_launched_by_worker) {
    // Exit the app and return to the watchface
    window_stack_pop_all(true);
  } else {
    window_stack_remove(s_reminder_window, true);
  }
}

static void reminder_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_reminder_layer = text_layer_create(GRect(PBL_IF_ROUND_ELSE(16, 4), bounds.size.h / 2 - 40,
                                             bounds.size.w - 2 * PBL_IF_ROUND_ELSE(16, 4), 80));
  text_layer_set_text(s_reminder_layer, "Time to stand up!");
  text_layer_set_font(s_reminder_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_reminder_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_reminder_layer, GTextOverflowModeWordWrap);
  text_layer_set_background_color(s_reminder_layer, GColorClear);
  text_layer_set_text_color(s_reminder_layer, GColorWhite);
  layer_add_child(window_layer, text_layer_get_layer(s_reminder_layer));
}

static void reminder_window_unload(Window *window) {
  if (s_dismiss_timer) {
    app_timer_cancel(s_dismiss_timer);
    s_dismiss_timer = NULL;
  }
  text_layer_destroy(s_reminder_layer);
}

static void show_reminder(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "reminder: vibrating");
  vibes_double_pulse();

  if (!window_stack_contains_window(s_reminder_window)) {
    window_stack_push(s_reminder_window, true);
  }
  if (s_dismiss_timer) {
    app_timer_reschedule(s_dismiss_timer, REMINDER_AUTO_DISMISS_MS);
  } else {
    s_dismiss_timer = app_timer_register(REMINDER_AUTO_DISMISS_MS, dismiss_timer_callback, NULL);
  }
}

// --- Worker ---

static void worker_message_handler(uint16_t type, AppWorkerMessage *data) {
  switch (type) {
    case WMSG_STATE:
      s_state = *data;
      s_have_state = true;
      if (window_stack_contains_window(s_main_window)) {
        update_status();
      }
      break;
    case WMSG_REMIND_NOW: {
      APP_LOG(APP_LOG_LEVEL_INFO, "WMSG_REMIND_NOW: sending ACK");
      AppWorkerMessage ack = { 0 };
      app_worker_send_message(WMSG_REMIND_ACK, &ack);
      show_reminder();
      break;
    }
  }
}

static void ensure_worker_running(void) {
  if (!app_worker_is_running()) {
    AppWorkerResult result = app_worker_launch();
    APP_LOG(APP_LOG_LEVEL_INFO, "app_worker_launch: %d", (int)result);
  }
}

// --- Clay / AppMessage ---

// Clay sends sliders/toggles as integers but select values as strings
static bool read_int_tuple(DictionaryIterator *iter, uint32_t key, int32_t *out) {
  Tuple *t = dict_find(iter, key);
  if (!t) {
    return false;
  }
  switch (t->type) {
    case TUPLE_CSTRING:
      if (strcmp(t->value->cstring, "true") == 0) {
        *out = 1;
      } else {
        *out = atoi(t->value->cstring);
      }
      APP_LOG(APP_LOG_LEVEL_INFO, "key %d: cstring \"%s\" -> %d", (int)key, t->value->cstring,
              (int)*out);
      return true;
    case TUPLE_INT:
      *out = t->length == 1 ? t->value->int8 : t->length == 2 ? t->value->int16 : t->value->int32;
      break;
    case TUPLE_UINT:
      *out = t->length == 1 ? t->value->uint8 : t->length == 2 ? t->value->uint16 : (int32_t)t->value->uint32;
      break;
    default:
      return false;
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "key %d: int(%d bytes) -> %d", (int)key, t->length, (int)*out);
  return true;
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  int32_t value;
  if (read_int_tuple(iter, MESSAGE_KEY_INTERVAL_MINUTES, &value)) {
    persist_write_int(PKEY_INTERVAL_MIN, value);
  }
  if (read_int_tuple(iter, MESSAGE_KEY_ACTIVITY_THRESHOLD, &value)) {
    persist_write_int(PKEY_ACTIVITY_THRESHOLD, value);
  }
  if (read_int_tuple(iter, MESSAGE_KEY_ENABLED, &value)) {
    persist_write_int(PKEY_ENABLED, value != 0);
  }
  if (read_int_tuple(iter, MESSAGE_KEY_ACTIVE_START_HOUR, &value)) {
    persist_write_int(PKEY_ACTIVE_START_HOUR, value);
  }
  if (read_int_tuple(iter, MESSAGE_KEY_ACTIVE_END_HOUR, &value)) {
    persist_write_int(PKEY_ACTIVE_END_HOUR, value);
  }

  if (!settings_load_enabled()) {
    APP_LOG(APP_LOG_LEVEL_INFO, "disabled: app_worker_kill");
    app_worker_kill();
    s_have_state = false;
  } else {
    ensure_worker_running();
    AppWorkerMessage msg = { 0 };
    app_worker_send_message(WMSG_SETTINGS_CHANGED, &msg);
  }

  if (window_stack_contains_window(s_main_window)) {
    update_status();
    // app_worker_is_running() lags a kill/launch slightly; refresh again shortly
    app_timer_register(STATUS_REFRESH_DELAY_MS, delayed_refresh_callback, NULL);
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "inbox dropped: %d", (int)reason);
}

// --- App lifecycle ---

static void init(void) {
  s_launched_by_worker = launch_reason() == APP_LAUNCH_WORKER;

  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  s_reminder_window = window_create();
  window_set_background_color(s_reminder_window, PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack));
  window_set_window_handlers(s_reminder_window, (WindowHandlers) {
    .load = reminder_window_load,
    .unload = reminder_window_unload
  });

  app_worker_message_subscribe(worker_message_handler);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_open(256, 64);

  if (s_launched_by_worker) {
    // Only the reminder on the stack, so dismissing it exits to the watchface
    APP_LOG(APP_LOG_LEVEL_INFO, "launched by worker");
    show_reminder();
  } else {
    if (settings_load_enabled()) {
      ensure_worker_running();
    }
    window_stack_push(s_main_window, true);
    request_state();
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  }
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  app_worker_message_unsubscribe();
  window_destroy(s_reminder_window);
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
