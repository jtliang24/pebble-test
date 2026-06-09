# include <pebble.h>
static Window *s_main_window;

static void main_window_load(Window *window) {

}

static void main_window_unload(Window *window) {

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

}

static void deinit() {
  // Destory window
  window_destroy(s_main_window);

}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
