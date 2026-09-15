/**
 * @file page_smart_home.c
 *
 * Layout, room filter and scenes. The tiles are in smart_home_tile.c and the
 * controls panel in smart_home_modal.c.
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui/pages/smart_home/smart_home_private.h"
#include "ui/ui_theme.h"

#include <string.h>

/*********************
 *      DEFINES
 *********************/

/** Tiles per row. At 1024x600 that makes them about 170 px square, which fits
 *  a thermostat's - and + either side of a two-digit setpoint. */
#define TILE_COLUMNS 5

#define HEADER_HEIGHT 64
#define SCENES_HEIGHT 80

/** Room chips beyond this many are not shown. */
#define ROOM_MAX 6

#define ROOM_CHIP_HEIGHT 40

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_obj_t * create(lv_obj_t * parent);
static void       header_create(lv_obj_t * parent);
static void       body_create(lv_obj_t * parent);
static void       scenes_create(lv_obj_t * parent);

static void rooms_rebuild(void);
static void room_clicked(lv_event_t * e);
static void room_select(uint32_t index);
static void grid_resized(lv_event_t * e);
static void notice_apply(void);
static void scene_clicked(lv_event_t * e);

/**********************
 *  STATIC VARIABLES
 **********************/

static const ui_page_t desc = {
    .title   = "Devices",
    .icon    = UI_SYMBOL_DEVICES,
    .create  = create,
    .on_show = NULL,
    /*The panel lives on the top layer, above every page, so it has to be
     *closed explicitly -- e.g. when the idle timeout returns to the clock.*/
    .on_hide = sh_modal_close,
};

static const int32_t grid_cols[]          = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static const int32_t grid_rows[]          = {HEADER_HEIGHT, LV_GRID_FR(1), SCENES_HEIGHT, LV_GRID_TEMPLATE_LAST};
static const int32_t grid_rows_no_scene[] = {HEADER_HEIGHT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

static lv_obj_t * root;
static lv_obj_t * link_icon;
static lv_obj_t * link_label;
static lv_obj_t * room_bar;
static lv_obj_t * grid;
static lv_obj_t * notice;
static lv_obj_t * scene_card;
static lv_obj_t * scene_bar;

static const device_t * devices;
static uint32_t         device_count;
static lv_obj_t *       tiles[DEVICE_MAX];
static int32_t          tile_size = 160;

static char     room_names[ROOM_MAX][DEVICE_NAME_LEN];
static uint32_t room_count;
static uint32_t room_current;   /**< 0 is "All"; room i is chip i + 1 */

static char notice_text[160];

static page_devices_command_cb_t command_cb;
static page_devices_scene_cb_t   scene_cb;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const ui_page_t * page_smart_home_desc(void)
{
    return &desc;
}

void page_smart_home_set_devices(const device_t table[], uint32_t count)
{
    /*The panel may be showing a device that no longer exists.*/
    sh_modal_close();

    devices      = table;
    device_count = count > DEVICE_MAX ? DEVICE_MAX : count;

    lv_obj_clean(grid);
    for(uint32_t i = 0; i < device_count; i++) {
        tiles[i] = sh_tile_create(grid, i, tile_size);
    }

    rooms_rebuild();
    notice_apply();
}

void page_smart_home_update_device(uint32_t index)
{
    if(index >= device_count) return;

    sh_tile_refresh(tiles[index], index);
    sh_modal_refresh(index);
}

void page_smart_home_set_scenes(const device_scene_t scenes[], uint32_t count)
{
    if(count > DEVICE_SCENE_MAX) count = DEVICE_SCENE_MAX;

    lv_obj_clean(scene_bar);

    for(uint32_t i = 0; i < count; i++) {
        lv_obj_t * btn = lv_button_create(scene_bar);
        lv_obj_set_height(btn, UI_TOUCH_MIN);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_style_radius(btn, UI_RADIUS - 4, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_PRESSED);

        lv_obj_t * label = ui_label_create(btn, scenes[i].name, UI_FONT_SM, UI_COLOR_TEXT);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, scene_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
    }

    /*No scenes, no bar: the tiles take the room instead.*/
    lv_obj_set_hidden(scene_card, count == 0);
    lv_obj_set_grid_dsc_array(root, grid_cols, count > 0 ? grid_rows : grid_rows_no_scene);
}

void page_smart_home_set_link(page_devices_link_t link, const char * detail)
{
    lv_color_t   color    = UI_COLOR_BAD;
    const char * fallback = "Broker offline";

    switch(link) {
        case PAGE_DEVICES_LINK_CONNECTING:
            color    = UI_COLOR_WARN;
            fallback = "Connecting...";
            break;
        case PAGE_DEVICES_LINK_ONLINE:
            color    = UI_COLOR_GOOD;
            fallback = "Connected";
            break;
        case PAGE_DEVICES_LINK_OFFLINE:
        default:
            break;
    }

    lv_obj_set_style_text_color(link_icon, color, LV_PART_MAIN);
    lv_label_set_text(link_label, detail ? detail : fallback);
}

void page_smart_home_set_notice(const char * text)
{
    lv_strlcpy(notice_text, text ? text : "", sizeof(notice_text));
    notice_apply();
}

void page_smart_home_set_command_cb(page_devices_command_cb_t cb)
{
    command_cb = cb;
}

void page_smart_home_set_scene_cb(page_devices_scene_cb_t cb)
{
    scene_cb = cb;
}

/*=====================
 * Shared with the tile and panel files
 *====================*/

const device_t * sh_device(uint32_t index)
{
    return index < device_count ? &devices[index] : NULL;
}

void sh_command(uint32_t index, device_attr_t attr, float value)
{
    if(command_cb && index < device_count) command_cb(index, attr, value);
}

lv_obj_t * sh_box_create(lv_obj_t * parent)
{
    lv_obj_t * box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);
    return box;
}

