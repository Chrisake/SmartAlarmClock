#include "hal.h"

#include "board/board.h"


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

  return disp;
}
