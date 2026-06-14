# include <pebble.h>
static Window *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;

static GFont s_time_font;
static GFont s_date_font;

static Layer *s_battery_layer;
static int s_battery_level;

static void update_time() {
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);
    static char s_time_buffer[8];
    strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);

    static char s_date_buffer[16];
    strftime(s_date_buffer, sizeof(s_date_buffer), "%a, %b %d", tick_time);

    text_layer_set_text(s_time_layer, s_time_buffer);
    text_layer_set_text(s_date_layer, s_date_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    update_time();
}

static void battery_callback(BatteryChargeState state) {
    // Record New battery level
    s_battery_level = state.charge_percent;

    // Redraw at next opportunity
    layer_mark_dirty(s_battery_layer);
}

static void battery_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    int bar_width = ((s_battery_level * (bounds.size.w - 4)) / 100);

    // Drawing border
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_round_rect(ctx, bounds, 4);

    // Choose color
    GColor bar_color;
    if (s_battery_level <= 20) {
        bar_color = PBL_IF_COLOR_ELSE(GColorRed, GColorWhite);
    } else if (s_battery_level <= 40) {
        bar_color = PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite);
    } else {
        bar_color = PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite);
    }

    // Draw bar
    graphics_context_set_fill_color(ctx, bar_color);
    graphics_fill_rect(ctx, GRect(2, 2, bar_width, bounds.size.h - 4), 1, GCornerNone);
}

static void main_window_load(Window *window) {
    // Get information about the window
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    // Load custom fonts
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JERSEY_56));
    s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JERSEY_24));

    // Centering the layout
    int date_height = 30;
    int block_height = 56 + date_height;
    int time_y = (bounds.size.h - block_height) / 2 - 15;
    // int date_y = time_y + 56;


    // Create time TextLayer
    s_time_layer = text_layer_create(GRect(0, time_y, bounds.size.w, 60));
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_font(s_time_layer, s_time_font);
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

    s_date_layer = text_layer_create(GRect(0, PBL_IF_ROUND_ELSE(110, 104), bounds.size.w, 30));
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, GColorWhite);
    text_layer_set_font(s_date_layer, s_date_font);
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);

    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
    layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

    // Battery meter Layer
    int bar_width = bounds.size.w / 2;
    int bar_x = (bounds.size.w - bar_width) / 2;
    int bar_y = PBL_IF_BW_ELSE(bounds.size.h / 8, bounds.size.h / 28);

    s_battery_layer = layer_create(GRect(bar_x, bar_y, bar_width, 8));
    layer_set_update_proc(s_battery_layer, battery_update_proc);
    layer_add_child(window_layer, s_battery_layer);

}

static void main_window_unload(Window *window) {
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_date_layer);

    layer_destroy(s_battery_layer);

    fonts_unload_custom_font(s_time_font);
    fonts_unload_custom_font(s_date_font);
}

static void init() {
  // Create Main window and assign to pointer
  s_main_window = window_create();

  // set background color
  window_set_background_color(s_main_window, GColorBlack);

  // set handlers to manage elements inside window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // show window on watch with animated=true
  window_stack_push(s_main_window, true);

  // Display time from the start
  update_time();

  // Subscribe to battery state changes
  battery_state_service_subscribe(battery_callback);
  battery_callback(battery_state_service_peek());

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

}

static void deinit() {
  tick_timer_service_unsubscribe();

  // Destory window
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
