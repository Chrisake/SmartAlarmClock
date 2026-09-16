#include "hal.h"

#include "board/board.h"
#include "presence/face_detector.h"
#include "presence/radar_sensor.h"

#include <SDL.h>

/*The simulator has no camera: F held down in the window is a face in front
 *of the clock, for face wake. Told only when F goes down or up.*/
static void face_key_poll(lv_timer_t * timer)
{
  static bool held;
  LV_UNUSED(timer);

  const Uint8 * keys    = SDL_GetKeyboardState(NULL);
  bool          pressed = keys && keys[SDL_SCANCODE_F];

  if(pressed == held) return;
  held = pressed;
  face_detector_sim_set_present(pressed);
}

/*The simulator has no presence board either. Keys stand in for someone in
 *front of the radar: R walks towards the clock, S stands still near it, A
 *moves about without coming closer. X unplugs the board, to try what the clock
 *does without one, and D turns the room's light out.*/
static void radar_key_poll(lv_timer_t * timer)
{
  /*A walk of about a metre a second, from across the room to arm's length.*/
  static const float FAR_CM  = 400.0f;
  static const float NEAR_CM = 60.0f;
  static const float STEP_CM = 10.0f;

  static bool  unplug_held;
  static bool  dark_held;
  static bool  dark;
  static float distance = 400.0f;

  const Uint8 * keys = SDL_GetKeyboardState(NULL);
  LV_UNUSED(timer);

  if(!keys) return;

  bool unplug = keys[SDL_SCANCODE_X];
  if(unplug && !unplug_held) radar_sensor_sim_set_connected(!radar_sensor_sim_is_connected());
  unplug_held = unplug;

  bool darken = keys[SDL_SCANCODE_D];
  if(darken && !dark_held) {
    dark = !dark;
    radar_sensor_sim_set_lux(dark ? 0.2f : 120.0f);
  }
  dark_held = darken;

  if(keys[SDL_SCANCODE_R]) {
    distance -= STEP_CM;
    if(distance < NEAR_CM) distance = NEAR_CM;
    radar_sensor_sim_set_target(true, distance, 60.0f);
    return;
  }

  /*Let go of R and the walk starts over from across the room.*/
  distance = FAR_CM;

  if(keys[SDL_SCANCODE_S])      radar_sensor_sim_set_target(true, 150.0f, 2.0f);
  else if(keys[SDL_SCANCODE_A]) radar_sensor_sim_set_target(true, 150.0f, 60.0f);
  else                          radar_sensor_sim_set_target(false, FAR_CM, 0.0f);
}

lv_display_t * sdl_hal_init(int32_t w, int32_t h)
{

  lv_group_set_default(lv_group_create());

  lv_display_t * disp = lv_sdl_window_create(w, h);

  /*Make the window stand in for the real panel: name it after the board, and
   *report the panel's pixel density so DPI-relative sizes (LV_DPX, default
   *paddings) come out the same as they will on the device.*/
  lv_sdl_window_set_title(disp, BOARD_WINDOW_TITLE);
  lv_display_set_dpi(disp, BOARD_DPI);

  /*Scales the window only, not the framebuffer, so the layout is unchanged.*/
  if(BOARD_SIM_ZOOM != 1.0f) {
    lv_sdl_window_set_zoom(disp, BOARD_SIM_ZOOM);
  }

  LV_LOG_USER("Simulating %s: %dx%d, %d bpp, %d DPI",
              BOARD_NAME,
              (int)lv_display_get_horizontal_resolution(disp),
              (int)lv_display_get_vertical_resolution(disp),
              (int)LV_COLOR_DEPTH,
              (int)lv_display_get_dpi(disp));

  lv_indev_t * mouse = lv_sdl_mouse_create();
  lv_indev_set_group(mouse, lv_group_get_default());
  lv_indev_set_display(mouse, disp);
  lv_display_set_default(disp);
  /*Declare the image file.*/
  LV_IMAGE_DECLARE(mouse_cursor_icon); 
  lv_obj_t * cursor_obj;
  /*Create an image object for the cursor */
  cursor_obj = lv_image_create(lv_screen_active()); 
  /*Set the image source*/
  lv_image_set_src(cursor_obj, &mouse_cursor_icon);           
  /*Connect the image  object to the driver*/
  lv_indev_set_cursor(mouse, cursor_obj);             

  lv_indev_t * mousewheel = lv_sdl_mousewheel_create();
  lv_indev_set_display(mousewheel, disp);
  lv_indev_set_group(mousewheel, lv_group_get_default());

  lv_indev_t * kb = lv_sdl_keyboard_create();
  lv_indev_set_display(kb, disp);
  lv_indev_set_group(kb, lv_group_get_default());

  lv_timer_create(face_key_poll, 50, NULL);
  /*The radar's own frames come ten times a second; so does the key that feeds them.*/
  lv_timer_create(radar_key_poll, 100, NULL);

  return disp;
}
