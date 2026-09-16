# VSCode Simulator project for LVGL

[LVGL](https://github.com/lvgl/lvgl) is written mainly for microcontrollers and embedded systems, however you can run the library **on your PC** as well without any embedded hardware. The code written on PC can be simply copied when your are using an embedded system.

This project is pre-configured for VSCode and should work work on Windows, Linux and MacOs as well. FreeRTOS is also included and can be optionally enabled to better simulate embedded system's behavior. 

## Get started

### Install SDL and the build tools

- **Windows (vcpkg):** `vcpkg install sdl2`  (`vcpkg` can be installed from [https://github.com/microsoft/vcpkg](https://github.com/microsoft/vcpkg)) Also install either MinGW or another compiler and `cmake`.
- **macOS (Homebrew):** `brew install sdl2 cmake make`  
- **Linux:**  
  - **Debian/Ubuntu:** `sudo apt install build-essential cmake libsdl2-dev`  
  - **Arch:** `sudo pacman -S base-devel cmake sdl2`  
  - **Fedora:** `sudo dnf install @development-tools cmake SDL2-devel`  
- **Manual Installation of SDL:** Download from [SDL’s website](https://github.com/libsdl-org/SDL/releases) and place headers/libraries in your project.
- **Verify Installation:** `sdl2-config --version`, `cmake --version`, `gcc --version`, `g++ --version` (should return the installed version).  

### Get the PC project

Clone the PC project and the related sub modules:

```bash
git clone --recursive https://github.com/lvgl/lv_port_pc_vscode
```

## Target board

This simulator is configured to stand in for the **Waveshare
[ESP32-P4-WIFI6-Touch-LCD-7B](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B)**:
a 7" 1024x600 IPS panel (EK79007 over 2-lane MIPI-DSI) with GT911 5-point
capacitive touch, driven by an ESP32-P4 with an ESP32-C6 for Wi-Fi 6 / BLE.

The board profile drives the simulator window resolution, the LVGL colour
depth and the DPI used to scale default widget sizes and paddings, so a layout
that looks right here looks right on the device:

| Property | Value |
| --- | --- |
| Resolution | 1024 x 600, landscape |
| Colour depth | 16 bpp (RGB565), matching the BSP default |
| DPI | 170 (`sqrt(1024^2 + 600^2) / 7.0"`) |

### Selecting a board

```bash
cmake -B build -DBOARD=ESP32_P4_WIFI6_TOUCH_LCD_7B   # default
cmake -B build -DBOARD=ESP32_P4_NANO_7INCH           # ESP32-P4-NANO + 7" DSI LCD
```

The profiles live in [`boards/boards.cmake`](boards/boards.cmake); add a board
by copying one of the `elseif()` blocks and listing its id in `BOARD_PROFILES`.
For a one-off panel, use the `CUSTOM` profile instead of editing the file:

```bash
cmake -B build -DBOARD=CUSTOM \
  -DBOARD_CUSTOM_NAME=MyPanel \
  -DBOARD_CUSTOM_HOR_RES=800 -DBOARD_CUSTOM_VER_RES=480 \
  -DBOARD_CUSTOM_COLOR_DEPTH=16 -DBOARD_CUSTOM_DPI=200
```

CMake prints the active profile when it configures, and the running simulator
logs it and shows it in the window title.

### Fitting the window on your desktop

1024x600 is larger than some laptop screens once window decorations are added.
`BOARD_SIM_ZOOM` scales the window without touching the framebuffer, so the
rendered layout is unchanged:

```bash
cmake -B build -DBOARD_SIM_ZOOM=0.75
```

### Using the profile from your own code

The selected profile is available to C through `src/board/board.h` as
`BOARD_NAME`, `BOARD_HOR_RES`, `BOARD_VER_RES`, `BOARD_COLOR_DEPTH`,
`BOARD_DPI` and `BOARD_WINDOW_TITLE`. Prefer these over hard-coded pixel
values so screens follow the board profile:

```c
#include "board/board.h"

sdl_hal_init(BOARD_HOR_RES, BOARD_VER_RES);
```

`lv_conf.h` keeps `LV_COLOR_DEPTH` and `LV_DPI_DEF` as `#ifndef`-guarded
fallbacks matching the default board; the board profile overrides both at
configure time, so change the profile rather than those two lines.

## Usage

### Visual Studio Code

1. Be sure you have installed [SDL and the build tools](#install-sdl-and-the-build-tools)
2. Open the project by double clicking on `simulator.code-workspace` or opening it with `File/Open Workspace from File`
3. Install the recommended plugins
4. Click the Run and Debug page on the left, and select `Debug LVGL demo with gdb` from the drop-down on the top. Like this:
![image](https://github.com/lvgl/lv_port_pc_vscode/assets/7599318/f527b235-5718-4949-b5f0-bd807b3a64ba)
5. Click the Play button or hit F5 to start debugging.

#### ArchLinux User

VSCode does not officially provide an installation package under Arch, you need to use the AUR manager `paru` to install it.
The command is as follows:

```bash
paru -S visual-studio-code-bin
```

#### macOS

Apple's default clang does not support the `-fsanitize=leak` flag.

to build using the latest version of clang from homebrew, do the following:

1. `brew install llvm`

2. cmd+shift+p and run `Cmake: select a kit`, then `[Scan for kits]`

3. then cmd+shift+p and run `Cmake: select a kit`, select the version of clang you just installed from homebrew (it should say `Using compilers C=/opt/homebrew/opt/llvm/bin/clang ...`)

4. reconfigure by running cmd+shift+p `Cmake: Configure`

5. build using [step 4 above](#visual-studio-code)

### FreeRTOS configuration
To correctly configure the project, the RTOS (Real-Time Operating System) requires a significant amount of heap memory, especially when debugging an SDL (Simple DirectMedia Layer) window application. In this project, the heap memory has been experimentally set to **512 MB**.

```c
#define configTOTAL_HEAP_SIZE ( ( size_t ) ( 512 * 1024 * 1024 ) )  // 512 MB Heap
```
This configuration ensures that the SDL window is displayed in a timely manner. If this value is reduced, it may cause significant delays in the SDL window's appearance. If the allocated heap memory is too small, the window may fail to appear altogether.
Therefore, it is crucial to allocate sufficient heap memory to ensure smooth execution and debugging experience.

### Enable FreeRTOS 
To enable the rtos part of this project select in lv_conf.h `#define LV_USE_OS   LV_OS_NONE` to `#define LV_USE_OS  LV_OS_FREERTOS`
Additionaly you have to enable the compilation of all FreeRTOS Files by turning on the `option(USE_FREERTOS "Enable FreeRTOS" OFF)` in the CMakeLists.txt file or
by enabling the same flag from the command line when bootstrapping `cmake`:

```bash
cmake -B build -DUSE_FREERTOS=ON
```

### CMake

This project uses CMake under the hood which can be used without Visula Studio Code too. Just type these in a Terminal when you are in the project's root folder:

```bash
mkdir build
cd build
cmake ..
make -j
```

## User interface

The UI lives under `src/ui/`, one directory per page so a page can grow into
several files without moving anything:

```
src/ui/
  ui.h / ui.c                 shell: navigation rail, page registry, ui_navigate()
  ui_page.h                   the contract every page implements
  ui_theme.h / ui_theme.c     colour, type and spacing tokens + widget factories
  ui_weather_icon.h / .c      weather condition icons drawn from plain objects
  ui_moon_icon.h / .c         the moon's phase, drawn the same way
  ui_format.h / .c            every time and date on screen, as the settings ask
  ui_picker.h / .c            iOS-style wheel picker (effects, date, time)
  ui_status.h / .c            loading and error covers, refresh control, notices
  ui_alarm_screen.h / .c      the screen shown while an alarm rings
  pages/
    clock/page_clock.*        ambient clock face + time, date, conditions
                              and MinuteCast precipitation graph, colour-coded
                              calendar events, air summary
    alarms/page_alarms.*      alarm list and editor, iOS-style
    weather/page_weather.*    current conditions, next 20 hours in 2-hour
                              steps, 7-day forecast with low/high range bars
    air_quality/page_air_quality.*
                              metric tiles, history chart with 1h/24h/7d,
                              analysis, forecast strip
    radio/page_radio.*        now playing, transport, volume, saved stations
                              with edit mode (add dialog in radio_search.c)
    smart_home/page_smart_home.*
                              Devices page: square device tiles, room filter,
                              scenes (tiles in smart_home_tile.c, controls
                              panel in smart_home_modal.c)
    settings/page_settings.*  Settings page: Wi-Fi, MQTT, Date & time and
                              Device tabs (one file per tab, settings_*.c)
  ui_devices_feed.h / .c      devices page <-> device hub; simulated broker
  ui_radio_feed.h / .c        radio page <-> Radio Browser, the SD card and
                              the stream player
  ui_settings_feed.h / .c     loads, stores and applies settings; simulated
                              Wi-Fi radio and broker connection
  ui_alarm_feed.h / .c        stores the alarms and rings them: screen, tone or
                              station, volume ramp, snooze, tone preview
  ui_weather_feed.h / .c      Open-Meteo forecasts -> weather page, clock page
                              and the air quality forecast
  ui_air_feed.h / .c          sensors and outdoor air -> air quality page and
                              clock page; readings to MQTT
  ui_presence_feed.h / .c     face wake while idle -> wakes the screen
src/presence/                 face_wake: the camera thread counting frames with
                              a face; face_detector: esp_video + ESP-DL YOLO on
                              the board, the F key in the simulator
src/devices/                  device model, JSON configuration, MQTT state hub
                              (plain C, no LVGL)
src/settings/                 device settings model, JSON load/save, and
                              clock_time: local time, zones, DST (plain C)
src/os/                       mutex, condition variable, thread, sleep:
                              Win32 or POSIX (plain C, no LVGL)
src/net/                      HTTP streams (WinHTTP / esp_http_client), GET,
                              the background download worker, and the MQTT
                              client seam
src/radio/                    Radio Browser station records, SD card layout,
                              favicon conversion (plain C, no LVGL)
src/audio/                    radio_player: stream, decoder and output;
                              tone_player: the synthesised alarm tones; and
                              audio_sink: SDL2 in the simulator, the ES8311
                              codec on the board (plain C, no LVGL)
src/sensors/                  SEN69C and SHT45 readings (I2C on the board, made
                              up in the simulator), the sampler thread, hour,
                              day and week history, US AQI (plain C, no LVGL)
src/weather/                  Open-Meteo forecast and air quality requests and
                              answers with the moon's phase, IP location
                              (plain C, no LVGL)
data/devices.json             example device configuration, read by the simulator
third_party/cjson/            cJSON 1.7.18 (MIT), same parser ESP-IDF ships
third_party/stb/              stb_image 2.30 (public domain), reads favicons
third_party/minimp3/          minimp3 (CC0), MP3 decoder
third_party/helix-aac/        Helix AAC and HE-AAC decoder (RPSL); SOURCE.md
                              lists the changes made to it
```

CMake globs `src/ui/**/*.c` with `CONFIGURE_DEPENDS`, so adding a file needs no
build edit.

### The page contract

Every page returns a `ui_page_t` (see `ui_page.h`) and is otherwise opaque:

```c
typedef struct {
    const char * title;                      /* nav rail label  */
    const char * icon;                       /* LV_SYMBOL_*     */
    lv_obj_t * (*create)(lv_obj_t * parent); /* build the tree  */
    void (*on_show)(void);                   /* start polling   */
    void (*on_hide)(void);                   /* stop polling    */
} ui_page_t;
```

All pages are built once during `ui_init()` and then shown or hidden, so
navigation is instant. Anything periodic belongs in `on_show`/`on_hide` so
background pages cost nothing.

To add a page: create `src/ui/pages/<name>/`, fill in a `ui_page_t`, add an id
to `ui_page_id_t` and its descriptor to the table at the top of `ui_init()`.

### The clock page's two states

The home page is what the panel shows when nobody is using it, so it has an
AMBIENT state -- just the time, the date and the next alarm, centred on an
otherwise empty screen with the navigation rail faded out -- and an ACTIVE
state that slides the clock up to the top-left and fades the rest in.

The shell drops back to ambient after `UI_IDLE_TIMEOUT_MS` (`ui.c`, currently
4 minutes) of no input, measured with `lv_display_get_inactive_time()` so it
covers every page. A press anywhere on the clock page wakes it again. Drive it
by hand with `page_clock_set_ambient()`.

Two details worth knowing if you change that layout:

- The rail keeps its slot in the layout and is only faded, so nothing reflows
  when it comes and goes. The ambient clock is therefore centred on the
  *display* rather than on the page, to compensate for the rail's width.
- The clock block is a free-positioned sibling of the grid, not a cell in it.
  An invisible `clock_anchor` cell reserves its active slot, and the block
  animates between that position and the ambient centre.

### The clock feed

`ui_clock_feed` keeps the home page's clock and alarm chip current. It runs a
one-second timer and:

- pushes the time every second and the date on each minute boundary;
- recomputes which alarm rings next on each minute boundary, and immediately
  whenever `ui_alarm_feed` reports that the alarms, or a snooze, changed.

The next alarm is whichever enabled alarm has the fewest minutes until it
fires, walking a week ahead for repeating ones. `when` comes out as "Today",
"Tomorrow", or "on <weekday>". A snoozed alarm takes the chip instead, as
"Snoozed" with the time it rings again.

Two details that are easy to get wrong: `tm_wday` counts from Sunday while
`page_alarm_days_t` counts from Monday, so the index is rotated at both ends of
the calculation; and `localtime()` returns a shared buffer with a different
reentrant spelling per toolchain, so `local_now()` wraps `localtime_s` and
`localtime_r`.

Keeping the alarms and ringing them is `ui_alarm_feed`'s job; see Alarms.

### Dividers and the precipitation band

The page is split into four regions by hairlines rather than by boxes: one
full-height line down the middle separating clock/calendar from weather/air,
and one across each side. They all fade to nothing at both ends
(`divider_create()` in `page_clock.c`), which is why none of them reads as the
edge of a card, and why the two horizontal ones stop short of the vertical one
instead of crossing it.

That needs three gradient stops -- transparent, colour, transparent -- so
`LV_GRADIENT_MAX_STOPS` is raised to 3 in `lv_conf.h`. Two descriptors exist,
one per axis, because a gradient's direction is part of the descriptor.

The weather column leads with the current conditions -- temperature with the
icon, then RealFeel and humidity as a pair of readouts -- and follows with the
MinuteCast forecast: a headline ("Rain starts in 24 min") over an area chart of
`PAGE_CLOCK_RAIN_MINUTES` (120) minutes, drawn against a Light/Heavy scale.

Intensity sets the curve's height and nothing else: the chart is drawn in a
single blue (`UI_COLOR_RAIN`), because height already carries the intensity and
the Light/Heavy scale labels it. Dry minutes rest on the axis at zero.

Under the plot is a ruler with a tick every quarter hour -- longest on the
hour, medium on the half hour, shortest on the quarters -- and the chart's own
division lines rise from each of those ticks, kept faint so they never compete
with the data. `RAIN_CHART_HEADROOM` adds padding above the plot so a
full-scale reading is not sliced off by the top edge.

The two sides of the page split their height differently, so each gets its own
grid rather than sharing rows: the left is three tenths time and date to seven
tenths events, the right three fifths forecast to two fifths indoor sensors.

```c
page_clock_set_rain(levels, count, "Rain starting in 24 min");
```

The plot is a plain `lv_chart` with nothing hooked onto it: the widget owns the
data, the scaling, the grid and the drawing. Entries past `count` are treated
as dry, so a short forecast just flattens the tail.

Note that `lv_chart`'s LINE type draws only the line and optional point
markers -- it has no area fill, so the curve is not shaded.

`lv_chart` renders no X axis of its own; the convention is to stack a
horizontal `lv_scale` beneath it with a matching tick count, which is what the
page does. Nothing on this page draws itself any more.

`lv_scale` offers exactly two tick lengths, major and minor, so a tick is major
every second one: the half hours stand taller than the quarters, and the hours
are singled out by being the only ticks that carry a label. The label array
passes empty strings for the half hours.

Two alignment details, both easy to trip over:

- The scale sits in its own row behind a spacer the width of the Light/Heavy
  gutter, mirroring the chart's row, so the two line up structurally rather
  than by matching padding values.
- Both rows are inset on the right by `RAIN_LABEL_MARGIN`. `lv_scale` centres
  a label on its tick, so the last one would otherwise overhang the end of the
  axis and be clipped. Insetting both equally keeps them aligned.

### Alarms

`page_alarms` holds the list and the editor in one page, swapping between them
in the same cell. An alarm carries a label, a time, the days it repeats on, an
enable flag and a sound; the editor sets them with time wheels, a row of day
toggles, a dropdown of tones and stations with a play button beside it, and an
on-screen keyboard for the label. Every alarm offers Snooze when it rings, so
there is no snooze switch per alarm.

`PAGE_ALARMS_MAX` (32) is deliberately invisible. Nothing announces it -- on
reaching the limit the add button simply goes disabled and greys out, the way
iOS stops you rather than explaining itself. Both `add_clicked()` and
`save_clicked()` re-check it, so the cap holds even if the button state is ever
missed.

The list is kept sorted by time of day, earliest first, so it reorders itself
when an alarm's time is edited. The array index is how a card identifies its
alarm and never appears on screen; anything holding one has to be finished with
it before the next sort.

The page only edits the list. `ui_alarm_feed` loads it at start with
`page_alarms_set_alarms()` and stores it from the callback registered with
`page_alarms_set_changed_cb()`, which fires on every add, edit, delete and
toggle -- in the simulator to `data/alarms.json`, with the days, tone and
station spelled out so the file can be edited by hand.

The SOUND menu lists the five tones, then the saved radio stations, which the
radio feed hands over with `page_alarms_set_stations()` whenever its list
changes. An alarm set to a station keeps its tone to fall back on; one whose
station has since been removed opens in the editor as that tone.

The play button beside the menu plays the tone picked; a station has none. The
page asks through `page_alarms_set_preview_cb()`, and `ui_alarm_feed` plays the
tone at the alarm volume -- the radio makes way, as it does for an alarm. It
stops after eight seconds, or when the button is tapped again, another sound is
picked, the editor closes, the page is left or an alarm rings.

Working out *which* alarm rings next needs a clock and a calendar, and the page
has neither. `ui_clock_feed` does it -- see below -- and pushes the answer to
the clock page with `page_clock_set_next_alarm()`.

Tapping the chip on the clock page opens the alarms page, but only once the
page is awake: while ambient the chip is part of the idle face, so touching it
wakes the page like touching anywhere else rather than navigating.

#### Ringing

`ui_alarm_feed` looks at the clock four times a second and rings an enabled
alarm when its minute comes round on one of its days. One with no days rings at
the next occurrence of its time and then switches itself off.

Ringing puts `ui_alarm_screen` up on the top layer, over the navigation rail
and everything else: a swinging bell, the time, the alarm's name, its repeat
and sound, and large buttons: Snooze and Stop, and between them Stop & Listen
when the sound is a station.
The panel is woken out of the ambient face, and activity is reported while it
rings, so the idle timeout cannot drop back to the ambient face underneath.

The sound is the alarm's station, played through the radio feed, or its tone.
The tones are synthesised by `tone_player` (`src/audio/`) sample by sample, so
there is nothing to store and they sound the same on the clock. A station that
has not started within 15 seconds, or stops, gives way to the tone, and the
screen says so. The volume rises from 5 % to the alarm volume over the ramp
time, and an alarm nobody answers stops after 15 minutes. The audio output
takes one writer at a time, so the radio is stopped before a tone starts, and
the radio's own volume comes back afterwards.

Stop & Listen ends the alarm but leaves its station playing, as if it had been
picked on the radio page, at the level the alarm had risen to; the radio page's
slider moves there. When the station gives way to the tone, the button goes.

Snooze silences the alarm for the snooze time, and meanwhile the clock page's
chip reads "Snoozed" with the time it rings again. The alarm settings live in
`data/settings.json`:

    "alarms": { "snooze_minutes": 9, "volume": 80, "ramp_seconds": 30 }

### Weather

`page_weather` stacks three cards, top to bottom:

- **Now** -- the refresh glyph and when the forecast was fetched in the top
  left corner, over a large drawn icon and the temperature, the condition with
  today's high and low, and the city; then, from the top on the right,
  feels-like, humidity, dew point, wind, sunrise and sunset, the moon
  drawn as it looks with how much of it is lit and whether that is growing,
  and the next new moon.
- **Next 20 hours** -- `PAGE_WEATHER_HOURS` (10) slots, one every two hours,
  each with its time, icon, temperature and chance of rain. The card has half
  the usual padding above and below, which is what lets a slot's four lines
  fit its height.
- **Next 7 days** -- `PAGE_WEATHER_DAYS` (7) columns, today first and on a
  tile, each with an icon, chance of rain, and the high over the low joined by
  a vertical range bar.

The range bars share one scale -- the week's coldest low at the bottom of
every track, its warmest high at the top -- so the warm and cool days read at a
glance. They are `lv_bar`s in `LV_BAR_MODE_RANGE`. `lv_bar` paints an
indicator gradient across the whole track and clips it to the indicator, so
the cool-to-warm colour stands for the same temperature in every column.
Moving both ends of a range bar has an ordering trap: each end is clamped
against the other, so `page_weather_set_daily()` opens the bar fully before
placing the low and high.

A chance of rain of `RAIN_NOTABLE_PERCENT` (30 %) or more is drawn in the rain
blue; anything below stays dim, so the wet slots stand out.

The service feeds it with plain data and never touches LVGL:

```c
page_weather_set_places(names, 3, 0);            /* "Athens", "Paris", "Oslo" */
page_weather_set_updated("Updated 10:15 AM", false);
page_weather_set_now(&now);
page_weather_set_hourly(hours, PAGE_WEATHER_HOURS);
page_weather_set_daily(days, PAGE_WEATHER_DAYS);
```

Temperatures are whole degrees in whatever unit the service works in; the page
only appends the degree sign. Passing fewer entries than there are slots hides
the rest.

With more than one city, the city's name is a button with a chevron, and a tap
opens the list of them, as the air quality page's sensor button does -- the
clock's own location first, marked with a house, then the cities added in
Settings. Picking one calls the page's place callback. With only the clock's
own, the name is plain text. While the page is covered, a city still loading or
failing to, the same button stays up over the cover in the corner, so a city
that will not load never traps the page.

#### The weather feed

`ui_weather_feed` fetches the forecasts from [Open-Meteo](https://open-meteo.com),
which is free and needs no key: the current conditions, the next 20 hours, the
week, and the quarter-hourly precipitation behind the clock page's MinuteCast
band; and, in a second request, the air quality forecast. The requests and the
parsing are plain C in `src/weather/open_meteo.c`, and the answers are read on
the download worker's thread.

The location comes from the settings or, while they leave it automatic, from
the public IP's location at ip-api.com. The other cities the weather page can
show are listed with it, up to `SETTINGS_PLACES_MAX` (6):

    "location": { "auto": false, "name": "Athens", "latitude": 37.98, "longitude": 23.73,
                  "places": [ { "name": "Paris", "latitude": 48.86, "longitude": 2.35 } ] }

It fetches at start, then every half hour for the weather and every hour for
the air quality. A failure is retried after a minute, then after two, doubling
up to a quarter of an hour. Changing the location, or `fahrenheit`, fetches
again at once.

A city picked on the weather page has a forecast of its own, fetched when it
is picked and then on the same schedule, and dropped when another is picked or
the city is removed. Everything else keeps to the clock's location: the clock
page's weather section, which names the city on a tab at its right border,
the automatic theme's sunrise and sunset, and the air quality. Times in another
city's forecast are shown in the clock's own time zone.

Cities are found by name with Open-Meteo's geocoding
(`open_meteo_search_url()`, `open_meteo_parse_search()`): up to five, best
first, each with its region and country to tell namesakes apart. The feed
answers the Settings page's searches, and drops the answer to a search typed
over by a newer one.

Until the first forecast arrives the page is covered by a spinner, or by what
went wrong with a Try again button (`page_weather_set_state()`). After that a
failed refresh leaves the forecast up: the "Updated" line turns amber and a
notice says the refresh failed. The refresh glyph beside that line fetches on
demand, and turns into a spinner while it does.

The moon comes with the forecast: Open-Meteo's daily `moon_phase`, 0 at new
moon and 0.5 at full, asked for from 31 days back to the 16 days ahead the
forecast reaches (`past_days` only lengthens the daily data; the hourly and
quarter-hourly keep their own ranges). Today's value gives how much of the
disc is lit, with a green arrow up while tomorrow's share is larger and a red
one down while it is smaller, and draws `ui_moon_icon` lit from the side it is
seen lit -- the other side south of the equator.

A new moon falls between the two days where the phase wraps round, placed
between their middles in proportion; that puts September 2026's at 03:30 UTC
on the 11th, against the US Naval Observatory's 03:27. The next new moon is
the first such within the forecast. When it is further off, it is the last one
before today, always within the 31 days back, plus a mean lunation, which the
real one keeps within some hours of.

Tapping the weather section of the clock page opens this page, awake only,
the same way the air quality section opens its page.

#### Weather icons

The built-in fonts have no weather glyphs, so `ui_weather_icon` draws them from
rounded objects -- discs and pills for the sun, moon, clouds, rain streaks,
snowflakes and fog bands, plus the symbol font's bolt for thunderstorms. The
shapes are laid out on a 100x100 grid and scaled, so one set of numbers serves
the 96 px headline icon and the 40 px daily ones.

```c
lv_obj_t * icon = ui_weather_icon_create(parent, 48, UI_WEATHER_SHOWERS);
ui_weather_icon_set(icon, UI_WEATHER_CLEAR_NIGHT);
```

Night variants are separate conditions, because the service knows sunrise and
sunset and the icon does not. The moon's crescent is a second disc painted in
the colour of the nearest opaque ancestor, so give a surface its background
*before* creating an icon on it. The bolt is a glyph rather than an `lv_line`:
with `LV_USE_FLOAT` on, percentage line points are stored as floats and lose
their encoding, so a zigzag could not scale with the icon.

Replace the icons with a weather icon font or images whenever you add one;
callers only ever deal in `ui_weather_t`.

#### Navigation rail icons

The rail reads Home, Alarms, Weather, AQI, Radio and Devices. The built-in
symbol font has no cloud, sun, wind, radio or bulb, so the rail draws its
icons with `UI_FONT_ICON` (`src/ui/fonts/ui_font_icons_28.c`): a tiny font cut
from [Font Awesome](https://fontawesome.com) Free 6.7.2 Solid (icons CC BY
4.0, font SIL OFL 1.1) holding only the glyphs the symbol font lacks --
cloud-with-sun for Weather (`UI_SYMBOL_WEATHER`), wind for AQI
(`UI_SYMBOL_AIR`), a classic radio for Radio (`UI_SYMBOL_RADIO`) and a light
bulb for Devices (`UI_SYMBOL_DEVICES`). LVGL's bundled FontAwesome copy is an
old 5.x, and no Free 5.x font has the radio, which is why the source is fetched
from npm instead. It falls back to Montserrat 28 for everything else, so the
other pages' `LV_SYMBOL_*` icons render through it unchanged. The
`lv_font_conv` command to regenerate it, with more code points, is in
`ui_theme.h`.

### Air quality and the sensors

The clock measures the room with a Sensirion SEN69C -- PM1.0 to PM10, the VOC
and NOx indices, formaldehyde and CO2 -- and an SHT45 for the temperature and
humidity, which reads more accurately than the SEN69C's own sensor beside its
fan. The drivers are in `src/sensors/sensor_hw_esp.c` (I2C, not yet tried on
the board); in the simulator `sensor_hw_sim.c` makes up a bedroom whose CO2
builds overnight and whose particles rise at breakfast and dinner.

`sensor_sampler` reads the sensors every two seconds on its own thread, and on
the LVGL thread `ui_air_feed` takes each reading to:

- the air quality page's tiles, drawn amber or red once a value is worth a
  look, and "--" while a sensor warms up or does not answer;
- `sensor_history`: 96 averages each over the last hour, day and week, which
  the chart plots and the analysis card sums up as the index's low, average,
  high and trend. It is written to the SD card every 10 minutes
  (`data/sdcard/sensors/history.bin` in the simulator), so a restart does not
  start the week over;
- the clock page's air section;
- the MQTT broker.

The sensor selector lists "Indoor" and the forecast's place. Outdoors, PM2.5,
PM10 and the index come from the Open-Meteo air quality forecast -- hourly over
the past week, interpolated onto the chart -- with the temperature and humidity
from the weather forecast. The forecast strip shows the index now, every three
hours for the next twelve, and tomorrow's worst hour, with its own refresh.

Under the chart, the times: five across the hour or the day, or the days
across the week, the last being now, each with a division line up from it. A
reading's unit decides its value axis. The unit of the most plotted readings
gets the axis on the left, the next the axis on the right -- the first plotted
wins a tie -- with the unit over each, since under them the first and last
times would run into it. Readings sharing an axis share its range, the span of
all their own widened to even steps (1, 2, 2.5 or 5 times a power of ten), so
its numbers hold for every trace on it and read 0 5 10 15 20 rather than
1 5 9 13 16; a reading in a third unit is stretched over the chart on its own
range, without an axis. `page_air_quality_set_history()` is told the scale
values come in, so an axis with steps under one shows tenths.

#### Readings over MQTT

Every `publish_interval` seconds the readings since the last message are
averaged and published as one JSON object to `topic`:

    smartclock/sensors  {"pm1_0":3.9,"pm2_5":5,"pm4_0":5.6,"pm10":8,"voc_index":112,
                         "nox_index":1,"hcho":17.2,"co2":478,"temperature":22.8,
                         "humidity":42.6,"aqi":27}

With `discovery` on, every metric is announced to Home Assistant's MQTT
discovery (`homeassistant/sensor/<client_id>/<key>/config`, retained) whenever
the connection comes up, so they appear as one device with the right units and
device classes. `temperature_offset` corrects the SHT45 for the warmth of the
case:

    "sensors": { "publish_interval": 60, "temperature_offset": -1.5,
                 "topic": "smartclock/sensors", "discovery": true }

`src/net/mqtt_client.h` is the seam. In the simulator there is no broker: the
link follows the settings feed's pretend connection and messages are only
logged. On the clock it is where esp-mqtt goes.

### Radio

The Radio page plays stations from [Radio Browser](https://www.radio-browser.info),
a free, community-run station directory with an open API. Every saved station
is one of its records, kept by the record's `stationuuid`.

The list shows each station's favicon, its name, and a line of country,
language and genre -- "United Kingdom • English • Alternative" -- with no
captions. Tapping a station plays it; previous and next step through the list.
**+** opens a dialog that searches the directory by name, tag or country
(`/json/stations/search`, most played first, broken stations hidden), with the
results' favicons; **+** on a result saves it, and a tick marks stations
already saved -- tapping the tick removes the station again, without asking. **Edit** slides a remove button and a drag handle onto every
station, a row at a time; **Done** folds them away again. Dragging a station by
its handle moves it through the list live, with a faded copy of the row under
the finger to show the drag has taken hold. The copy is plain parts faded one
by one over a solid backing -- no whole-object opacity, rounded clipping or
shadow -- so the clock's software renderer draws it without an off-screen
layer.
A remove button asks first (`ui_confirm`: the station's name, Cancel and a red
Remove; a tap outside cancels too). Removing the station that is playing stops
it and clears the selection, and the question says so.

#### On the SD card

```
/radio/stations.json          {"stations": ["<uuid>", ...]}, in list order
/radio/<uuid>/station.json    the directory's record: name, stream URL, favicon
                              URL, country, language, tags, codec, bitrate, HLS
/radio/<uuid>/favicon.bin     the favicon as a 96 x 96 ARGB8888 LVGL image
```

A favicon is converted once, when it is downloaded, so drawing the list never
decodes a PNG or JPEG. stb_image reads PNG, JPEG, BMP, GIF and ICO; a station
whose favicon is missing, unreachable, or in another format (SVG, WebP) shows
the default music icon. On the very first start there is no `stations.json`,
so eight stations are saved by UUID and their records and favicons fetched in
the background; that list is at the top of `ui_radio_feed.c`.

In the simulator the card is `data/sdcard/`, which is git-ignored. Delete it to
start again from the eight.

#### Downloads off the LVGL thread

`src/net/http_worker.c` runs downloads on two background threads and hands the
results back through `http_worker_poll()`, which the radio feed calls from an
LVGL timer. Decoding and writing a favicon happen on the worker as well, which
is why they use `malloc()` and stb_image rather than LVGL's allocator and image
decoders: LVGL's heap is not thread-safe. Requests go through `http_stream.c`:
WinHTTP in the simulator, and `esp_http_client` with the certificate bundle on
the clock -- written, but not yet run there.

#### Playback

Stations play for real, with the same code in the simulator and on the clock:
`src/audio/radio_player.c`. Playing a station starts two background threads, a
reader that pulls the stream into a quarter-megabyte buffer and a decoder that
plays from it. Sound starts once a second or so is buffered, and a stream that
runs dry pauses to fill up again rather than stuttering. Changing station
abandons the old threads without waiting on a slow server; they finish and
free themselves.

| Stream | Played with |
|---|---|
| Icecast or Shoutcast MP3 | minimp3 (`third_party/minimp3`, CC0) |
| Icecast or Shoutcast AAC and HE-AAC | Helix AAC (`third_party/helix-aac`, RealNetworks Public Source License) |
| Track titles | the ICY metadata those servers send, shown under the station name; an empty " - " shows nothing |
| HLS | the playlist, or a master playlist's first variant; MPEG-TS or packed-audio segments |

Encrypted HLS and fragmented-MP4 segments are not played; the page says so. A
stream that fails or drops is retried three times. Helix does not check a frame
against its input, so the player hands it whole ADTS frames only, and a guard in
the vendored `bitstream.c` stops a damaged frame reading past its buffer.

minimp3 keeps its state between frames -- the bit reservoir a frame borrows
from the frames before it, and the overlap that joins them -- only while the
next frame's header follows the frame it is given. Handed a window that ends
just after a frame, it resets and that frame comes out wrong or silent. The
decoder's window ends wherever the network happened to stop, so it holds back
`MP3_LOOKAHEAD_BYTES` (the largest frame plus a header) until more arrives.
Without that, streams broke up into distortion. A harness decoding 20 s
captures of three stations the player's way decoded 0-22 % of the audio
correctly; with the lookahead, every sample matched decoding the capture in
one piece.

Only the audio output differs between the two builds (`src/audio/audio_sink.h`):

- `audio_sink_sdl.c`: the simulator, through SDL2's queued audio.
- `audio_sink_esp.c`: the board's ES8311 codec and NS4150B amplifier, through
  Waveshare's BSP component `waveshare/esp32_p4_wifi6_touch_lcd_7b` (which
  brings `esp_codec_dev`), with stereo mixed down for the one speaker. Written,
  but not yet run on the board.

Threads come from `src/os/os_port.c` (Win32 in the simulator, POSIX threads on
ESP-IDF), and the connection from `http_stream.c`. As the directory asks of its
clients, starting a station also tells it about the play (`/json/url/<uuid>`).

Names render in the UI text fonts (`ui_font_text_*`, see `ui_theme.h`): Latin
with its European accents, Cyrillic and Greek. A name in a script they lack --
CJK, Arabic, Hebrew, emoji -- shows placeholder boxes for those characters.

### Devices

The Devices page shows one square tile per device: its name along the top
and its state in the middle. Glyph devices show an icon that is grey when off
and coloured when on: a yellow bulb, a green plug, a blue droplet, an orange
flame. A light glows in whatever it was set to last, a colour or a colour
temperature: the hub stamps each change, and `device_light_color()` picks the
newer of the two and maps colour temperature onto the same cool-to-warm ramp
as its slider. An LED strip shows a wand with sparkles, in the same colour.
Curtains are drawn, with panels that close as the position drops. Sensors and
thermostats show their readings instead of an icon.

A short line under the icon carries what the colour cannot: brightness, the
effect and brightness of an LED strip, position, speed or mode. Humidifiers
and dehumidifiers show their readings without opening the panel: the icon, the
room's humidity and the target stack down the tile, spread evenly, with a gap
under the name. They have no detail line, so the three have room.

What a tap does depends on what the device can do:

| Device | Tap |
| --- | --- |
| Plug, switch, heater; light, fan, purifier or (de)humidifier with only power | Toggles power |
| Light or LED strip with brightness, colour temperature, colour or effect | Opens its panel |
| Fan, purifier, (de)humidifier with speed, mode or target humidity | Opens its panel |
| Curtain, lock | Opens its panel |
| Thermostat | `-` / `+` on the tile step the setpoint; the tile opens a panel if there is a mode or power channel |
| Sensor, door/window contact, motion | Nothing |

The grid scrolls from anywhere, including the gaps between tiles and the
space below the last row.

The panel opens over the dimmed page, with a power switch and one row per
writable channel: brightness, colour temperature, colour swatches, effects,
speed, mode, target temperature or humidity, curtain position with
Open/Close/Stop, and a Lock/Unlock button. The rows carry no captions; each
control is recognisable by itself. A light's panel leads with a bulb showing
its current colour.

- Colour swatches and Open/Close/Stop are actions, not selections: tapping one
  sends the command, and nothing stays highlighted. Effects, modes and speed
  presets do show which one is active.
- Sliders send one command on release rather than a burst while dragging. They
  follow the device's reports whenever they are not being dragged, and every
  report counts, even one that repeats the last value, so a slider the device
  did not follow snaps back.
- Open and Close put a curtain's position straight at 100 or 0, on the slider
  and the tile, without waiting for the curtain. The command says where it is
  going, and many drivers report their position only on arrival, or never.
  `device_hub_set()` makes that assumption for cover options named "open" or
  "close". A driver that does report while moving still moves the slider
  through its reports. Stop assumes nothing: if the driver reports where it
  stopped, the slider follows; if not, the slider keeps showing the end it was
  heading for, because there is no way to know better.
- Effects are one button naming the current effect. It opens a wheel picker
  over the panel, an `lv_roller` in the style of an iOS picker: scroll until the
  effect sits in the middle band, then Apply, or close it with the x (or a tap
  outside) to leave the light as it is. Nothing is sent while scrolling, so the
  light does not flash through the effects on the way. The wheel opens on the
  effect running now.
- Long mode lists, over four options, wrap onto rows of four buttons.

Every tap is a request. The tile only changes when the device reports its new
state back over MQTT, so the page always shows what the device really did.

The room chips and the scene bar come from the configuration. Rooms are listed
in the order their first device appears, and the chips are hidden when there
is only one room. The scene bar disappears when there are no scenes.

#### The device configuration

Devices are described in a JSON document. `data/devices.json` is a complete
example covering every device type, in both MQTT styles:

```json
{
  "devices": [
    {
      "name": "Bedside lamp", "type": "light", "room": "Bedroom",
      "state": "zigbee2mqtt/bedside_lamp", "command": "zigbee2mqtt/bedside_lamp/set",
      "channels": {
        "power":      { "key": "state" },
        "brightness": { "key": "brightness", "max": 254 },
        "color":      { "key": "color", "format": "rgb" }
      }
    },
    {
      "name": "Kettle", "type": "switch", "room": "Kitchen",
      "channels": {
        "power": { "state": "stat/kettle/POWER", "command": "cmnd/kettle/POWER" }
      }
    }
  ],
  "scenes": [
    { "name": "Good night", "topic": "home/scene/set", "payload": { "scene": "good_night" } }
  ]
}
```

A device has a `name`, a `type`, an optional `room`, and a `channels` object.
Each channel binds one attribute to MQTT. Device-level `state` and `command`
topics are inherited by every channel that does not name its own. That keeps a
Zigbee2MQTT device, where everything shares one topic, to one line per
attribute.

Types: `light`, `led_strip` (also accepted as `wled`), `switch`, `plug`,
`fan`, `air_purifier`, `humidifier`, `dehumidifier`, `heater`, `curtain`,
`thermostat`, `lock`, `sensor`, `contact`, `motion`.

| Channel | Value | Defaults |
| --- | --- | --- |
| `power` | on/off | `on` "ON", `off` "OFF" |
| `brightness` | percent of `min`..`max` | 0..100 |
| `color_temp` | raw | 153..500 (mireds) |
| `color` | RGB | `format` "hex" |
| `effect` | one of `options` (required) | |
| `speed` | percent, or one of `options` | 0..100 |
| `mode` | one of `options` (required) | |
| `position` | percent, 100 = open | 0..100 |
| `cover` | one of `options` | OPEN, CLOSE, STOP |
| `target_temperature` | number | 5..35, `step` 0.5 |
| `target_humidity` | number | 30..80, `step` 5 |
| `temperature`, `humidity`, `co2` | number, read-only | |
| `lock` | locked/unlocked | `on` "LOCK", `off` "UNLOCK" |
| `contact` | open/closed, read-only | `on` "OPEN", `off` "CLOSED" |
| `motion` | detected/clear, read-only | `on` "ON", `off` "OFF" |

Channel fields, all optional:

- `state`: the topic the value is read from. Without one, the page assumes a
  command worked.
- `command`: the topic changes are published to. Without one the channel is
  read-only; set `"command": ""` to stop a channel inheriting the device's.
- `key`: read and write the value inside a JSON object. Dotted for nesting,
  so `"AM2301.Humidity"` reads Tasmota's sensor report. Without a key, the
  whole payload is the value.
- `on` / `off`: payloads for the two states. They can be strings, booleans or
  numbers, e.g. Zigbee2MQTT's door sensor is `"on": false, "off": true`
  because it reports `contact: true` when the door is closed.
- `min` / `max`: the device's raw range, for percentages.
- `step`: increment for setpoint buttons.
- `options`: names for effects, modes or speed presets, shown on the buttons.
- `values`: what the device calls each option, one per option, when that is
  not its name. WLED numbers its effects, so `"values": [0, 9]` sends and
  reads 0 and 9 for "Solid" and "Rainbow". A numeric value goes into JSON as a
  number.
- `template`: a plain command with `{}` standing for the value, e.g. `"FX={}"`.
- `tag`: read the value from between `<tag>` and `</tag>` in the payload, for
  devices that report XML. Not both `tag` and `key`.
- `format`: `"hex"` for `"#RRGGBB"`, `"rgb"` for `{"r":..,"g":..,"b":..}`,
  or `r,g,b` as a plain payload.

WLED has no single JSON state topic. It reports brightness on `<name>/g`
(0 when off), colour on `<name>/c`, and its full status as XML on `<name>/v`.
It takes `ON`/`OFF` and brightness on `<name>`, colour on `<name>/col`, and API
commands on `<name>/api`, so an effect is sent as `FX=<id>` and read back from
`<fx>`:

```json
{
  "name": "TV backlight", "type": "wled", "room": "Living room",
  "channels": {
    "power":      { "state": "wled/tv/g", "command": "wled/tv" },
    "brightness": { "state": "wled/tv/g", "command": "wled/tv", "max": 255 },
    "color":      { "state": "wled/tv/c", "command": "wled/tv/col" },
    "effect":     { "state": "wled/tv/v", "tag": "fx", "command": "wled/tv/api", "template": "FX={}",
                    "options": ["Solid", "Breathe", "Rainbow", "Fire 2012"],
                    "values":  [0, 2, 9, 66] }
  }
}
```

Effect ids are WLED's; check them against the effect list of your WLED
version. Power reads the brightness topic: a bool channel that receives a
number treats anything but 0 as on.

A channel written as a bare string is a read-only state topic:
`"temperature": "home/attic/temperature"`.

On the way in, colours are also accepted as `{"hex": "#RRGGBB"}`, and
booleans fall back to `true`/`on` and `false`/`off`, then to any number
(non-zero is true). Options match either their value or their name. A JSON
message that leaves a key out does not change that channel, so partial updates
are safe.

Limits: 32 devices, 6 channels per device, 6 scenes, 12 options per channel.
Topics can be up to 95 characters. The parser reports the first problem with
the device number and name, e.g. `device 4 (Curtains): unknown channel
"postion"`, and a broken document never replaces a working one.

#### Getting the configuration onto the clock

The simplest delivery uses the broker the clock needs anyway. Publish the file
as a retained message, and the clock receives it on every connect:

```bash
mosquitto_pub -h broker.local -r -t smartclock/config/devices -f devices.json
```

To change the devices, publish again. `ui_devices_feed_load()` takes the
payload, and the page rebuilds. On the ESP32, raise esp-mqtt's
`buffer.size` above the file's size, or the message arrives in fragments.

#### Code layout and the simulated broker

- `src/devices/` is plain C, with no LVGL: `device.h` is the model,
  `device_config.c` the JSON parser (cJSON, which ESP-IDF ships as its `json`
  component), and `device_hub.c` holds the live state. The hub matches
  incoming topics to channels, extracts and scales values, and formats
  commands. It owns no connection.
- `ui_devices_feed.c` wires the hub to the page. In the simulator it also plays
  the broker. It reads `data/devices.json` from the working directory, seeds
  the example devices' state, and echoes every command back on the state
  topics that read from it, 150 ms later, the way a real device confirms. A
  templated command read back through a tag is answered in that tag, so WLED's
  `FX=9` comes back as `<fx>9</fx>`. The pretend curtains behave like the
  simplest real drivers: they report nothing while moving and nothing on Stop,
  so their position comes only from a position command or from the end the
  hub assumes for Open/Close. Every publish is logged as
  `MQTT publish <topic> <payload>`.

On the clock, an MQTT client replaces the simulated half:

1. subscribe to the configuration topic and pass its payload to
   `ui_devices_feed_load()`;
2. subscribe to `device_hub_subscriptions()` (again after each load);
3. feed every message to `device_hub_handle_message()`;
4. publish whatever `device_hub_set_publish_cb()` hands over, and show the
   connection with `page_smart_home_set_link()`.

The hub is not thread-safe, and esp-mqtt calls back on its own task, so
marshal messages onto the LVGL thread, or take `lv_lock()`, first.

The device icons come from a second icon font, `UI_FONT_ICON_LG`
(`ui_font_icons_48.c`), cut from the same Font Awesome source at 48 px. The
command and code points are in `ui_theme.h`. Font Awesome Free has no blinds,
which is why the curtain is drawn.

### Settings

The Settings page, last in the rail, has five tabs:

- **Wi-Fi**: the connection status, the network name and password with
  Connect, and beside them the networks a scan found. Tapping a network fills
  in its name and, if it is secured, moves on to the password.
- **MQTT**: broker and port, client ID, username and password, the device
  configuration topic and TLS, then the status beside Save & connect.
- **Date & time**: set automatically from a time server, or set the date and
  time by hand with a wheel picker; automatic time zone (from the location of
  the public IP) or one chosen from a list; then the 24-hour clock, the date
  format (DD/MM, MM/DD or YYYY-MM-DD) and seconds. The date picker's wheels
  follow the date format, and the time picker has an AM/PM wheel only on the
  12-hour clock.
- **Weather**: automatic location, the default, found from the public IP, or
  the clock's city typed in; and more cities for the weather page, typed in the
  same way, each with a button to remove it. A city's name is searched for
  when Enter is pressed, and the cities found are listed under the field with
  their region and country; a tap takes one.
- **Device**: brightness, automatic or a level; always-on display, and its
  brightness, automatic or a level up to 20 %; dark, light or automatic theme,
  accent colour, and how soon the clock goes idle; then language (English only
  so far), °C or °F, and the languages the keyboard offers; and face wake,
  with 5 or 10 frames a second and 3 to 7 frames in a row.

Brightness belongs to the backlight alone. Nothing on screen is drawn
differently at any level. When a brightness is automatic, its slider sets how
strongly the light sensor moves the backlight instead of a level, and the
slider's name changes to say so.

#### Idle: always-on display, screen off and face wake

Untouched for the idle time, the clock goes to its ambient face. With
**always-on display** on, that is what stays up, at the always-on brightness,
which is never more than `SETTINGS_IDLE_BRIGHTNESS_MAX` (20 %) -- an older,
brighter setting comes down to it when it loads. Off, the screen goes off
instead: `ui.c` covers everything with black on the system layer and the
backlight is turned off. The ambient face stays underneath, so switching
always-on back on brings it straight back. A touch on the dark screen, an
alarm ringing, or a face wakes it with `ui_wake()`, which also brings the clock
out of its ambient face; the touch that wakes it does nothing else. Without
always-on, the idle time row reads "Screen off after" and the always-on
brightness rows go.

**Face wake** watches for someone looking at the clock while it is idle.
`presence/face_wake.c` runs one thread of its own that looks at a camera frame
5 or 10 times a second through `face_detector.h`, and a face in 3 to 7 frames
in a row -- the settings say how many -- raises a wake, so a face passing
through the picture does not count. `ui_presence_feed` starts it only while
idle with face wake on, stops it otherwise (the thread closes the camera), and
calls `ui_wake()` on a wake. If the camera will not start, a notice says so.

On the clock, `face_detector_esp.cpp` grabs RGB565 frames from the MIPI-CSI
camera through esp_video's V4L2 device and runs a small one-class YOLO face
model -- YOLOv8n-face or YOLO11n trained on WIDER FACE, quantised to int8 with
ESP-PPQ and flashed to a `face_model` partition -- with ESP-DL, keeping the
best box over a score threshold. It has not been tried on the board; the file
lists what to check first. The simulator has no camera: hold **F** in its
window and `face_detector_sim.c` sees a face in every frame.

Device settings take effect as they change. Wi-Fi and MQTT credentials are only
used when their button is pressed, so a half-typed password never drops a
working connection, and a Wi-Fi network is only stored once it has been
joined. Tapping a text field brings up an on-screen keyboard, and the tab
scrolls the field clear of it.

Every keyboard comes from `ui_keyboard_create()` and is given its field with
`ui_keyboard_attach()`, which also says what the field takes: text typed as
keyed, sentences, or a number. It is LVGL's keyboard -- one button matrix,
however many keys -- made to work like Android's Gboard:

- Gboard's letter layouts for English, Greek, German, French, Spanish, Russian
  and Ukrainian. English is always on, since networks, brokers and topics are
  typed in it; the others are ticked in a panel of checkboxes opened from
  "Keyboard languages" on the Device tab, stored as
  `"keyboards": ["en", "el"]`. With more than one on, the globe key goes on to
  the next. The space bar names the language in use, and every keyboard
  follows it.
- Shift capitalises the next letter. Tapped twice, it locks capitals, with a
  bar under the arrow.
- Holding a key opens its alternatives above it -- the number on a top-row key
  (shown in its corner), accented letters, more punctuation on the full stop
  -- to pick by sliding onto one and letting go.
- A preview of the key shows above the finger, and sliding onto another key
  before letting go types that one instead.
- Sliding along the space bar moves the cursor.
- Two symbol pages, `?123` and `=\<`, as Gboard's, back to the letters after a
  space; a phone number pad for number fields (the MQTT port).
- In sentence fields (an alarm's name), a capital starts each sentence, and a
  second space straight after a word becomes a full stop.
- Enter is the accent colour, a tick for one-line fields; a hide key closes
  the keyboard without it.

Hints, the space bar's language, Shift's state and Enter are drawn onto the
keys as the button matrix draws them, and the preview and the pop-up are small
objects on the top layer, made when needed -- none of it adds objects per key.
A language is three strings of letters in `ui_keyboard.c`, and holding a key
offers the same alternatives in every layout.

The settings are plain C in `src/settings/` and are stored as JSON with cJSON,
like the device configuration, so the same code serves the simulator and the
clock. Missing or out-of-range values take their defaults, so an old file
still loads. `ui_settings_feed.c` loads and stores them and applies each one:

| Setting | Applied by |
| --- | --- |
| Theme, accent | `ui_theme_set()`, then `ui_rebuild()`; the automatic theme is looked at every minute |
| 24-hour clock, date format | `ui_format`, then `ui_rebuild()`, so every time and date on screen is written again |
| Seconds | `ui_clock_feed` |
| Time set by hand, time zone | `clock_time` |
| Time server | SNTP on the clock |
| Brightness, always-on brightness | the backlight, off while the screen is; a logged hook in the simulator |
| Idle after, always-on display | `ui_set_idle_timeout()`, `ui_set_always_on()` |
| Face wake | `ui_presence_feed`, while idle |
| Location, more cities | `ui_weather_feed`: the forecasts fetched again for a new location; the weather page's list of cities |
| Temperature unit, language | stored for the weather service and translations to use |
| Keyboard languages | `ui_keyboard_set_languages()` |

#### Changing the theme without a restart

The surface, text and accent tokens in `ui_theme.h` (`UI_COLOR_BG`,
`UI_COLOR_TEXT`, `UI_COLOR_ACCENT`, ...) read from a runtime palette,
`ui_palette`, which `ui_theme_set()` fills from a dark or light table and the
chosen accent. The automatic theme is light from today's sunrise to its
sunset where the forecasts are for (`ui_weather_feed_sun_today()`), and
before there is a forecast from 7:00 to 19:00 local time; the settings feed
looks every minute and rebuilds the UI when it crosses either. Status and identity colours such as good/warn/bad, rain and the
calendars are the same in both themes. The weather icons' greys and whites
follow the theme so they stay visible on a white card.

Widgets take their colours when they are built, so a change of theme is
followed by `ui_rebuild()`. It gives every page its `on_hide` (which closes
popups and panels on the top layer), keeps the user's alarms, builds the
screen again, returns to the page that was showing, and has each feed tell the
new pages what it last told the old ones (`ui_clock_feed_refresh()`,
`ui_devices_feed_republish()`, `ui_settings_feed_republish()`). The rebuild is
deferred with `lv_async_call()`, because the tap that asked for it lands on a
button the rebuild deletes. `ui_theme_set()` also re-initialises LVGL's default
theme, for the parts of stock widgets that no page styles: keyboard keys and
dropdown lists. It is given `UI_FONT_XS`, so those show the same letters as
the rest of the UI.

A page that holds state only in its widgets loses it in a rebuild. Keep such
state in the page's statics, or behind a feed, as the alarms, devices and
settings are.

#### Times and dates

Every time and date on screen goes through `ui_format.h`, which reads the
24-hour and date format settings. That covers the clock, its date line and
next-alarm chip, the alarm cards and editor wheels, calendar entries, and the
weather page's hours, sunrise, sunset and "Updated" time. Nothing formats a
time by hand, so one setting changes them all.

| Function | 12-hour, DD/MM | 24-hour, MM/DD | YYYY-MM-DD |
| --- | --- | --- | --- |
| `ui_format_time` | 7:24 PM | 19:24 | |
| `ui_format_clock` + `ui_format_meridiem` | 7:24 + PM | 19:24 + NULL | |
| `ui_format_hour` | 2 PM | 14:00 | |
| `ui_format_date_long` | Monday, 14 September | Monday, September 14 | Monday, 2026-09-14 |
| `ui_format_date_short` | Mon 14 Sep | Mon Sep 14 | Mon 09-14 |
| `ui_format_date_numeric` | 14/09/2026 | 09/14/2026 | 2026-09-14 |

Text already on screen does not change by itself, so a change of either
format rebuilds the UI, as a change of theme does. The alarms editor then comes
back with the right wheels, 00-23 or 12-11 with AM/PM. Services that push text
to pages (weather, calendar) must format through the same functions.

The time itself comes from `clock_time` (`src/settings/`, plain C): the
system clock (SNTP on the device) plus an offset when set by hand, in the
chosen zone. Zones are POSIX TZ strings, e.g. `EET-2EEST,M3.5.0/3,M10.5.0/4`,
which is what ESP-IDF's `setenv("TZ")` takes. `clock_time` works the
daylight-saving rules out itself, so the simulator shows the same local time as
the device would. The Date & time tab offers a table of common zones.

Pickers use `ui_picker.h`, a shared iOS-style wheel picker with up to four
wheels, Apply and a close button; the lights' effect picker uses it too.

#### In the simulator

Settings live in `data/settings.json`, which is git-ignored because it holds
passwords. A scan finds a fixed list of networks after a second. Joining works
for an open network, or for any password of eight characters or more, and the
broker always accepts the connection, which also sets the Devices page's
broker status. With no broker set, the Devices page keeps its simulated one.

Network time is the computer's clock. A time set by hand is kept as an offset
from it until the simulator closes. Automatic time zone takes the computer's
current UTC offset (shown as e.g. `UTC+03:00`), which gives the right time but
knows no daylight-saving rules. Brightness only logs what the backlight would
be set to, awake or idle.

Each of these is a `TODO` in `ui_settings_feed.c` for the clock: NVS, the
backlight PWM and light sensor, SNTP against the time server, `settimeofday()`
and the RTC, an IP geolocation lookup for the time zone, `esp_wifi` and
`esp-mqtt`.

### Calendar colours

Entries in the events section carry no label. The bar in front of the title is
the only thing saying which calendar an entry came from, so the four colours
are fixed and defined once in `ui_theme.h`:

| Calendar | Colour | `page_clock_calendar_t` |
| --- | --- | --- |
| National holidays | green | `PAGE_CLOCK_CAL_HOLIDAY` |
| Birthdays, anniversaries | yellow | `PAGE_CLOCK_CAL_OCCASION` |
| Personal | blue | `PAGE_CLOCK_CAL_PERSONAL` |
| Work meetings | purple | `PAGE_CLOCK_CAL_WORK` |

Each calendar is routed to one of the two columns. The left column is today's
schedule and labels entries with a clock time; the right column is the next
fortnight of all-day entries -- holidays, birthdays, name days -- and labels
them with the day instead. Neither column is titled: the timestamps and the
colours are what tell them apart.

The routing is configuration rather than a UI setting, and defaults to
holidays and occasions on the right, personal and work on the left:

```c
page_clock_set_calendar_column(PAGE_CLOCK_CAL_WORK, PAGE_CLOCK_COLUMN_UPCOMING);
```

`page_clock_set_events()` then takes one merged, already-sorted list and routes
each entry by its calendar. The columns fill independently and each holds
`PAGE_CLOCK_EVENTS_PER_COLUMN` entries, so a busy day cannot crowd out the
fortnight ahead. Deciding *which* events to pass -- today's, plus the next 14
days of all-day entries -- stays with the caller, since the page has no notion
of dates.

Fill `time` for timed entries and `day` for all-day ones; whichever matches the
column an entry lands in is the one shown, with a fallback to the other.

### Feeding data in

Pages expose typed setters and never reach out for data themselves, so the
clock service, weather client, sensor driver and MQTT client stay free of
LVGL:

```c
page_clock_set_time("07:24", "31", "AM");
page_air_quality_push_sample(18);
page_radio_set_state(PAGE_RADIO_STATE_PLAYING, NULL);
page_smart_home_update_device(index);
```

Input works the other way, through callbacks (`page_radio_set_play_cb()`,
`page_smart_home_set_command_cb()`, ...). Treat those as *requests*: let the
real state come back through the setters rather than assuming the action
succeeded. The calendar is the one part of the UI still showing placeholder
content; search for `TODO` for the seams left on the clock's side.

### Loading, errors and notices

`ui_status` gives every page the same three ways to say that something is
missing or went wrong:

- a **cover** (`ui_status_create()`, `ui_status_set()`) over a card, a section
  or a whole page until its first data arrives: a spinner while LOADING, or a
  warning, the reason and an optional Try again button when FAILED. It keeps to
  its parent's size and corners and stays out of its layout; one shorter than
  150 px, like the forecast strip's, lines its contents up in a row;
- a **refresh control** (`ui_refresh_create()`): a glyph the size of the text
  beside it, with a fingertip-sized touch area, that turns into a spinner while
  busy;
- a **notice** (`ui_notice_show()`): a banner at the top of the screen for a
  failure away from the page it concerns. It fades after six seconds, or at a
  tap.

Once data has been shown, a failure never takes it away: the data stays up,
marked stale, and a notice says what failed.

### Alignment and icons

`section_create()` centres its stack on both axes, so each region sits in the
middle of its own grid cell instead of hugging the top-left. The clock block
does the same within the cell its anchor reserves. Anything that should fill
its cell rather than be centred by it -- the calendar columns, for instance --
has to be content-sized, not `flex_grow`n, or it swallows the free space the
centring needs.

The on-board sensor readouts are icon-and-value only, with no caption: the icon
alone says which sensor it is, so both are drawn large enough to read across a
room. Humidity uses `LV_SYMBOL_TINT`, but the built-in Montserrat/FontAwesome
subset has no thermometer, so `thermometer_create()` draws one from two small
objects -- a rounded stem on a round bulb, scaled to a requested height. Swap
both for a proper weather icon font when you add one.

Sections come in two shapes. `section_create()` centres its whole stack in the
cell, which suits the weather column. `header_section_create()` pins the
caption to the top and hands back a body that fills everything below it, which
is what the calendar and air quality sections use.

Calendar entries stack from the top at their natural height rather than
stretching to fill, and each column scrolls once there are more than fit.
`PAGE_CLOCK_EVENTS_PER_COLUMN` is the point at which entries start being
*dropped*, not the point at which they stop being visible -- keep it well above
what fits on screen, or the scrolling can never engage.

Watch for `flex_grow` when a parent is centring its children: a grown child
swallows exactly the free space the centring needs, so it has to be one or the
other.

### Working with the theme

Build from the tokens in `ui_theme.h` (`UI_COLOR_*`, `UI_FONT_*`, `UI_GAP`,
`UI_RADIUS`, `UI_TOUCH_MIN`) rather than literals, and use `ui_card_create()` /
`ui_tile_create()` / `ui_label_create()` / `ui_slider_create()` so pages stay
consistent.

Use `ui_slider_create()` for every slider. Its knob stands out past the track
on all sides, and a row sized to its content clips it. Making the row
overflow-visible does not help, because LVGL still clips to the parent's
drawing area. So the helper gives the slider top and bottom margins covering
the overhang, which flex and grid rows make room for. The margins are
`UI_SLIDER_KNOB_ROOM`: the knob as it is while pressed, plus a couple of
pixels. The theme's own pressed growth scales with DPI, so the helper pins it
to `UI_SLIDER_KNOB_GROW`, which keeps the reserved room exact.

The ends are left to the row. Side margins would be wrong, because flex does
not subtract a growing child's side margins from the width it hands out, so
they push the neighbouring labels out of the row. The row has to leave
`UI_SLIDER_KNOB_OVERHANG` beside each end of the track: as side padding where
the slider meets the edge of the row, and as the column gap where it meets a
sibling. Otherwise, at 0 or 100, the knob covers the neighbouring label.

Two things worth knowing:

- **The text sizes are not the built-in fonts.** `UI_FONT_XS` to `UI_FONT_XL`
  are Montserrat cut with Latin-1, Latin Extended-A, Cyrillic, Greek and
  typographic punctuation (`src/ui/fonts/ui_font_text_*.c`; ranges and the
  command to regenerate are in `ui_theme.h`). Their `LV_SYMBOL_*` icons come
  from the built-in Montserrat of the same size, as a fallback. `UI_FONT_CLOCK`
  is still the built-in 48 px, with only ASCII, `UI_DEG` and `UI_BULLET`.
  Sources are compiled as UTF-8 (`/utf-8` on MSVC).
- **`LV_LABEL_LONG_MODE_DOTS` ellipsises on vertical overflow**, not
  horizontal. A label left at `LV_SIZE_CONTENT` height just grows to a second
  line and never shows dots. Use `ui_label_single_line(label, font)`, which
  pins the height to one line; the width still has to come from `flex_grow` or
  an explicit width.

## Run demos and examples

By default, the widgets demo (`lv_demo_widgets()`) will run. If you want to run a different demo or example from the LVGL library,
simply replace the demo function call in the code with another one—such as `lv_demo_benchmark()` or `lv_example_label_1()`.

```c
int main(int argc, char **argv)
{
  /* ... */
  /* Run the default demo */
  /* To try a different demo or example, replace this with one of: */
  /* - lv_demo_benchmark(); */
  /* - lv_demo_stress(); */
  /* - lv_example_label_1(); */
  /* - etc. */
  lv_demo_widgets(); 

  while(1) {
      /* ... */
  }
  return 0;
}
```

## Optional library

There are also FreeType and FFmpeg support. You can install these according to the followings:

### Linux

```bash
# FreeType support
wget https://kumisystems.dl.sourceforge.net/project/freetype/freetype2/2.13.2/freetype-2.13.2.tar.xz
tar -xf freetype-2.13.2.tar.xz
cd freetype-2.13.2
make
make install
```

```bash
# FFmpeg support
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg
cd ffmpeg
git checkout release/6.0
./configure --disable-all --disable-autodetect --disable-podpages --disable-asm --enable-avcodec --enable-avformat --enable-decoders --enable-encoders --enable-demuxers --enable-parsers --enable-protocol='file' --enable-swscale --enable-zlib
make
sudo make install
```
### (RT)OS support
Works with any OS like pthred, Windows, FreeRTOS, etc. It has build in support for FreeRTOS. 

## Test
This project is configured for [VSCode](https://code.visualstudio.com) and is tested on: 
- Ubuntu Linux 
- Windows WSL (Ubuntu Linux)

It requires a working version of GCC, GDB and make in your path.

To allow debugging inside VSCode you will also require a GDB [extension](https://marketplace.visualstudio.com/items?itemName=webfreak.debug) or other suitable debugger. All the requirements, build and debug settings have been pre-configured in the [.workspace](simulator.code-workspace) file.

The project can use **SDL** but it can be easily relaced by any other built-in LVGL dirvers.

## Integration with LVGL Pro

This project supports integration with LVGL Pro projects for UI development.

### Setup

1. Configure CMake with your LVGL Pro project folder:

```bash
cmake -B build -DLVGL_PRO_PROJECT_DIR=<path-to-lvgl-pro-project>
```

Build your project:

```bash
cmake --build build
```

### Usage in Code

In your main.c, include the UI header from your LVGL Pro project and replace the default demo with your screen.

```c
#include "ui.h"

int main(void) {

    /*Initialization code for LVGL*/
    
    /* Initialize the LVGL Pro UI */
    ui_init("<path-to-lvgl-pro-project>");
    
    /* ... rest of your application ...*/
}
```
