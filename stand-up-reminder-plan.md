# Standup Reminder — Pebble Watchapp + Background Worker

## Context

The user wants a **stand-up reminder app** for Pebble that:
1. Vibrates periodically to remind them to stand up, with a **configurable interval**.
2. **Resets the countdown** whenever it detects real movement, so the reminder only fires after a genuine sedentary stretch. Movement is detected as a configurable number of steps within a short rolling window (the "activity threshold"), which doubles as a noise filter against false-positive step jitter.
3. Only reminds during a **configurable active-hours window** (e.g. 9:00–18:00), so it stays silent overnight.

Decisions confirmed with the user:
- **1B** — implement as a **watchapp + background worker** (keeps the user's normal watchface; the reminder logic runs in the background).
- **2B** — configuration via a **Clay phone settings page** (Pebble mobile app), not on-watch buttons.
- **3 (event-driven continuous reset)** — the worker subscribes to health movement events. On each movement update it computes steps over a short rolling window; if that meets the configurable activity threshold, it treats the user as active and **resets the countdown**. The reminder fires only if a full interval elapses with no qualifying activity. This is a true "sedentary timer" (like Apple Watch / Fitbit stand reminders) and is the user's chosen design.
- **4 (night suppression = active-hours window)** — reminders fire only between a configurable start and end hour (Clay settings). Not chosen: sleep-detection suppression and Quiet Time suppression.
- **5 (dismissal = auto-exit)** — the reminder screen auto-dismisses after ~30 s; Back dismisses immediately.

### Why event-driven (vs. polling or evaluate-at-expiry)
The Pebble pedometer runs continuously system-wide regardless of our app, so reading steps powers up no extra hardware. The only background battery cost is CPU wakeups. Subscribing to `health_service_events_subscribe(HealthEventMovementUpdate)` means the worker wakes **only when step data actually changes**: near-silent while the user is sedentary (the case where we must *not* reset), active only while they move (when we *do* reset). This gives the correct recency semantics at close to the battery cost of a once-per-interval timer, far cheaper than a blind every-minute poll. To protect battery/flash further, live countdown state lives in RAM and is shared with the app via worker messages (no per-event `persist_write`).

### Key SDK facts (verified against installed SDK 4.33.1, pebble-tool 5.0.40)
Line numbers are from `.local/.pebble-sdk/SDKs/4.33.1/sdk-core/pebble/basalt/include/`. Every API used below is present, with the same signatures, in both 4.9.169 and 4.33.1.

Worker (`pebble_worker.h`) can: subscribe to health events (`health_service_events_subscribe:1016`, `HealthEventMovementUpdate:991`, `HealthEventSignificantUpdate` fires on day change), read today's step total (`health_service_sum_today:722`), run one-shot timers (`app_timer_register:1904`, `app_timer_reschedule:1911`, `app_timer_cancel:1916`), do local-time math (`localtime:2177`, `time_start_of_today:2220`), use `persist_*`, and message the app (`app_worker_send_message:1880`). The worker **cannot vibrate or draw UI** (`vibes_*` / window APIs are absent) — its only way to alert is `worker_launch_app()` (`:1817`). App side (`pebble.h`) has `app_worker_is_running/launch/kill` (`:2915,2920,2925`) and `launch_reason()` (`:3314`, `APP_LAUNCH_WORKER:3297`). So: the worker decides when a reminder is due, then hands off to the foreground app, which vibrates.

**Do not use `health_service_sum()` for the rolling window.** Its doc (`:715`) says: *"The value returned will be based on daily totals, weighted for the length of the specified time range."* A 2-minute query returns a prorated slice of today's total, not the steps actually taken in those 2 minutes. (This note is also in 4.9.169; the earlier version of this plan got it wrong.) `health_service_get_minute_history()` would give real per-minute data, but it is **app-only** (not in `pebble_worker.h`). The worker therefore derives recent steps itself from `health_service_sum_today()` samples (see the worker section).

What's new in 4.33.1 that matters here:
- New APIs: `alarm_service_peek_next`, `persist_get_max_size`, HRV (`HealthEventHRVUpdate`, `health_service_set_hrv_sample_period`), quick-launch/touch/backlight APIs. **None of them are needed for this app.** On older platforms (aplite/basalt/chalk/diorite) they are stub macros returning `0`, so don't rely on them without platform guards.
- `quiet_time_is_active()` exists, but only for the app; the worker can't check it. The user didn't choose Quiet Time suppression, so it isn't used.
- pebble-tool 5.0.40 adds emulator health controls: `pebble emu-steps <count>` sets today's step total, and there's also `emu-sleep`. With `emu-set-time` (`HH:MM:SS`), the reset path and active-hours path can now be tested fully in the emulator (see Verification).
- `pebble-clay` is still at **1.0.4** (last published 2022). It is pure PebbleKit JS and doesn't depend on the platform, so it should work on emery/gabbro/flint, but it hasn't been tested on them yet. Verify early (Verification step 0).

## Approach

Create a **new sibling project** `standup-reminder/` next to `watchface-c/`. It reuses the existing build tooling unchanged: `build_and_install.sh` auto-selects the most-recently-modified project containing a `wscript`, and the stock `wscript` already builds a worker whenever a `worker_src/` directory exists (`watchface-c/wscript:29,39-44`) and bundles PebbleKit JS from `src/pkjs/` (`watchface-c/wscript:50-54`). No font/image resources are needed — use system fonts (`fonts_get_system_font(...)`).

### Files to create (mirrors `watchface-c/` layout)

```
standup-reminder/
  wscript                       # copy of watchface-c/wscript verbatim
  package.json                  # new UUID, capabilities, messageKeys, clay dep
  src/c/standup_app.c           # foreground app: UI, vibration, Clay/AppMessage, launches worker
  worker_src/c/standup_worker.c # background worker: event-driven timing + step evaluation + active hours
  src/pkjs/index.js             # wires up Clay
  src/pkjs/config.js            # Clay config page definition
  src/common/keys.h             # shared persist keys + worker-message types + defaults + active-hours helpers
```

Note: the stock `wscript` only globs `src/common/**/*.js` for the JS bundle; `keys.h` is a C header included by relative path (`#include "../../src/common/keys.h"` from `worker_src/c/`, `"../common/keys.h"` from `src/c/`). Check that both app and worker compile with it.

### `package.json` (`standup-reminder/package.json`)
Base it on `watchface-c/package.json` with these changes:
- New `uuid` — generate at implementation time with `uuidgen` (lowercased).
- `displayName`: "Standup Reminder".
- `watchapp.watchface`: `false` (it is an app, not a face).
- Add `"capabilities": ["health", "configurable"]` — `health` is **required** for step access in both app and worker; `configurable` shows the settings gear in the phone app for Clay.
- `messageKeys`: `["INTERVAL_MINUTES", "ACTIVITY_THRESHOLD", "ENABLED", "ACTIVE_START_HOUR", "ACTIVE_END_HOUR"]` (replaces `"dummy"`).
- Add `"dependencies": { "pebble-clay": "^1.0.4" }` (Clay is pulled in via `enableMultiJS`).
- `targetPlatforms`: drop `aplite` (no health sensor); keep `basalt, chalk, diorite, emery, flint, gabbro`. `emery` is the emulator default.
- Remove the `resources.media` font entries (none needed).

### Shared keys & state (`src/common/keys.h`)
- **Settings** (persisted by app from Clay, read by worker): `PKEY_INTERVAL_MIN` (default 30), `PKEY_ACTIVITY_THRESHOLD` (default 50, steps within the rolling window), `PKEY_ENABLED` (default true), `PKEY_ACTIVE_START_HOUR` (default 9), `PKEY_ACTIVE_END_HOUR` (default 18).
- **Constant:** `ACTIVITY_WINDOW_SEC` = 120 (rolling window for movement detection; a tunable; using a ~2 min window avoids a single-minute test missing a steady slow walk). `REMINDER_AUTO_DISMISS_MS` = 30000. `REMIND_ACK_TIMEOUT_MS` = 1500.
- **Worker-message types:**
  - `WMSG_SETTINGS_CHANGED` (app→worker)
  - `WMSG_REQUEST_STATE` (app→worker)
  - `WMSG_STATE` (worker→app). `AppWorkerMessage` fields are `uint16_t`, so it sends `data0` = minutes until the next reminder (or until active hours start), rounded up; `data1` = recent steps in the window; `data2` = flags (bit0 = paused, outside active hours)
  - `WMSG_REMIND_NOW` (worker→app)
  - `WMSG_REMIND_ACK` (app→worker; see the reminder handshake below)
- **Active-hours helpers** (pure functions, shared): `in_active_hours(hour, start, end)`. If `start == end`, it's always active. If `start < end`, it's active for `start <= hour < end`. If `start > end`, the window wraps past midnight (e.g. 22→6). There's also `next_active_start(now, start)`, which returns `time_start_of_today() + start*3600`, plus one day if that time has passed.
- Helper inlines `settings_load_*` read from persist with the defaults above.
- Live countdown state is **RAM-only in the worker** (not persisted) and shared on demand via worker messages — avoids per-event flash writes.

### Background worker (`worker_src/c/standup_worker.c`)
- `worker_main()` → init → `worker_event_loop()`.
- **RAM state:** `time_t deadline`, `bool paused` (outside active hours), `AppTimer *fire_timer`, `AppTimer *ack_timer`, and a small step-sample ring buffer `struct { time_t t; HealthValue steps_today; } samples[16]`.
- **Init:** load settings, subscribe to `health_service_events_subscribe(on_health_event, NULL)` and `app_worker_message_subscribe(on_app_msg)`, then call `schedule_from_now()`.
- **`schedule_from_now()`** (one function that every reset or settings change goes through):
  - If `!ENABLED`, cancel timers and return.
  - If inside active hours: `paused = false; deadline = now + interval_min*60`; register or reschedule `fire_timer` → `on_fire`.
  - If outside: `paused = true; deadline = next_active_start(...)`; register or reschedule `fire_timer` for `deadline - now` → `on_window_open`, which just calls `schedule_from_now()`. So the first reminder of the day comes one full interval after the window opens.
- **Recent-steps measurement (replaces `health_service_sum` window):**
  - On each `HealthEventMovementUpdate`, push `{now, health_service_sum_today(HealthMetricStepCount)}` to the ring buffer.
  - `recent = latest.steps_today - baseline.steps_today`. The baseline is the newest sample with `t <= now - ACTIVITY_WINDOW_SEC`. If there's none, use the oldest sample (conservative undercount).
  - Clear the buffer on `HealthEventSignificantUpdate` (day change, data rewrite), or if `steps_today` ever decreases (midnight rollover).
  - Evict entries older than what the baseline lookup needs.
- **`on_health_event`:** always record the sample. If `ENABLED && !paused && recent >= activity_threshold`, the user is active, so **reset** with `schedule_from_now()`. Rescheduling is cheap and nothing is persisted.
- **`on_fire`** (timer expired, so a full interval passed with no qualifying activity):
  - If still inside active hours, start the **reminder handshake**: send `app_worker_send_message(WMSG_REMIND_NOW)` and start `ack_timer` (`REMIND_ACK_TIMEOUT_MS`). If the app is open, it vibrates and replies `WMSG_REMIND_ACK`, which cancels `ack_timer`. If the timer expires without an ACK, the app isn't running, so call `worker_launch_app()`; the app sees `launch_reason() == APP_LAUNCH_WORKER`.
  - This avoids a double alert (message plus relaunch) when the app is already open, and it doesn't keep "app is open" state that could go stale.
  - Then `schedule_from_now()`. If the window has closed in the meantime, it pauses without reminding.
- **`on_app_msg`:**
  - `WMSG_SETTINGS_CHANGED`: reload settings, then `schedule_from_now()`. Turning reminders off is handled on the app side with `app_worker_kill()`.
  - `WMSG_REQUEST_STATE`: reply with `WMSG_STATE` (fields above).
  - `WMSG_REMIND_ACK`: cancel `ack_timer`.

### Foreground app (`src/c/standup_app.c`)
- `main()` → `init()` → `app_event_loop()` → `deinit()` (same shape as `watchface-c/src/c/watchface-c.c:148-152`).
- **Reminder delivery:** a reminder happens when `launch_reason() == APP_LAUNCH_WORKER` **or** a `WMSG_REMIND_NOW` worker message arrives. In the message case, reply `WMSG_REMIND_ACK` first. Then call `vibes_double_pulse()` (or a custom `VibePattern`) and push a "Time to stand up!" window.
- **Auto-dismiss:** the reminder window registers an `app_timer` for `REMINDER_AUTO_DISMISS_MS`. When it fires:
  - If the worker launched the app, `window_stack_pop_all(true)`, which exits the app and returns to the watchface.
  - If the user already had the app open, pop only the reminder window, back to the status screen.
  
  Back dismisses immediately in either case (the default Back behaviour). Cancel the timer in the window's unload handler.
- **Ensure the worker runs:** on a normal launch, if `ENABLED` and `!app_worker_is_running()`, call `app_worker_launch()`. Subscribe to `app_worker_message_subscribe` to receive `WMSG_REMIND_NOW` / `WMSG_STATE`.
- **Clay/AppMessage:** `app_message_open(...)`. The inbox-received handler reads all five message keys (`dict_find(iter, MESSAGE_KEY_*)`) and persists them. Read each tuple with a helper that accepts both integer and `TUPLE_CSTRING` (`atoi`) tuples, because Clay `select` values arrive as strings. Then notify the worker:
  - If turned **off**: `app_worker_kill()`.
  - If **on**: make sure it's running (`app_worker_launch()`), then `app_worker_send_message(WMSG_SETTINGS_CHANGED, NULL)`.
- **Status UI:** a system-font `TextLayer` window showing:
  - time until the next reminder, or "Paused until HH:00" when outside active hours
  - recent steps vs. threshold
  - enabled/worker state

  All of this comes from the `WMSG_STATE` reply to `WMSG_REQUEST_STATE`, so the app never computes steps itself. Refresh with `tick_timer_service_subscribe(MINUTE_UNIT, ...)` (pattern at `watchface-c/src/c/watchface-c.c:25-27,134`). This tick only runs while the app is in the foreground, so it costs nothing in the background.

### Clay configuration (`src/pkjs/`)
- `config.js`: export a Clay config array containing:
  - a **slider** for interval minutes (5–120, step 5)
  - a **slider** for the activity/step threshold (10–200, default 50)
  - a **toggle** for enabled
  - an "Active hours" section with two **select** dropdowns, start hour and end hour (0–23, shown as "09:00" etc., defaults 9 and 18), and a note that start = end means all day
  
  Each `messageKey` must equal the `package.json` `messageKeys`.
- `index.js`: `var Clay = require('pebble-clay'); var cfg = require('./config'); var clay = new Clay(cfg);` — Clay auto-handles `showConfiguration`/`webviewclosed` and sends values to the watch via AppMessage. Keep a `Pebble.addEventListener('ready', ...)` log (matches existing `watchface/src/pkjs/index.js`).

## Verification

Build and install with the existing flow (`build_and_install.sh` auto-selects the new dir because it's the most recently modified project):
```
cd /home/jtliang/Projects/pebble-test/standup-reminder
direnv exec . pebble build              # confirm app + worker (pebble-worker.elf) + pkjs all build
direnv exec . pebble install --emulator emery
direnv exec . pebble logs --emulator emery   # in a second terminal, throughout
```
(or run the root `build_and_install.sh` / the Zed "Build and Install" task.)

Settings can be pushed through Clay (`pebble emu-app-config`) or directly with `pebble send-app-message --int <id>=<value>`. The numeric message-key IDs are in `build/include/message_keys.auto.h`. All time-dependent steps below assume `pebble emu-set-time` puts the clock inside active hours unless stated otherwise.

0. **Clay on new platforms:** `pebble emu-app-config` on emery opens the page, shows all controls, and on save the app logs all five values with the right types. If Clay 1.0.4 misbehaves, fall back to `send-app-message` for testing and investigate before continuing.
1. **Worker launches & live state:** open the app. The status screen shows a countdown that decreases each minute, and the logs show the worker started and replied to `WMSG_REQUEST_STATE`.
2. **Reminder fires, app closed:** set interval = 1 min and threshold = 200, then exit to the watchface. After about 1 min, the app comes to the foreground and **vibrates once**, then returns to the watchface by itself after about 30 s. The logs show the ACK timeout, then `worker_launch_app`.
3. **Reminder fires, app open:** same settings, but leave the status screen open. The app vibrates **once** (no relaunch). The logs show `WMSG_REMIND_ACK`, and after about 30 s the reminder window pops back to the status screen.
4. **Event-driven reset:** set threshold = 10 and interval = 5 min. Mid-interval, run `pebble emu-steps <current+100>`. The worker logs a movement event with `recent >= 10` and a reset, and the countdown jumps back to 5. Then bump steps by only `+5`: no reset. Check that `emu-steps` actually produces `HealthEventMovementUpdate` events; if it doesn't, test this path on a real watch.
5. **Midnight rollover:** set the time to `23:59:00` (with the window set to 0→0, i.e. all day), and lower the step count after midnight. No spurious reset or negative `recent`, and the buffer is cleared.
6. **Active hours:** with the window at 9→18, `emu-set-time 18:30:00` gives "Paused until 09:00" and no reminder. `emu-set-time 08:59:00` → wait → at 09:00 a fresh interval starts, and the first reminder comes one interval later. Also check a wrapping window (22→6) and start = end (always on).
7. **Config round-trip:** change settings in Clay and close it. The app persists the values and the worker reschedules (logs). Toggling **off** stops the worker (`app_worker_is_running()` is false); toggling **on** relaunches it.

## Notes / risks
- `capabilities: ["health"]` is mandatory or health calls return no data.
- A reminder interrupts to the foreground by design (the worker can't vibrate in the background). It now auto-returns to the watchface after about 30 s.
- Reminders do **not** respect the watch's Quiet Time; active hours are the only suppression (user decision 4). Whether the firmware mutes app `vibes_*` during Quiet Time hasn't been checked; test it on hardware if it matters.
- The ACK handshake adds about 1.5 s of latency before relaunching a closed app. That's negligible for a reminder, and it prevents a double vibration. Confirm the timeout is long enough on real hardware; raise `REMIND_ACK_TIMEOUT_MS` if the logs show relaunches while the app is open.
- How often `HealthEventMovementUpdate` fires during walking isn't documented. If events arrive less often than every `ACTIVITY_WINDOW_SEC`, the "no older sample" fallback undercounts, and slow walks may not reset the timer. Tune `ACTIVITY_WINDOW_SEC` and the threshold against real step data.
- Live countdown state is kept in worker RAM and shared via messages. Only **settings** are persisted, so there are no per-event flash writes (good for battery and flash wear). If the worker is killed and relaunched mid-interval, it simply starts a fresh interval, which is acceptable.
- Only one background worker can be active system-wide on Pebble; installing this worker replaces any other app's running worker.
- Active-hours math uses local time via `time_start_of_today()`. On a DST-change day the window can be off by an hour once, which is acceptable.
