# Standup Reminder — Pebble Watchapp + Background Worker

## Context

The user wants a **stand-up reminder app** for Pebble that:
1. Vibrates periodically to remind them to stand up, with a **configurable interval**.
2. **Resets the countdown** whenever it detects real movement, so the reminder only fires after a genuine sedentary stretch. Movement is detected as a configurable number of steps within a short rolling window (the "activity threshold"), which doubles as a noise filter against false-positive step jitter.

Decisions confirmed with the user:
- **1B** — implement as a **watchapp + background worker** (keeps the user's normal watchface; the reminder logic runs in the background).
- **2B** — configuration via a **Clay phone settings page** (Pebble mobile app), not on-watch buttons.
- **3 (event-driven continuous reset)** — the worker subscribes to health movement events. On each movement update it sums steps over a short rolling window; if that exceeds the configurable activity threshold, it treats the user as active and **resets the countdown**. The reminder fires only if a full interval elapses with no qualifying activity. This is a true "sedentary timer" (like Apple Watch / Fitbit stand reminders) and is the user's chosen design.

### Why event-driven (vs. polling or evaluate-at-expiry)
The Pebble pedometer runs continuously system-wide regardless of our app, so reading steps powers up no extra hardware. The only background battery cost is CPU wakeups. Subscribing to `health_service_events_subscribe(HealthEventMovementUpdate)` means the worker wakes **only when step data actually changes**: near-silent while the user is sedentary (the case where we must *not* reset), active only while they move (when we *do* reset). This gives the correct recency semantics at close to the battery cost of a once-per-interval timer, far cheaper than a blind every-minute poll. To protect battery/flash further, live countdown state lives in RAM and is shared with the app via worker messages (no per-event `persist_write`).

### Key SDK facts (verified against installed SDK 4.9.169)
Worker (`.../basalt/include/pebble_worker.h`) can: subscribe to health events (`health_service_events_subscribe:1007`, `HealthEventMovementUpdate:984`), sum steps over a window (`health_service_sum:710`), read live value (`health_service_peek_current_value:725`), run one-shot timers (`app_timer_register:1897`, `app_timer_reschedule:1904`), use `persist_*`, and message the app (`app_worker_send_message`). The worker **cannot vibrate or draw UI** (`vibes_*` / window APIs are absent) — its only way to alert is `worker_launch_app()` (`pebble_worker.h:1810`). App side has `app_worker_launch/kill/is_running` (`pebble.h:2879,2884,2874`) and `launch_reason()`. So: the worker decides when a reminder is due, then hands off to the foreground app, which vibrates. UX note: a reminder briefly brings the Standup Reminder app to the foreground; pressing Back returns to the watchface.

## Approach

Create a **new sibling project** `standup-reminder/` next to `watchface-c/`. It reuses the existing build tooling unchanged: `build_and_install.sh` auto-selects the most-recently-modified project containing a `wscript`, and the stock `wscript` already builds a worker whenever a `worker_src/` directory exists (`watchface-c/wscript:29,39-44`) and bundles PebbleKit JS from `src/pkjs/` (`watchface-c/wscript:50-54`). No font/image resources are needed — use system fonts (`fonts_get_system_font(...)`).

### Files to create (mirrors `watchface-c/` layout)

```
standup-reminder/
  wscript                       # copy of watchface-c/wscript verbatim
  package.json                  # new UUID, capabilities, messageKeys, clay dep
  src/c/standup_app.c           # foreground app: UI, vibration, Clay/AppMessage, launches worker
  worker_src/c/standup_worker.c # background worker: event-driven timing + step evaluation
  src/pkjs/index.js             # wires up Clay
  src/pkjs/config.js            # Clay config page definition
  src/common/keys.h             # shared persist keys + AppMessage/worker-message keys + defaults
```

### `package.json` (`standup-reminder/package.json`)
Base it on `watchface-c/package.json` with these changes:
- New `uuid` — generate at implementation time with `uuidgen` (lowercased).
- `displayName`: "Standup Reminder".
- `watchapp.watchface`: `false` (it is an app, not a face).
- Add `"capabilities": ["health"]` — **required** for step access in both app and worker.
- `messageKeys`: `["INTERVAL_MINUTES", "ACTIVITY_THRESHOLD", "ENABLED"]` (replaces `"dummy"`).
- Add `"dependencies": { "pebble-clay": "^1.0.4" }` (Clay is pulled in via `enableMultiJS`).
- `targetPlatforms`: drop `aplite` (no health sensor); keep `basalt, chalk, diorite, emery, flint, gabbro`. `emery` is the emulator default.
- Remove the `resources.media` font entries (none needed).

### Shared keys & state (`src/common/keys.h`)
- **Settings** (persisted by app from Clay, read by worker): `PKEY_INTERVAL_MIN` (default 30), `PKEY_ACTIVITY_THRESHOLD` (default 50, steps within the rolling window), `PKEY_ENABLED` (default true).
- **Constant:** `ACTIVITY_WINDOW_SEC` = 120 (rolling window for movement detection; a tunable; using a ~2 min window avoids a single-minute test missing a steady slow walk).
- **Worker-message types:** `WMSG_SETTINGS_CHANGED` (app→worker), `WMSG_STATE` (worker→app, carries remaining seconds in `AppWorkerMessage.data0`), `WMSG_REMIND_NOW` (worker→app, for the already-open case), `WMSG_REQUEST_STATE` (app→worker).
- **AppMessage key enum** matching the `messageKeys` names.
- Helper inlines `settings_load_*` reading persist with the defaults above.
- Live countdown state is **RAM-only in the worker** (not persisted) and shared on demand via worker messages — avoids per-event flash writes.

### Background worker (`worker_src/c/standup_worker.c`)
- `worker_main()` → init → `worker_event_loop()`.
- **Init:** load settings. RAM state: `time_t deadline`, `AppTimer *fire_timer`. Start an interval: `deadline = now + interval_min*60`; `fire_timer = app_timer_register(interval_min*60*1000, on_fire, NULL)`. Subscribe `health_service_events_subscribe(on_health_event, NULL)` and `app_worker_message_subscribe(on_app_msg)`.
- **`on_health_event(HealthEventMovementUpdate / SignificantUpdate)`:** if `!ENABLED` return. Compute `recent = health_service_sum(HealthMetricStepCount, now - ACTIVITY_WINDOW_SEC, now)`. If `recent >= activity_threshold` → user is active → **reset**: `deadline = now + interval`; `app_timer_reschedule(fire_timer, interval_ms)`. (Rescheduling is cheap; nothing is persisted here.) `health_service_sum` over a recent window handles midnight rollover internally, so no manual baseline math.
- **`on_fire` (timer expired = a full interval with no qualifying activity):** if `ENABLED` → if `app_worker_is_running`-equivalent not knowable from worker, just both signal and launch: `app_worker_send_message(WMSG_REMIND_NOW, NULL)` (handles app-already-open) **and** `worker_launch_app()` (handles app-closed; the launched app sees `launch_reason()==APP_LAUNCH_WORKER`). Then start the next interval (reset `deadline`, `app_timer_reschedule`).
- **`on_app_msg`:** `WMSG_SETTINGS_CHANGED` → reload settings, restart interval from `now` (kill the feature path handled app-side). `WMSG_REQUEST_STATE` → reply `app_worker_send_message(WMSG_STATE, {data0 = clamp(deadline - now)})` so the app can show a live countdown.

### Foreground app (`src/c/standup_app.c`)
- `main()` → `init()` → `app_event_loop()` → `deinit()` (same shape as `watchface-c/src/c/watchface-c.c:148-152`).
- **Reminder delivery:** treat it as a reminder when `launch_reason()==APP_LAUNCH_WORKER` **or** a `WMSG_REMIND_NOW` worker message arrives → `vibes_double_pulse()` (or a custom `VibePattern`) and show a "Time to stand up!" window.
- **Ensure worker runs:** on normal launch, if `ENABLED` and `!app_worker_is_running()` → `app_worker_launch()`. Subscribe `app_worker_message_subscribe` to receive `WMSG_REMIND_NOW` / `WMSG_STATE`.
- **Clay/AppMessage:** `app_message_open(...)`; inbox-received handler reads `INTERVAL_MINUTES / ACTIVITY_THRESHOLD / ENABLED` (`dict_find`), persists them, then notifies the worker: if turned **off** → `app_worker_kill()`; if **on** → ensure running (`app_worker_launch()`), then `app_worker_send_message(WMSG_SETTINGS_CHANGED, NULL)`.
- **Status UI:** a system-font `TextLayer` window showing time-until-next-reminder (request via `WMSG_REQUEST_STATE`, display the `WMSG_STATE` reply), recent-window steps vs. threshold (`health_service_sum` over `ACTIVITY_WINDOW_SEC`), and enabled/worker state. Refresh with `tick_timer_service_subscribe(MINUTE_UNIT, ...)` (pattern at `watchface-c/src/c/watchface-c.c:25-27,134`) — this tick is foreground-only (no background cost).

### Clay configuration (`src/pkjs/`)
- `config.js`: export a Clay config array — a **slider** for interval minutes (5–120, step 5), a **slider** for the activity/step threshold (10–200, default 50), and a **toggle** for enabled. Each `messageKey` must equal the `package.json` `messageKeys`.
- `index.js`: `var Clay = require('pebble-clay'); var cfg = require('./config'); var clay = new Clay(cfg);` — Clay auto-handles `showConfiguration`/`webviewclosed` and sends values to the watch via AppMessage. Keep a `Pebble.addEventListener('ready', ...)` log (matches existing `watchface/src/pkjs/index.js`).

## Verification

Build/install with the existing flow (the new dir will be the most-recently-modified project, so it is auto-selected):
```
cd /home/jtliang/Projects/pebble-test/standup-reminder
direnv exec . pebble build              # confirm app + worker (pebble-worker.elf) + pkjs all build
direnv exec . pebble install --emulator emery
```
(or run the root `build_and_install.sh` / the Zed "Build and Install" task.)

Then verify end-to-end:
1. **Worker launches & live state:** open the app → status screen shows a countdown that decreases each minute; `pebble logs` shows the worker started and replied to `WMSG_REQUEST_STATE`.
2. **Reminder fires (fast loop):** via Clay set interval = 1 min, activity threshold high (e.g. 200). Leave the watchface; with no movement, after ~1 min the app should pop to the foreground and **vibrate**. Confirm in `pebble logs`.
3. **Event-driven reset:** set threshold low (e.g. 10). Inject steps in the emulator health panel (or test on a real watch) during the interval → confirm the countdown jumps back to full (worker logs a reset on the movement event) and **no** vibration occurs. Note: emulator step injection is limited — the no-movement path (reminder fires) is fully testable in the emulator; the reset path may need step injection or a physical device.
4. **Config round-trip:** change interval/threshold/enabled in the Clay page, close it; confirm the app persists values and the worker restarts with the new interval (logs). Toggling **off** stops the worker (`app_worker_is_running()` false); toggling **on** relaunches it.

## Notes / risks
- `capabilities: ["health"]` is mandatory or health calls return no data.
- A reminder interrupts to the foreground by design (the worker can't vibrate in the background) — documented above.
- Live countdown state is kept in worker RAM and shared via messages; only **settings** are persisted, so there are no per-event flash writes (battery + flash-wear safe). If the worker is killed/relaunched mid-interval it simply starts a fresh interval — acceptable.
- Only one background worker can be active system-wide on Pebble; installing this worker replaces any other app's running worker.
- `ACTIVITY_WINDOW_SEC` (2 min) and the default threshold (50) are tunables; validate against real step data and adjust if jitter resets too eagerly or real walks fail to reset.