void sh_number_format(char * buf, size_t size, float value, float step)
{
    bool negative = value < 0;
    if(negative) value = -value;

    if(step >= 1.0f) {
        lv_snprintf(buf, size, "%s%d", negative ? "-" : "", (int)(value + 0.5f));
        return;
    }

    int tenths = (int)(value * 10.0f + 0.5f);
    lv_snprintf(buf, size, "%s%d.%d", negative ? "-" : "", tenths / 10, tenths % 10);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * create(lv_obj_t * parent)
{
    root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(root, UI_GAP, LV_PART_MAIN);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_grid_dsc_array(root, grid_cols, grid_rows_no_scene);

    header_create(root);
    body_create(root);
    scenes_create(root);

    /*Empty until ui_devices_feed loads a configuration.*/
    page_smart_home_set_link(PAGE_DEVICES_LINK_OFFLINE, NULL);
    page_smart_home_set_scenes(NULL, 0);
    page_smart_home_set_devices(NULL, 0);

    return root;
}

static void header_create(lv_obj_t * parent)
{
    lv_obj_t * card = ui_card_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(card, UI_GAP / 2, LV_PART_MAIN);

    link_icon  = ui_label_create(card, LV_SYMBOL_WIFI, UI_FONT_MD, UI_COLOR_BAD);
    link_label = ui_label_create(card, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);

    /*Room chips pushed to the right.*/
    room_bar = sh_box_create(card);
    lv_obj_set_flex_grow(room_bar, 1);
    lv_obj_set_style_pad_column(room_bar, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(room_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(room_bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

static void body_create(lv_obj_t * parent)
{
    lv_obj_t * body = sh_box_create(parent);
    lv_obj_set_grid_cell(body, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

    /*Wrapping rows of square tiles; the column count is fixed and the tile
     *size follows the width, see grid_resized().*/
    grid = sh_box_create(body);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_row(grid, UI_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scrollable(grid, true);
    /*sh_box_create() makes boxes click-through, which here would hand a drag
     *that starts between tiles to a parent that cannot scroll. Clickable, the
     *grid takes it and scrolls from anywhere.*/
    lv_obj_set_clickable(grid, true);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(grid, grid_resized, LV_EVENT_SIZE_CHANGED, NULL);

    notice = ui_label_create(body, "", UI_FONT_SM, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(notice, LV_PCT(80));
    lv_obj_set_style_text_align(notice, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(notice, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_center(notice);
}

static void scenes_create(lv_obj_t * parent)
{
    scene_card = ui_card_create(parent);
    lv_obj_set_grid_cell(scene_card, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_set_flex_flow(scene_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scene_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(scene_card, UI_GAP / 2, LV_PART_MAIN);

    scene_bar = sh_box_create(scene_card);
    lv_obj_set_width(scene_bar, LV_PCT(100));
    lv_obj_set_style_pad_column(scene_bar, UI_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(scene_bar, LV_FLEX_FLOW_ROW);
}

/** Collect the rooms, in order of first appearance, and rebuild the chips. */
static void rooms_rebuild(void)
{
    room_count = 0;

    for(uint32_t i = 0; i < device_count && room_count < ROOM_MAX; i++) {
        const char * room = devices[i].room;
        if(room[0] == '\0') continue;

        bool seen = false;
        for(uint32_t r = 0; r < room_count && !seen; r++) seen = strcmp(room_names[r], room) == 0;
        if(!seen) lv_strlcpy(room_names[room_count++], room, DEVICE_NAME_LEN);
    }

    lv_obj_clean(room_bar);

    /*One room or none: there is nothing to filter.*/
    if(room_count < 2) {
        room_select(0);
        return;
    }

    for(uint32_t i = 0; i <= room_count; i++) {
        lv_obj_t * chip = lv_button_create(room_bar);
        lv_obj_set_height(chip, ROOM_CHIP_HEIGHT);
        lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(chip, UI_GAP, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(chip, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(chip, UI_COLOR_CARD_ALT, LV_PART_MAIN);
        lv_obj_set_style_bg_color(chip, UI_COLOR_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(chip, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_color(chip, UI_COLOR_TEXT, LV_PART_MAIN | LV_STATE_CHECKED);

        lv_obj_t * label = lv_label_create(chip);
        lv_label_set_text(label, i == 0 ? "All" : room_names[i - 1]);
        lv_obj_set_style_text_font(label, UI_FONT_XS, LV_PART_MAIN);
        lv_obj_center(label);

        lv_obj_add_event_cb(chip, room_clicked, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
    }

    room_select(room_current <= room_count ? room_current : 0);
}

static void room_clicked(lv_event_t * e)
{
    room_select((uint32_t)(lv_uintptr_t)lv_event_get_user_data(e));
}

static void room_select(uint32_t index)
{
    room_current = index;

    for(uint32_t i = 0; i < lv_obj_get_child_count(room_bar); i++) {
        lv_obj_t * chip = lv_obj_get_child(room_bar, (int32_t)i);
        if(i == index) lv_obj_add_state(chip, LV_STATE_CHECKED);
        else           lv_obj_remove_state(chip, LV_STATE_CHECKED);
    }

    for(uint32_t i = 0; i < device_count; i++) {
        bool visible = index == 0 || strcmp(devices[i].room, room_names[index - 1]) == 0;
        lv_obj_set_hidden(tiles[i], !visible);
    }

    lv_obj_scroll_to_y(grid, 0, LV_ANIM_OFF);
}

static void grid_resized(lv_event_t * e)
{
    LV_UNUSED(e);

    int32_t width = lv_obj_get_content_width(grid);
    int32_t size  = (width - (TILE_COLUMNS - 1) * UI_GAP) / TILE_COLUMNS;
    if(size <= 0 || size == tile_size) return;

    tile_size = size;
    for(uint32_t i = 0; i < device_count; i++) lv_obj_set_size(tiles[i], size, size);
}

/** Show the notice in place of the grid when there is one, or nothing to show. */
static void notice_apply(void)
{
    const char * text = notice_text[0] ? notice_text
                        : (device_count == 0 ? "No devices configured" : NULL);

    lv_obj_set_hidden(notice, text == NULL);
    lv_obj_set_hidden(grid, text != NULL);
    if(text) lv_label_set_text(notice, text);
}

static void scene_clicked(lv_event_t * e)
{
    if(scene_cb) scene_cb((uint32_t)(lv_uintptr_t)lv_event_get_user_data(e));
}
