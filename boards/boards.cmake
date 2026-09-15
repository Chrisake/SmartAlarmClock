# ---------------------------------------------------------------------------
# Board profiles
#
# This project is the SDL/PC simulator for the Smart Alarm Clock firmware. The
# profile selected here makes the simulator window match the panel of the real
# hardware: resolution, colour format and pixel density. Keeping these in sync
# means a layout that looks right in the simulator also looks right on the
# device.
#
# Select a board at configure time:
#
#   cmake -B build -DBOARD=ESP32_P4_WIFI6_TOUCH_LCD_7B
#
# To add a board, copy one of the elseif() blocks below and add its id to
# BOARD_PROFILES.
#
# DPI is the panel's real pixel density, used by LVGL to scale default widget
# sizes and paddings:  dpi = sqrt(hor_res^2 + ver_res^2) / diagonal_inches
# ---------------------------------------------------------------------------

set(BOARD_PROFILES
    ESP32_P4_WIFI6_TOUCH_LCD_7B
    ESP32_P4_NANO_7INCH
    CUSTOM)

set(BOARD "ESP32_P4_WIFI6_TOUCH_LCD_7B"
    CACHE STRING "Board whose display the simulator mimics")
set_property(CACHE BOARD PROPERTY STRINGS ${BOARD_PROFILES})

# Only read when BOARD=CUSTOM.
set(BOARD_CUSTOM_NAME        "Custom" CACHE STRING "CUSTOM board: name shown in the window title")
set(BOARD_CUSTOM_HOR_RES     1024     CACHE STRING "CUSTOM board: horizontal resolution in px")
set(BOARD_CUSTOM_VER_RES     600      CACHE STRING "CUSTOM board: vertical resolution in px")
set(BOARD_CUSTOM_COLOR_DEPTH 16       CACHE STRING "CUSTOM board: LVGL colour depth (16 = RGB565, 32 = XRGB8888)")
set(BOARD_CUSTOM_DPI         170      CACHE STRING "CUSTOM board: pixel density in DPI")

# Scale factor applied to the simulator window only. The framebuffer stays at
# the board resolution, so this never changes the rendered layout -- it just
# makes a large panel fit on a smaller desktop (e.g. 0.75), or a small one
# easier to inspect (e.g. 2.0).
set(BOARD_SIM_ZOOM "1.0" CACHE STRING "Simulator window zoom factor (does not affect the framebuffer)")

if(BOARD STREQUAL "ESP32_P4_WIFI6_TOUCH_LCD_7B")
    # Waveshare ESP32-P4-WIFI6-Touch-LCD-7B
    #   7" 1024x600 IPS, landscape, MIPI-DSI (2 lane), EK79007 panel driver
    #   GT911 5-point capacitive touch
    #   ESP32-P4NRW32 (32 MB PSRAM) + ESP32-C6 for Wi-Fi 6 / BLE 5
    #   BSP default pixel format is RGB565
    #   https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B
    set(_board_name        "ESP32-P4-WIFI6-Touch-LCD-7B")
    set(_board_hor_res     1024)
    set(_board_ver_res     600)
    set(_board_color_depth 16)      # RGB565
    set(_board_dpi         170)     # sqrt(1024^2 + 600^2) / 7.0 = 169.5

elseif(BOARD STREQUAL "ESP32_P4_NANO_7INCH")
    # Waveshare ESP32-P4-NANO carrier with the 7" 1024x600 DSI LCD accessory.
    set(_board_name        "ESP32-P4-NANO-7inch-DSI")
    set(_board_hor_res     1024)
    set(_board_ver_res     600)
    set(_board_color_depth 16)      # RGB565
    set(_board_dpi         170)     # sqrt(1024^2 + 600^2) / 7.0 = 169.5

elseif(BOARD STREQUAL "CUSTOM")
    set(_board_name        "${BOARD_CUSTOM_NAME}")
    set(_board_hor_res     ${BOARD_CUSTOM_HOR_RES})
    set(_board_ver_res     ${BOARD_CUSTOM_VER_RES})
    set(_board_color_depth ${BOARD_CUSTOM_COLOR_DEPTH})
    set(_board_dpi         ${BOARD_CUSTOM_DPI})

else()
    message(FATAL_ERROR
        "Unknown BOARD '${BOARD}'. Supported values: ${BOARD_PROFILES}")
endif()

if(NOT _board_color_depth EQUAL 16 AND NOT _board_color_depth EQUAL 32)
    message(FATAL_ERROR
        "Board '${BOARD}' requests colour depth ${_board_color_depth}; "
        "this project supports 16 (RGB565) or 32 (XRGB8888).")
endif()

# A board name with spaces would not survive being passed as a -D string
# definition through every generator, so reject it early with a clear message.
if(_board_name MATCHES "[ \t\"]")
    message(FATAL_ERROR
        "Board name '${_board_name}' must not contain spaces or quotes.")
endif()

# Exposed to C via src/board/board.h. LV_COLOR_DEPTH and LV_DPI_DEF override
# the defaults in lv_conf.h, which guards both with #ifndef for this purpose.
add_compile_definitions(
    BOARD_NAME="${_board_name}"
    BOARD_HOR_RES=${_board_hor_res}
    BOARD_VER_RES=${_board_ver_res}
    BOARD_COLOR_DEPTH=${_board_color_depth}
    BOARD_DPI=${_board_dpi}
    BOARD_SIM_ZOOM=${BOARD_SIM_ZOOM}f
    LV_COLOR_DEPTH=${_board_color_depth}
    LV_DPI_DEF=${_board_dpi})

message(STATUS "Board: ${_board_name} (BOARD=${BOARD})")
message(STATUS "  Resolution:   ${_board_hor_res}x${_board_ver_res}")
message(STATUS "  Colour depth: ${_board_color_depth} bpp")
message(STATUS "  DPI:          ${_board_dpi}")
message(STATUS "  Window zoom:  ${BOARD_SIM_ZOOM}")
