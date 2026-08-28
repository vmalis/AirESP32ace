// AirESP32ace
// Created by VM, August 2026

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#ifdef AIRSPACE_DESKTOP_RENDERER
#include "desktop_platform.h"
#include "desktop_renderer.h"
#else
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include <png.h>
#endif
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "aircraft_icons.h"
#include "aircraft_type_registry.h"
#include "api_logos.h"
#include "app_config.h"
#include "clock_hand_assets.h"
#include "data.h"
#include "detail_metric_icons.h"
#include "gate_arrow_assets.h"
#include "locations.h"
#include "menu_icons.h"
#include "muller_clock_fonts.h"
#include "nasalization_font.h"
#include "network.h"
#include "power_status.h"
#include "remote.h"

#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define SCREEN_SIZE 466
#define CX (SCREEN_SIZE / 2)
#define CY (SCREEN_SIZE / 2)
#define MAX_LINE_PAIRS 240
#define DEG_TO_RAD 0.01745329252f
#define UI_TIMER_MS 50
#define FULL_REFRESH_TICKS 12000
#define HEALTH_LOG_TICKS (10000 / UI_TIMER_MS)
#define LVGL_TASK_STACK_SIZE (16 * 1024)
#define RADAR_SWEEP_ENABLED 1
#define RADAR_SWEEP_TIMER_MS 15
#define RADAR_SWEEP_DEG_PER_SECOND 122.0f
#define RADAR_SWEEP_SECTOR_DEG 9.0f
#define RADAR_SWEEP_DIRTY_BANDS 6
#define RADAR_AIRCRAFT_TIMER_MS 50
#define RADAR_AIRCRAFT_SPRITE_SIZE 50
#define RADAR_AIRCRAFT_SPRITE_BYTES (RADAR_AIRCRAFT_SPRITE_SIZE * RADAR_AIRCRAFT_SPRITE_SIZE)
#define RADAR_AIRCRAFT_CORRECTION_SECONDS 1.25f
#define RADAR_GREEN 0x007a1f
#define RADAR_AIRCRAFT_GREEN 0x009428
#define RADAR_AIRCRAFT_DIM 0x07531b
#define RADAR_GREEN_LABEL 0x24bd38
#define RADAR_GREEN_DIM 0x051f0a
#define RADAR_GREEN_DARK 0x010a03
#define RADAR_SWEEP_GREEN 0x003a10
#define RADAR_SWEEP_RADIUS_PX 234.0f
#define RADAR_RANGE_DEFAULT_MI 20.0f
#define STATUTE_MI_TO_NM 0.868976f
#define RADAR_RADIUS_PX 226.0f
#define RADAR_TARGET_HIT_RADIUS 34
#define RADAR_AIRCRAFT_PROJECTION_MAX_SECONDS 120
#define RADAR_RANGE_BUTTON_COUNT 3
#define RADAR_LOCATION_BUTTON_COUNT 8
#define RADAR_MENU_PAGE_COUNT 4
#define RADAR_ACCENT_BUTTON_COUNT APP_ACCENT_COUNT
#define MAP_TILE_HEIGHT 64
#define MAP_TILE_COUNT ((SCREEN_SIZE + MAP_TILE_HEIGHT - 1) / MAP_TILE_HEIGHT)
#define MAP_TILE_STRIDE_BYTES 1024
#define MAP_KIND_ROUTE 1
#define MAP_KIND_RADAR_BASE 16
#define MAP_KIND_WEATHER_BASE 32
#define WEATHER_RADAR_PLAYBACK_FRAME_MS 700
#define WEATHER_RADAR_CENTER_HIT_RADIUS 62
#define CLOCK_CITY_COUNT APP_CLOCK_CITY_COUNT
#define CLOCK_CROWN_TOGGLE_HIT_RADIUS 76
#define GMT_SNAPSHOT_BYTES (SCREEN_SIZE * SCREEN_SIZE * 2)
#define SPACE_SOLAR_SNAPSHOT_BYTES (SCREEN_SIZE * SCREEN_SIZE * 2)
#define ECLIPSE_TERMINATOR_ANIMATION_US 6500000LL
#define ECLIPSE_TERMINATOR_MAX_GROUPS 4
#define ECLIPSE_TERMINATOR_SAMPLE_COUNT 120
#define ECLIPSE_TERMINATOR_POINTS_PER_GROUP 128
#define ECLIPSE_TERMINATOR_FRAME_MS 40
#define ECLIPSE_TERMINATOR_DIRTY_AREAS 8
#define ECLIPSE_SHADE_STEPS 64
#define ECLIPSE_SHADE_DIRTY_ROWS 6
#define POWER_MENU_HOLD_US 2500000LL
#define POWER_MENU_TIMEOUT_US 10000000LL
#define DISPLAY_SLEEP_PWR_POLL_US 500000ULL
#define SPLASH_STARTUP_FRAME_MS 10
#define SPLASH_SHUTDOWN_FRAME_MS 25
#define SPLASH_STARTUP_HOLD_US 260000LL
#define SPLASH_STARTUP_RAMP_US 1150000LL
#define SPLASH_SHUTDOWN_FADE_US 1000000LL
#define POWER_OFF_MESSAGE_US 1600000LL
#define DOCK_CHORD_WINDOW_US 450000LL
#define DOCK_CHORD_STABLE_US 120000LL
#define DOCK_FEEDBACK_US 1250000LL
#define INPUT_STARTUP_GUARD_US 1200000LL
#define SPACE_ISS_FLAGS_LONG_PRESS_US 650000LL
#define SCREEN_TRANSITION_FRAME_MS 10
#define SCREEN_FADE_OUT_US 160000LL
#define SCREEN_FADE_IN_US 160000LL
#define DOCK_SCREEN_FADE_OUT_US 240000LL
#define DOCK_SCREEN_FADE_IN_US 240000LL
#define GMT_INNER_FADE_OUT_US 110000LL
#define GMT_INNER_FADE_IN_US 110000LL
#define SCREEN_TRANSITION_FLOOR_PERCENT 1.5f
#define GMT_INNER_FADE_DIAMETER 312
#define ISS_FLAGS_FINAL_OPACITY ((lv_opa_t)217)
#define SPACE_MAP_OVERLAY_FADE_MS 170
#define SPACE_CITY_MARKER_HIT_RADIUS (SCREEN_SIZE / 4)

static const char *TAG = "airspace";

typedef enum {
    SCREEN_RADAR = 0,
    SCREEN_TRACKED_FLIGHT,
    SCREEN_WEATHER,
    SCREEN_GMT,
    SCREEN_SPACE,
    SCREEN_LAUNCH,
    SCREEN_COUNT,
} app_screen_t;

typedef enum {
    TRACKED_FLIGHT_GLOBE = 0,
    TRACKED_FLIGHT_FRAME,
    TRACKED_FLIGHT_DETAIL,
} tracked_flight_view_t;

typedef enum {
    LAUNCH_VIEW_MAIN = 0,
    LAUNCH_VIEW_MAP,
    LAUNCH_VIEW_QR,
} launch_view_t;

typedef enum {
    SPACE_VIEW_SOLAR = 0,
    SPACE_VIEW_ECLIPSE,
    SPACE_VIEW_MOON,
    SPACE_VIEW_ISS,
} space_view_t;

typedef struct {
    bool press_edge;
    bool release_edge;
    bool short_press;
    bool long_press;
} axp_power_key_events_t;

typedef struct {
    app_screen_t screen;
    int duration_seconds;
    int variant;
} dock_sequence_entry_t;

enum {
    DOCK_VARIANT_DEFAULT = 0,
    DOCK_VARIANT_SPACE_SOLAR,
    DOCK_VARIANT_SPACE_ECLIPSE,
    DOCK_VARIANT_SPACE_MOON,
    DOCK_VARIANT_SPACE_ISS,
    DOCK_VARIANT_LAUNCH_NASA,
    DOCK_VARIANT_LAUNCH_ROSCOSMOS,
    DOCK_VARIANT_LAUNCH_SPACEX,
    DOCK_VARIANT_TRACKER_FRAME,
    DOCK_VARIANT_TRACKER_DETAIL,
    DOCK_VARIANT_WEATHER_FORECAST,
    DOCK_VARIANT_WEATHER_RADAR,
    DOCK_VARIANT_CLOCK_WORLD,
    DOCK_VARIANT_LAUNCH_NASA_MAP,
    DOCK_VARIANT_LAUNCH_ROSCOSMOS_MAP,
    DOCK_VARIANT_LAUNCH_SPACEX_MAP,
};

typedef enum {
    TRACKED_METRIC_STEADY = 0,
    TRACKED_METRIC_UP,
    TRACKED_METRIC_DOWN,
} tracked_metric_trend_t;

typedef enum {
    SPLASH_TRANSITION_IDLE = 0,
    SPLASH_TRANSITION_STARTUP_HOLD,
    SPLASH_TRANSITION_STARTUP_RAMP,
    SPLASH_TRANSITION_SHUTDOWN_FADE,
} splash_transition_t;

typedef enum {
    SCREEN_TRANSITION_IDLE = 0,
    SCREEN_TRANSITION_FADE_OUT,
    SCREEN_TRANSITION_RENDER_HIDDEN,
    SCREEN_TRANSITION_FADE_IN,
} screen_transition_t;

typedef enum {
    SCREEN_TRANSITION_FULL_DISPLAY = 0,
    SCREEN_TRANSITION_GMT_INNER,
} screen_transition_visual_t;

typedef struct {
    bool valid;
    int x;
    int y;
    int w;
    int h;
    const char *key;
} clock_city_button_t;

static app_screen_t current_screen = SCREEN_RADAR;
static tracked_flight_view_t tracked_flight_view = TRACKED_FLIGHT_GLOBE;
static bool tracked_post_arrival_expiry_rendered;
static bool render_requested = true;
static bool aircraft_detail_active;
static bool boot_was_pressed;
static int64_t boot_press_started_us;
static bool touch_was_pressed;
static int64_t touch_press_started_us;
static bool top_button_held;
static int64_t top_button_pressed_us;
static bool top_release_guard;
static bool boot_release_guard;
static bool touch_release_guard;
static bool input_startup_guard = true;
static int64_t input_startup_guard_until_us;
static bool dock_chord_locked;
static bool power_menu_visible;
static int64_t power_menu_deadline_us;
static bool power_menu_opening_release_guard;
static bool display_sleeping;
static int display_sleep_brightness = 100;
static bool power_off_pending;
static int64_t power_off_deadline_us;
static bool startup_splash_visible;
static bool splash_cleanup_pending;
static splash_transition_t splash_transition;
static int64_t splash_transition_started_us;
static int splash_restore_brightness = 100;
static int splash_shutdown_start_brightness = 100;
static lv_timer_t *splash_brightness_timer;
static screen_transition_t screen_transition;
static screen_transition_visual_t screen_transition_visual;
static int64_t screen_transition_started_us;
static int screen_transition_target_brightness = 100;
static bool screen_transition_dock_timing;
static lv_timer_t *screen_transition_timer;
static lv_obj_t *screen_transition_inner_mask;
static uint8_t *startup_splash_buffer;
static lv_image_dsc_t startup_splash_image;
static lv_obj_t *startup_splash_object;
static bool dock_mode_enabled;
static bool dock_feedback_visible;
static bool dock_feedback_on;
static int64_t dock_feedback_deadline_us;
static int dock_sequence_index;
static int64_t dock_deadline_us;
static bool dock_waiting_for_screen_render;
static int status_refresh_ticks;
static int health_log_ticks;
#if RADAR_SWEEP_ENABLED
static int64_t radar_sweep_epoch_us;
static int64_t radar_sweep_last_tick_us;
static int64_t radar_sweep_gap_max_us;
static uint64_t radar_sweep_gap_total_us;
static uint32_t radar_sweep_gap_count;
static uint32_t radar_aircraft_move_count;
#endif
static int data_poll_ticks;
static int data_fetched_unix_seen = -1;
static uint32_t api_health_revision_seen = UINT32_MAX;
static int gmt_minute_seen = -1;
static lv_point_t touch_down_point;
static lv_point_t touch_last_point;
static app_aircraft_t selected_aircraft;
static bool weather_radar_visible;
static bool weather_forecast_visible;
static bool weather_radar_loading;
static bool weather_radar_playing;
static bool dock_weather_radar_autoplay_pending;
static int weather_radar_playback_index = -1;
static int64_t weather_radar_playback_step_us;
static char weather_selected_key[8];
static bool weather_entry_animation_pending;
static bool clock_digital_mode;
static bool clock_crown_visible;
static lv_obj_t *gmt_live_crown;
static char clock_selected_key[8];
static char clock_secondary_key[8];
static bool clock_bezel_uses_secondary;
static clock_city_button_t clock_city_buttons[CLOCK_CITY_COUNT];
static uint8_t *gmt_snapshot_buffer;
static lv_image_dsc_t gmt_snapshot_image;
static int gmt_snapshot_minute = -1;
static bool gmt_snapshot_digital_mode;
static bool gmt_snapshot_base_only;
static bool gmt_snapshot_dynamic_hands;
static bool gmt_snapshot_bezel_uses_secondary;
static char gmt_snapshot_location_key[8];
static char gmt_snapshot_secondary_key[8];
static uint8_t *gmt_transition_snapshot_buffer;
static lv_image_dsc_t gmt_transition_snapshot_image;
static int gmt_transition_snapshot_minute = -1;
static bool gmt_transition_snapshot_digital_mode;
static bool gmt_transition_snapshot_base_only;
static bool gmt_transition_snapshot_dynamic_hands;
static bool gmt_transition_snapshot_bezel_uses_secondary;
static char gmt_transition_snapshot_location_key[8];
static char gmt_transition_snapshot_secondary_key[8];
static bool gmt_transition_snapshot_ready;
static lv_obj_t *gmt_live_hands_layer;
static int64_t gmt_live_hands_second_seen = -1;
static bool gmt_secondary_transition_active;
static bool gmt_secondary_transition_finish_pending;
static lv_obj_t *gmt_secondary_roll_old;
static lv_obj_t *gmt_secondary_roll_new;
static float gmt_secondary_hand_start_degrees;
static float gmt_secondary_hand_delta_degrees;
static int gmt_date_day_seen = -1;
static char gmt_date_location_key_seen[8];
static lv_obj_t *gmt_date_animation_window;
static int gmt_date_animation_day;
static space_view_t space_view = SPACE_VIEW_SOLAR;
static int64_t space_iss_flags_fade_started_us;
#define space_iss_visible \
    (space_view == SPACE_VIEW_ISS || \
     space_view == SPACE_VIEW_ECLIPSE)
#define space_moon_visible (space_view == SPACE_VIEW_MOON)
#define space_eclipse_visible (space_view == SPACE_VIEW_ECLIPSE)
static bool space_city_marker_visible;
static bool space_eclipse_terminator_active;
static int64_t space_eclipse_terminator_started_us;
static lv_obj_t *space_eclipse_time_label;
static lv_timer_t *space_eclipse_terminator_timer;
static lv_obj_t *space_iss_night_image_object;
static lv_obj_t *space_iss_terminator_image_object;
static lv_obj_t *space_eclipse_motion_image_object;
static int space_eclipse_shade_step = -1;
typedef struct eclipse_path_image eclipse_path_image_t;
static eclipse_path_image_t *space_eclipse_path_asset;
static void update_total_eclipse_path_progress(float progress, bool complete);
typedef struct {
    float x;
    float y;
} eclipse_float_point_t;
typedef struct {
    eclipse_float_point_t points[
        ECLIPSE_TERMINATOR_POINTS_PER_GROUP];
    int point_count;
} eclipse_terminator_group_t;
static eclipse_terminator_group_t
    space_eclipse_terminator_groups[ECLIPSE_TERMINATOR_MAX_GROUPS];
static lv_area_t space_eclipse_motion_dirty[
    ECLIPSE_TERMINATOR_DIRTY_AREAS];
static int space_eclipse_motion_dirty_count;
static launch_view_t launch_view = LAUNCH_VIEW_MAIN;
static app_remote_launch_agency_t launch_selected_agency =
    APP_REMOTE_LAUNCH_SPACEX;
static int launch_animation_ticks;
static lv_obj_t *launch_countdown_prefix_label;
static lv_obj_t *launch_countdown_label;
static lv_obj_t *launch_map_marker;
static lv_obj_t *launch_map_pad_glow;
static lv_obj_t *launch_inner_ring;
static uint8_t *launch_gradient_ring_buffer;
static lv_image_dsc_t launch_gradient_ring_image;
static uint8_t *launch_outer_gradient_ring_buffer;
static lv_image_dsc_t launch_outer_gradient_ring_image;
static float launch_gradient_radial_profile[193];
static bool launch_gradient_radial_profile_ready;
static app_remote_status_t launch_render_status;
static bool launch_inner_ring_uses_gradient;
static int64_t launch_countdown_second_seen = -1;
static int space_animation_ticks;
static lv_obj_t *space_planets[8];
static lv_obj_t *space_planet_symbols[8];
static lv_obj_t *space_planet_labels[8];
static lv_obj_t *space_earth_glow;
static lv_obj_t *space_iss_marker;
static lv_obj_t *space_map_overlay_layer;
static bool space_map_overlay_transition_active;
static lv_obj_t *space_city_marker;
static lv_obj_t *space_city_marker_glow;
static int space_city_marker_x;
static int space_city_marker_y;
static uint8_t space_planet_annotation_mode;
static bool space_solar_intro_active;
static int64_t space_solar_intro_start_us;
static int64_t space_orbit_hour_seen = -1;
static float space_planet_target_angles[8];
static bool space_planet_targets_valid;
static uint8_t *space_solar_snapshot_buffer;
static lv_image_dsc_t space_solar_snapshot_image;
static uint8_t *space_iss_night_buffer;
static uint8_t *space_iss_terminator_buffer;
static uint8_t *space_eclipse_motion_buffer;
static lv_image_dsc_t space_iss_night_image;
static lv_image_dsc_t space_iss_terminator_image;
static lv_image_dsc_t space_eclipse_motion_image;
static uint8_t *space_moon_phase_buffer;
static uint8_t *space_moon_strip_buffer;
static lv_image_dsc_t space_moon_phase_image;
static lv_image_dsc_t space_moon_strip_images[8];
static lv_image_dsc_t space_asset_images[16];
static uint32_t space_asset_image_revisions[16];
static int space_asset_retry_attempts;
static int64_t space_asset_retry_after_us;

typedef enum {
    GENERATED_SCENE_NONE = 0,
    GENERATED_SCENE_SOLAR,
    GENERATED_SCENE_ISS,
    GENERATED_SCENE_MOON,
    GENERATED_SCENE_LAUNCH_MAIN,
    GENERATED_SCENE_LAUNCH_MAP,
    GENERATED_SCENE_LAUNCH_QR,
    GENERATED_SCENE_GMT,
} generated_scene_t;

static generated_scene_t rendered_generated_scene =
    GENERATED_SCENE_NONE;
static void trim_space_asset_cache(
    const char *const *keep_names,
    size_t keep_count);
static lv_image_dsc_t airline_logo_image;
static uint8_t *tracked_logo_glow_buffer;
static lv_image_dsc_t tracked_logo_glow_image;
static lv_image_dsc_t tracked_route_flag_images[2];
static lv_image_dsc_t flipped_detail_aircraft_image;
static uint8_t *flipped_detail_aircraft_buffer;
static size_t flipped_detail_aircraft_capacity;
static const uint8_t *flipped_detail_aircraft_source;
static bool detail_entry_animation_pending;
static uint32_t detail_logo_revision_seen;
static char detail_route_signature_seen[32];
static int tracked_logo_poll_ticks;
static uint32_t tracked_logo_revision_seen;
static bool tracked_detail_entry_animation_pending;
static bool tracked_detail_aux_visible = true;
static bool tracked_panels_visible = true;
static bool tracked_panels_entry_animation_pending = true;
static lv_obj_t *tracked_panel_primary;
static lv_obj_t *tracked_panel_secondary;
static lv_obj_t *tracked_time_pages[2];
static lv_obj_t *tracked_gate_value_pages[2][2];
static int tracked_time_page_base_y;
static uint8_t tracked_time_page_index;
static int64_t tracked_time_page_epoch_us;
static int tracked_time_minute_seen = -1;
static lv_obj_t *tracked_preflight_days_label;
static lv_obj_t *tracked_preflight_days_units_label;
static lv_obj_t *tracked_preflight_countdown_label;
static lv_obj_t *tracked_preflight_countdown_units_label;
static lv_obj_t *tracked_preflight_gate_sign;
static int64_t tracked_preflight_countdown_minute_seen = -1;
static char tracked_time_flight_seen[16];
static char tracked_time_journey_seen[12];
static lv_obj_t *tracked_aircraft_holder;
static lv_obj_t *tracked_aircraft_image;
static bool tracked_aircraft_frame_view;
static float tracked_aircraft_progress_base;
static float tracked_aircraft_progress_per_second;
static int64_t tracked_aircraft_progress_epoch_us;

static void dock_mark_visible_state_changed(void);
static int dock_visible_duration_seconds(void);
static bool screen_transition_active(void);
static void begin_screen_transition(void);
static void begin_gmt_inner_transition(void);
static bool prepare_gmt_transition_snapshot(void);
static bool begin_space_map_overlay_transition(void);
static void cancel_screen_transition(bool restore_brightness);
static void refresh_radar_menu_overlay(void);
static void start_eclipse_terminator_animation(void);
static void stop_eclipse_terminator_animation(void);
static void wake_display(void);
static int eclipse_clamp_screen(int value);
static uint8_t tracked_route_point_count;
static int16_t tracked_route_points[
    APP_REMOTE_TRACKED_ROUTE_MAX_POINTS
][2];
static uint32_t tracked_metric_state_pushes_seen = UINT32_MAX;
static char tracked_metric_flight_seen[16];
static int tracked_metric_altitude_seen;
static int tracked_metric_speed_seen;
static tracked_metric_trend_t tracked_altitude_trend;
static tracked_metric_trend_t tracked_speed_trend;
static lv_grad_dsc_t tracked_divider_gradient;
static bool tracked_divider_gradient_initialized;
static lv_grad_dsc_t tracked_left_panel_gradient;
static lv_grad_dsc_t tracked_right_panel_gradient;
static bool tracked_side_panel_gradients_initialized;

static lv_obj_t *screen_root;
#if RADAR_SWEEP_ENABLED
static lv_obj_t *radar_sweep_obj;
static float radar_sweep_angle_deg;
#endif
static uint32_t remote_revision_seen;
static int remote_style_seen = -1;
static uint32_t remote_map_revision_seen;
static uint32_t remote_radar_map_revision_seen;
static uint32_t padded_map_revision_seen;
static uint8_t padded_map_kind_seen;
static uint8_t *padded_map_buffer;
static app_data_status_t *weather_data_snapshot;
static lv_image_dsc_t weather_city_icon_image;
static uint32_t weather_composite_overlay_revision_seen = UINT32_MAX;
static uint32_t weather_composite_map_revision_seen = UINT32_MAX;
static int weather_composite_map_index_seen = -1;
static int weather_composite_frame_index_seen = -1;
static lv_image_dsc_t pushed_map_tiles[MAP_TILE_COUNT];
static lv_obj_t *pushed_map_tile_objects[MAP_TILE_COUNT];
static lv_obj_t *weather_temperature_label;
static lv_obj_t *weather_summary_label;
static lv_obj_t *weather_precipitation_label;
static lv_obj_t *weather_humidity_label;
static lv_obj_t *weather_wind_label;
static lv_obj_t *weather_pressure_label;
static lv_obj_t *weather_aqi_label;
static lv_obj_t *weather_uv_label;
static lv_obj_t *weather_scale_markers[4];
static lv_obj_t *weather_forecast_day_labels[APP_WEATHER_FORECAST_DAYS];
static lv_obj_t *weather_forecast_high_labels[APP_WEATHER_FORECAST_DAYS];
static lv_obj_t *weather_forecast_low_labels[APP_WEATHER_FORECAST_DAYS];
static lv_obj_t *weather_forecast_precip_labels[APP_WEATHER_FORECAST_DAYS];
static lv_obj_t *weather_radar_temperature_label;
static lv_obj_t *weather_radar_time_label;
static lv_obj_t *weather_radar_progress_fill;
static lv_obj_t *weather_radar_progress_marker;
static lv_point_precise_t line_pairs[MAX_LINE_PAIRS][2];
static int line_pair_count;

static app_data_status_t *ui_data_status(void)
{
    if (weather_data_snapshot == NULL) {
        weather_data_snapshot = heap_caps_calloc(
            1,
            sizeof(*weather_data_snapshot),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (weather_data_snapshot != NULL) {
        app_data_get_status(weather_data_snapshot);
    }
    return weather_data_snapshot;
}

#define MAX_POLYLINE_POINTS 32
#define TRACKED_ROUTE_RENDER_POINT_COUNT 72

typedef struct {
    lv_point_precise_t pts[MAX_POLYLINE_POINTS];
} polyline_t;

static polyline_t polylines[48];
static int polyline_count;
static lv_point_precise_t tracked_route_render_points[
    TRACKED_ROUTE_RENDER_POINT_COUNT
];
static float tracked_route_render_samples[
    TRACKED_ROUTE_RENDER_POINT_COUNT
][2];
static uint8_t *tracked_route_mask_buffer;
static lv_image_dsc_t tracked_route_mask_image;

static void detail_draw_metric(const lv_image_dsc_t *icon,
                               int center_x,
                               int top_y,
                               const char *value,
                               int rotation_degrees);
static void detail_draw_compass(int center_x,
                                int top_y,
                                const char *value,
                                int heading_degrees,
                                bool heading_valid);
static const char *detail_compass_direction(const app_aircraft_t *aircraft);
static bool clock_select_next_displayed_city(void);
static bool clock_begin_secondary_transition(void);

typedef struct {
    bool allocated;
    bool visible;
    bool sprite_dirty;
    bool commercial;
    int x;
    int y;
    int w;
    int h;
    int heading_deg;
    int aircraft_index;
    float display_x;
    float display_y;
    float correction_x;
    float correction_y;
    int64_t correction_epoch_us;
    char icao24[8];
    app_aircraft_t aircraft;
    const lv_image_dsc_t *icon;
    lv_image_dsc_t sprite;
    lv_obj_t *img;
    lv_obj_t *label;
} radar_aircraft_view_t;

static radar_aircraft_view_t radar_aircraft_views[APP_MAX_AIRCRAFT];
static int radar_aircraft_status_unix_seen = -1;
static int radar_aircraft_sprite_cursor;
static uint8_t *radar_aircraft_sprite_pool;
static lv_obj_t *radar_aircraft_layer;

typedef struct {
    bool valid;
    int x;
    int y;
    int w;
    int h;
    float miles;
} radar_range_button_t;

static radar_range_button_t radar_range_buttons[RADAR_RANGE_BUTTON_COUNT];
static int radar_range_button_count;
static radar_range_button_t radar_menu_button;
static radar_range_button_t weather_radar_range_buttons[RADAR_RANGE_BUTTON_COUNT];

typedef struct {
    bool valid;
    int x;
    int y;
    int w;
    int h;
    app_wifi_profile_id_t profile;
} radar_wifi_profile_button_t;

static radar_wifi_profile_button_t radar_wifi_profile_buttons[APP_WIFI_PROFILE_COUNT];
static int radar_wifi_profile_button_count;

typedef struct {
    bool valid;
    int x;
    int y;
    int w;
    int h;
    char key[16];
} radar_location_button_t;

static radar_location_button_t radar_location_buttons[RADAR_LOCATION_BUTTON_COUNT];
static int radar_location_button_count;
static radar_range_button_t radar_labels_button;
static radar_range_button_t radar_wedge_button;
static radar_range_button_t radar_weather_unit_buttons[APP_WEATHER_UNITS_COUNT];
static radar_range_button_t radar_brightness_up_button;
static radar_range_button_t radar_brightness_down_button;

typedef struct {
    bool valid;
    int x;
    int y;
    int w;
    int h;
    int page;
} radar_page_button_t;

typedef struct {
    bool valid;
    int x;
    int y;
    int w;
    int h;
    app_accent_color_t color;
} radar_accent_button_t;

typedef struct {
    app_accent_color_t color;
    const char *label;
    uint32_t main;
    uint32_t aircraft;
    uint32_t secondary_aircraft;
    uint32_t dim;
    uint32_t text;
    uint32_t dark;
    uint32_t sweep;
} radar_palette_t;

static radar_palette_t radar_palettes[] = {
    {APP_ACCENT_GREEN, "GREEN", RADAR_GREEN, RADAR_AIRCRAFT_GREEN, 0x0a7625, RADAR_AIRCRAFT_DIM, RADAR_GREEN_LABEL, RADAR_GREEN_DARK, RADAR_SWEEP_GREEN},
    {APP_ACCENT_CYAN, "CYAN", 0x00a3bf, 0x00c7df, 0x087582, 0x07525b, 0x35d8eb, 0x021016, 0x004655},
    {APP_ACCENT_ORANGE, "ORANGE", 0xd47a17, 0xff901a, 0x925016, 0x61330b, 0xffb04e, 0x170902, 0x5b2604},
    {APP_ACCENT_RED, "RED", 0xbc252f, 0xf0444c, 0x84202a, 0x5a1118, 0xff686e, 0x160305, 0x52080c},
};

static radar_page_button_t radar_page_buttons[RADAR_MENU_PAGE_COUNT];
static radar_accent_button_t radar_accent_buttons[RADAR_ACCENT_BUTTON_COUNT];
static int radar_accent_button_count;
static const radar_palette_t *radar_palette_active = &radar_palettes[0];
static int radar_menu_page;
static bool radar_wedge_visible = true;
static float radar_range_miles = RADAR_RANGE_DEFAULT_MI;
static bool radar_range_menu_open;
static lv_obj_t *radar_menu_overlay;
static lv_obj_t *radar_brightness_arc;
static lv_obj_t *radar_brightness_value_label;
static lv_timer_t *radar_brightness_animation_timer;
static bool radar_brightness_drag_candidate;
static bool radar_brightness_drag_active;
static float radar_brightness_drag_anchor_percent = 100.0f;
static float radar_brightness_visual_percent = 100.0f;
static float radar_brightness_target_percent = 100.0f;
static int radar_brightness_last_raw = -1;

static const radar_palette_t *palette_for_accent(app_accent_color_t color)
{
    for (size_t i = 0; i < sizeof(radar_palettes) / sizeof(radar_palettes[0]); i++) {
        if (radar_palettes[i].color == color) {
            return &radar_palettes[i];
        }
    }
    return &radar_palettes[0];
}

static uint32_t blend_rgb(
    uint32_t source,
    uint32_t target,
    uint8_t target_weight)
{
    const uint32_t source_weight = 255U - target_weight;
    const uint32_t red =
        ((((source >> 16) & 0xFFU) * source_weight) +
         (((target >> 16) & 0xFFU) * target_weight)) / 255U;
    const uint32_t green =
        ((((source >> 8) & 0xFFU) * source_weight) +
         (((target >> 8) & 0xFFU) * target_weight)) / 255U;
    const uint32_t blue =
        (((source & 0xFFU) * source_weight) +
         ((target & 0xFFU) * target_weight)) / 255U;
    return (red << 16) | (green << 8) | blue;
}

static void configure_custom_palette(
    app_accent_color_t color,
    uint32_t main_color)
{
    for (size_t index = 0;
         index < sizeof(radar_palettes) / sizeof(radar_palettes[0]);
         index++) {
        radar_palette_t *palette = &radar_palettes[index];
        if (palette->color != color) {
            continue;
        }
        const uint32_t main = main_color & 0xFFFFFFU;
        palette->main = main;
        palette->aircraft = blend_rgb(main, 0xFFFFFFU, 48);
        palette->secondary_aircraft = blend_rgb(main, 0x000000U, 92);
        palette->dim = blend_rgb(main, 0x000000U, 158);
        palette->text = blend_rgb(main, 0xFFFFFFU, 94);
        palette->dark = blend_rgb(main, 0x000000U, 226);
        palette->sweep = blend_rgb(main, 0x000000U, 148);
        return;
    }
}

static void sync_radar_display_config(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    configure_custom_palette(APP_ACCENT_CYAN, cfg.accent_cyan_rgb);
    configure_custom_palette(APP_ACCENT_ORANGE, cfg.accent_orange_rgb);
    configure_custom_palette(APP_ACCENT_RED, cfg.accent_red_rgb);
    radar_palette_active = palette_for_accent(cfg.accent_color);
    radar_wedge_visible = cfg.radar_wedge_enabled;
}

/* The existing C/F choice is the device-wide display-unit preference.  Data
 * stays in its canonical metric/aviation form; only the final label formatting
 * changes, so switching units cannot affect tracking calculations or caches. */
static bool display_prefers_imperial(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    return cfg.weather_units == APP_WEATHER_UNITS_FAHRENHEIT;
}

static void format_display_altitude_m(char *output,
                                      size_t output_size,
                                      int altitude_m,
                                      bool imperial)
{
    if (altitude_m <= 0) {
        snprintf(output, output_size, "-");
        return;
    }
    if (imperial) {
        snprintf(output,
                 output_size,
                 "%d ft",
                 (int)lroundf((float)altitude_m / 0.3048f));
    } else {
        snprintf(output, output_size, "%d m", altitude_m);
    }
}

static void format_display_speed_kph(char *output,
                                     size_t output_size,
                                     int speed_kph,
                                     bool imperial)
{
    if (speed_kph <= 0) {
        snprintf(output, output_size, "-");
        return;
    }
    if (imperial) {
        snprintf(output,
                 output_size,
                 "%d mph",
                 (int)lroundf((float)speed_kph * 0.621371f));
    } else {
        snprintf(output, output_size, "%d km/h", speed_kph);
    }
}

static void format_display_distance_km(char *output,
                                       size_t output_size,
                                       int distance_km,
                                       bool imperial)
{
    if (distance_km < 0) {
        snprintf(output, output_size, "-");
        return;
    }
    if (imperial) {
        snprintf(output,
                 output_size,
                 "%d mi",
                 (int)lroundf((float)distance_km * 0.621371f));
    } else {
        snprintf(output, output_size, "%d km", distance_km);
    }
}

static void configure_boot_button(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

#ifndef AIRSPACE_DESKTOP_RENDERER
static esp_lcd_panel_handle_t app_display_panel;
static esp_lcd_panel_io_handle_t app_display_panel_io;
static bool panel_boot_brightness_guard = true;

esp_err_t __real_esp_lcd_panel_io_tx_param(
    esp_lcd_panel_io_handle_t io,
    int lcd_cmd,
    const void *param,
    size_t param_size);

esp_err_t __wrap_esp_lcd_panel_io_tx_param(
    esp_lcd_panel_io_handle_t io,
    int lcd_cmd,
    const void *param,
    size_t param_size)
{
    const uint32_t encoded_cmd = (uint32_t)lcd_cmd;
    const bool is_brightness_command =
        encoded_cmd == 0x51U ||
        ((encoded_cmd >> 8U) & 0xffU) == 0x51U;
    if (panel_boot_brightness_guard &&
        io == app_display_panel_io &&
        is_brightness_command &&
        param_size == 1U) {
        const uint8_t panel_off_brightness = 0;
        return __real_esp_lcd_panel_io_tx_param(
            io,
            lcd_cmd,
            &panel_off_brightness,
            sizeof(panel_off_brightness));
    }
    return __real_esp_lcd_panel_io_tx_param(
        io,
        lcd_cmd,
        param,
        param_size);
}

esp_err_t __real_esp_lcd_new_panel_co5300(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel);

esp_err_t __wrap_esp_lcd_new_panel_co5300(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel)
{
    app_display_panel_io = io;
    const esp_err_t result =
        __real_esp_lcd_new_panel_co5300(
            io,
            panel_dev_config,
            ret_panel);
    if (result == ESP_OK && ret_panel != NULL) {
        app_display_panel = *ret_panel;
    }
    return result;
}

static esp_err_t app_display_panel_on_off(bool on)
{
    if (app_display_panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_disp_on_off(app_display_panel, on);
}

static esp_err_t app_display_brightness_raw_set(uint8_t brightness)
{
    if (app_display_panel_io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t lcd_cmd = 0x51U;
    lcd_cmd <<= 8U;
    lcd_cmd |= 0x02U << 24U;
    return esp_lcd_panel_io_tx_param(
        app_display_panel_io,
        (int)lcd_cmd,
        &brightness,
        sizeof(brightness));
}

#define AXP2101_I2C_ADDRESS 0x34
#define AXP2101_STATUS_1 0x00
#define AXP2101_STATUS_2 0x01
#define AXP2101_CHIP_ID_REGISTER 0x03
#define AXP2101_CHIP_ID 0x4a
#define AXP2101_COMMON_CONFIG 0x10
#define AXP2101_POWER_OFF_ENABLE 0x22
#define AXP2101_IRQ_OFF_ON_LEVEL_CONTROL 0x27
#define AXP2101_INTERRUPT_ENABLE_2 0x41
#define AXP2101_INTERRUPT_STATUS_2 0x49
#define AXP2101_POWER_KEY_POSITIVE_MASK (1U << 0)
#define AXP2101_POWER_KEY_NEGATIVE_MASK (1U << 1)
#define AXP2101_POWER_KEY_LONG_MASK (1U << 2)
#define AXP2101_POWER_KEY_SHORT_MASK (1U << 3)
#define AXP2101_POWER_KEY_EVENT_MASK \
    (AXP2101_POWER_KEY_POSITIVE_MASK | \
     AXP2101_POWER_KEY_NEGATIVE_MASK | \
     AXP2101_POWER_KEY_LONG_MASK | \
     AXP2101_POWER_KEY_SHORT_MASK)
#define AXP2101_LONG_PRESS_SHUTDOWN_ENABLE (1U << 1)
#define AXP2101_LONG_PRESS_RESTART_SELECT (1U << 0)
#define AXP2101_IRQ_LEVEL_2_5_SECONDS 3U
#define AXP2101_OFF_LEVEL_8_SECONDS 2U
#define AXP2101_SOFTWARE_POWER_OFF (1U << 0)
#define AXP2101_BATTERY_PERCENT_DATA 0xa4
#define AXP2101_BATTERY_PRESENT_MASK (1U << 3)

static i2c_master_dev_handle_t top_power_button_device;
static bool top_power_button_read(uint8_t reg, uint8_t *value);
static app_power_battery_status_t battery_status_cache = {
    .percent = -1,
};

void app_power_get_battery_status(app_power_battery_status_t *out)
{
    if (out != NULL) {
        *out = battery_status_cache;
    }
}

static bool refresh_battery_status_cache(void)
{
    app_power_battery_status_t next = {
        .available = top_power_button_device != NULL,
        .percent = -1,
    };
    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t percent = 0;
    if (next.available &&
        top_power_button_read(AXP2101_STATUS_1, &status1)) {
        next.present =
            (status1 & AXP2101_BATTERY_PRESENT_MASK) != 0;
        if (top_power_button_read(AXP2101_STATUS_2, &status2)) {
            next.charging = (status2 & 0x60U) == 0x20U;
        }
        if (next.present &&
            top_power_button_read(
                AXP2101_BATTERY_PERCENT_DATA,
                &percent) &&
            percent <= 100) {
            next.percent = percent;
        }
    }
    const bool changed =
        memcmp(&next, &battery_status_cache, sizeof(next)) != 0;
    battery_status_cache = next;
    return changed;
}

static bool top_power_button_read(uint8_t reg, uint8_t *value)
{
    return top_power_button_device != NULL &&
           value != NULL &&
           i2c_master_transmit_receive(
               top_power_button_device,
               &reg,
               1,
               value,
               1,
               40) == ESP_OK;
}

static bool top_power_button_write(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    return top_power_button_device != NULL &&
           i2c_master_transmit(
               top_power_button_device,
               bytes,
               sizeof(bytes),
               40) == ESP_OK;
}

static void configure_top_power_button(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        ESP_LOGW(TAG, "PWR button unavailable: I2C bus is not ready");
        return;
    }
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(
            bus,
            &device_config,
            &top_power_button_device) != ESP_OK) {
        top_power_button_device = NULL;
        ESP_LOGW(TAG, "PWR button unavailable: could not attach AXP2101");
        return;
    }

    uint8_t chip_id = 0;
    uint8_t enabled = 0;
    uint8_t timing = 0;
    uint8_t power_off_enable = 0;
    if (!top_power_button_read(AXP2101_CHIP_ID_REGISTER, &chip_id) ||
        chip_id != AXP2101_CHIP_ID ||
        !top_power_button_read(AXP2101_INTERRUPT_ENABLE_2, &enabled) ||
        !top_power_button_read(AXP2101_IRQ_OFF_ON_LEVEL_CONTROL, &timing) ||
        !top_power_button_read(AXP2101_POWER_OFF_ENABLE, &power_off_enable)) {
        ESP_LOGW(TAG, "PWR button unavailable: AXP2101 did not respond");
        top_power_button_device = NULL;
        return;
    }

    static const char *irq_times[] = {"1.0", "1.5", "2.0", "2.5"};
    static const char *off_times[] = {"4", "6", "8", "10"};
    const uint8_t original_timing = timing;
    const uint8_t original_power_off_enable = power_off_enable;
    ESP_LOGI(
        TAG,
        "AXP2101 PWR config before: reg27=0x%02x irq=%ss off=%ss reg22=0x%02x hw_off=%s action=%s",
        original_timing,
        irq_times[(original_timing >> 4) & 0x03],
        off_times[(original_timing >> 2) & 0x03],
        original_power_off_enable,
        (original_power_off_enable & AXP2101_LONG_PRESS_SHUTDOWN_ENABLE) ? "on" : "off",
        (original_power_off_enable & AXP2101_LONG_PRESS_RESTART_SELECT) ? "restart" : "power-off");

    enabled |= AXP2101_POWER_KEY_EVENT_MASK;
    timing = (uint8_t)(
        (timing & (uint8_t)~0x3cU) |
        (AXP2101_IRQ_LEVEL_2_5_SECONDS << 4) |
        (AXP2101_OFF_LEVEL_8_SECONDS << 2));
    power_off_enable |= AXP2101_LONG_PRESS_SHUTDOWN_ENABLE;
    power_off_enable &= (uint8_t)~AXP2101_LONG_PRESS_RESTART_SELECT;

    const bool configured =
        top_power_button_write(AXP2101_INTERRUPT_ENABLE_2, enabled) &&
        top_power_button_write(AXP2101_IRQ_OFF_ON_LEVEL_CONTROL, timing) &&
        top_power_button_write(AXP2101_POWER_OFF_ENABLE, power_off_enable);
    (void)top_power_button_write(
        AXP2101_INTERRUPT_STATUS_2,
        AXP2101_POWER_KEY_EVENT_MASK);
    if (!configured) {
        ESP_LOGW(TAG, "AXP2101 PWR configuration write failed");
        return;
    }
    (void)refresh_battery_status_cache();
    ESP_LOGI(
        TAG,
        "AXP2101 PWR config active: irq=2.5s emergency-off=8s press/release edges enabled; safety bits preserved");
}

static axp_power_key_events_t take_top_power_button_events(void)
{
    axp_power_key_events_t events = {0};
    if (top_power_button_device == NULL) {
        return events;
    }

    uint8_t status = 0;
    if (!top_power_button_read(AXP2101_INTERRUPT_STATUS_2, &status)) {
        return events;
    }
    const uint8_t power_events = status & AXP2101_POWER_KEY_EVENT_MASK;
    if (power_events != 0) {
        (void)top_power_button_write(
            AXP2101_INTERRUPT_STATUS_2,
            power_events);
    }
    events.press_edge =
        (power_events & AXP2101_POWER_KEY_NEGATIVE_MASK) != 0;
    events.release_edge =
        (power_events & AXP2101_POWER_KEY_POSITIVE_MASK) != 0;
    events.long_press =
        (power_events & AXP2101_POWER_KEY_LONG_MASK) != 0;
    events.short_press =
        (power_events & AXP2101_POWER_KEY_SHORT_MASK) != 0;
    return events;
}

static bool axp2101_software_power_off(void)
{
    uint8_t common_config = 0;
    if (!top_power_button_read(AXP2101_COMMON_CONFIG, &common_config)) {
        return false;
    }
    return top_power_button_write(
        AXP2101_COMMON_CONFIG,
        common_config | AXP2101_SOFTWARE_POWER_OFF);
}
#else
static app_power_battery_status_t battery_status_cache = {
    .available = true,
    .present = true,
    .charging = true,
    .percent = 85,
};

void app_power_get_battery_status(app_power_battery_status_t *out)
{
    if (out != NULL) {
        *out = battery_status_cache;
    }
}

static bool refresh_battery_status_cache(void)
{
    return false;
}

static void configure_top_power_button(void)
{
}

static axp_power_key_events_t take_top_power_button_events(void)
{
    return (axp_power_key_events_t){0};
}

static bool axp2101_software_power_off(void)
{
    return false;
}
#endif

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
                       lv_align_t align, int x, int y)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_set_style_text_letter_space(obj, 1, 0);
    lv_obj_align(obj, align, x, y);
    return obj;
}

static lv_obj_t *label_box(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color,
                           lv_align_t align, int x, int y, int width)
{
    lv_obj_t *obj = label(parent, text, font, color, align, x, y);
    lv_obj_set_width(obj, width);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    return obj;
}

static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t fill, lv_opa_t fill_opa,
                      lv_color_t border, lv_opa_t border_opa, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, fill, 0);
    lv_obj_set_style_bg_opa(obj, fill_opa, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    lv_obj_set_style_border_opa(obj, border_opa, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_shadow_color(obj, border, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
    return obj;
}

static lv_obj_t *ring(lv_obj_t *parent, int diameter, lv_color_t color, int width, lv_opa_t opacity)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, diameter, diameter);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, width, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_border_opa(obj, opacity, 0);
    lv_obj_center(obj);
    return obj;
}

static lv_obj_t *dot(lv_obj_t *parent, int x, int y, int size, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_shadow_color(obj, color, 0);
    lv_obj_set_style_shadow_width(obj, size <= 18 ? 5 : 0, 0);
    lv_obj_set_style_shadow_opa(obj, size <= 18 ? LV_OPA_30 : LV_OPA_TRANSP, 0);
    lv_obj_set_pos(obj, x - size / 2, y - size / 2);
    return obj;
}

typedef struct {
    lv_point_precise_t points[3];
    lv_color_t color;
    lv_opa_t opacity;
} filled_triangle_data_t;

static void filled_triangle_event_cb(lv_event_t *event)
{
    filled_triangle_data_t *data =
        (filled_triangle_data_t *)lv_event_get_user_data(event);
    if (data == NULL) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        lv_free(data);
        return;
    }
    if (code != LV_EVENT_DRAW_MAIN) {
        return;
    }

    lv_obj_t *object = lv_event_get_target(event);
    lv_area_t area;
    lv_obj_get_coords(object, &area);

    lv_draw_triangle_dsc_t descriptor;
    lv_draw_triangle_dsc_init(&descriptor);
    descriptor.base.layer = lv_event_get_layer(event);
    descriptor.color = data->color;
    descriptor.opa = data->opacity;
    for (int point = 0; point < 3; point++) {
        descriptor.p[point].x =
            area.x1 + data->points[point].x;
        descriptor.p[point].y =
            area.y1 + data->points[point].y;
    }
    lv_draw_triangle(
        lv_event_get_layer(event),
        &descriptor);
}

static lv_obj_t *filled_triangle(lv_obj_t *parent,
                                 int x0,
                                 int y0,
                                 int x1,
                                 int y1,
                                 int x2,
                                 int y2,
                                 lv_color_t color,
                                 lv_opa_t opacity)
{
    const int minimum_x =
        x0 < x1
            ? (x0 < x2 ? x0 : x2)
            : (x1 < x2 ? x1 : x2);
    const int maximum_x =
        x0 > x1
            ? (x0 > x2 ? x0 : x2)
            : (x1 > x2 ? x1 : x2);
    const int minimum_y =
        y0 < y1
            ? (y0 < y2 ? y0 : y2)
            : (y1 < y2 ? y1 : y2);
    const int maximum_y =
        y0 > y1
            ? (y0 > y2 ? y0 : y2)
            : (y1 > y2 ? y1 : y2);

    filled_triangle_data_t *data =
        lv_malloc(sizeof(*data));
    if (data == NULL) {
        return NULL;
    }
    data->points[0] =
        (lv_point_precise_t){
            .x = x0 - minimum_x,
            .y = y0 - minimum_y,
        };
    data->points[1] =
        (lv_point_precise_t){
            .x = x1 - minimum_x,
            .y = y1 - minimum_y,
        };
    data->points[2] =
        (lv_point_precise_t){
            .x = x2 - minimum_x,
            .y = y2 - minimum_y,
        };
    data->color = color;
    data->opacity = opacity;

    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, minimum_x, minimum_y);
    lv_obj_set_size(
        object,
        maximum_x - minimum_x + 1,
        maximum_y - minimum_y + 1);
    lv_obj_remove_flag(
        object,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(
        object,
        filled_triangle_event_cb,
        LV_EVENT_ALL,
        data);
    return object;
}

static lv_obj_t *soft_blob(lv_obj_t *parent, int x, int y, int size, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *obj = dot(parent, x, y, size, color, opa);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
    return obj;
}

typedef struct {
    const char *icao;
    const char *iata;
} airline_code_map_t;

static const airline_code_map_t airline_code_map[] = {
    {"AAL", "AA"}, {"ACA", "AC"}, {"AFR", "AF"}, {"AMX", "AM"},
    {"ANA", "NH"}, {"ASA", "AS"}, {"AAR", "OZ"}, {"AZA", "AZ"},
    {"BAW", "BA"}, {"CMP", "CM"}, {"CPA", "CX"}, {"DAL", "DL"},
    {"DLH", "LH"}, {"EDV", "9E"}, {"EIN", "EI"}, {"ENY", "MQ"},
    {"FFT", "F9"}, {"FIN", "AY"}, {"GLO", "G3"}, {"HAL", "HA"},
    {"IBE", "IB"}, {"JAL", "JL"}, {"JBU", "B6"}, {"JIA", "OH"},
    {"KAL", "KE"}, {"KLM", "KL"}, {"LAN", "LA"}, {"NKS", "NK"},
    {"PDT", "PT"}, {"QFA", "QF"}, {"QTR", "QR"}, {"QXE", "QX"},
    {"RPA", "YX"}, {"SAS", "SK"}, {"SIA", "SQ"}, {"SKW", "OO"},
    {"SWA", "WN"}, {"TAM", "JJ"}, {"TAP", "TP"}, {"THY", "TK"},
    {"UAL", "UA"}, {"UAE", "EK"}, {"UCA", "C5"}, {"VIR", "VS"},
    {"VOI", "Y4"}, {"WJA", "WS"},
};

static bool callsign_looks_commercial_flight(const char *callsign)
{
    if (callsign == NULL || !isalpha((unsigned char)callsign[0]) ||
        !isalpha((unsigned char)callsign[1]) || !isalpha((unsigned char)callsign[2])) {
        return false;
    }
    return isdigit((unsigned char)callsign[3]);
}

static void aircraft_display_label(const app_aircraft_t *aircraft, char *dst, size_t dst_size)
{
    if (dst_size == 0) {
        return;
    }
    const char *callsign = (aircraft != NULL && aircraft->callsign[0] != '\0')
        ? aircraft->callsign
        : "UNKNOWN";
    snprintf(dst, dst_size, "%s", callsign);

    if (!callsign_looks_commercial_flight(callsign)) {
        return;
    }

    char prefix[4] = {
        (char)toupper((unsigned char)callsign[0]),
        (char)toupper((unsigned char)callsign[1]),
        (char)toupper((unsigned char)callsign[2]),
        '\0',
    };
    const char *digits = callsign + 3;
    if (aircraft != NULL && aircraft->airline_iata[0] != '\0') {
        snprintf(dst, dst_size, "%s%s", aircraft->airline_iata, digits);
        return;
    }
    for (size_t i = 0; i < sizeof(airline_code_map) / sizeof(airline_code_map[0]); i++) {
        if (strcmp(prefix, airline_code_map[i].icao) == 0) {
            snprintf(dst, dst_size, "%s%s", airline_code_map[i].iata, digits);
            return;
        }
    }
}

static lv_obj_t *line(lv_obj_t *parent, int x1, int y1, int x2, int y2, lv_color_t color, int width, lv_opa_t opa)
{
    if (line_pair_count >= MAX_LINE_PAIRS) {
        return NULL;
    }

    lv_point_precise_t *pts = line_pairs[line_pair_count++];
    pts[0].x = x1;
    pts[0].y = y1;
    pts[1].x = x2;
    pts[1].y = y2;

    lv_obj_t *obj = lv_line_create(parent);
    lv_line_set_points(obj, pts, 2);
    lv_obj_set_style_line_color(obj, color, 0);
    lv_obj_set_style_line_width(obj, width, 0);
    lv_obj_set_style_line_opa(obj, opa, 0);
    lv_obj_set_style_line_rounded(obj, true, 0);
    return obj;
}

static lv_obj_t *polyline(lv_obj_t *parent, const int coords[][2], int count, lv_color_t color, int width, lv_opa_t opa)
{
    if (polyline_count >= (int)(sizeof(polylines) / sizeof(polylines[0])) ||
        count > MAX_POLYLINE_POINTS) {
        return NULL;
    }

    lv_point_precise_t *pts = polylines[polyline_count++].pts;
    for (int i = 0; i < count; i++) {
        pts[i].x = coords[i][0];
        pts[i].y = coords[i][1];
    }

    lv_obj_t *obj = lv_line_create(parent);
    lv_line_set_points(obj, pts, count);
    lv_obj_set_style_line_color(obj, color, 0);
    lv_obj_set_style_line_width(obj, width, 0);
    lv_obj_set_style_line_opa(obj, opa, 0);
    lv_obj_set_style_line_rounded(obj, true, 0);
    return obj;
}

static bool launch_get_selected_status(
    app_remote_status_t *remote)
{
    if (app_remote_get_launch_status(
            launch_selected_agency,
            remote)) {
        return true;
    }
    for (int agency = 0;
         agency < APP_REMOTE_LAUNCH_AGENCY_COUNT;
         agency++) {
        if (app_remote_get_launch_status(
                (app_remote_launch_agency_t)agency,
                remote)) {
            launch_selected_agency =
                (app_remote_launch_agency_t)agency;
            return true;
        }
    }
    app_remote_get_status(remote);
    return remote != NULL && remote->launch_valid;
}

static void launch_select_global_next(void)
{
    int selected = -1;
    int64_t selected_net = INT64_MAX;
    for (int agency = 0;
         agency < APP_REMOTE_LAUNCH_AGENCY_COUNT;
         agency++) {
        app_remote_status_t candidate;
        if (!app_remote_get_launch_status(
                (app_remote_launch_agency_t)agency,
                &candidate)) {
            continue;
        }
        const int64_t net =
            candidate.launch_net_unix > 0
                ? candidate.launch_net_unix
                : INT64_MAX - agency;
        if (selected < 0 || net < selected_net) {
            selected = agency;
            selected_net = net;
        }
    }
    if (selected >= 0) {
        launch_selected_agency =
            (app_remote_launch_agency_t)selected;
    }
}

static bool cycle_launch_agency(void)
{
    const int current = (int)launch_selected_agency;
    for (int offset = 1;
         offset <= APP_REMOTE_LAUNCH_AGENCY_COUNT;
         offset++) {
        const int candidate =
            (current + offset) %
            APP_REMOTE_LAUNCH_AGENCY_COUNT;
        app_remote_status_t ignored;
        if (!app_remote_get_launch_status(
                (app_remote_launch_agency_t)candidate,
                &ignored)) {
            continue;
        }
        if (candidate == current) {
            return false;
        }
        launch_selected_agency =
            (app_remote_launch_agency_t)candidate;
        launch_animation_ticks = 0;
        launch_countdown_second_seen = -1;
        render_requested = true;
        dock_mark_visible_state_changed();
        return true;
    }
    return false;
}

static void select_space_view(
    space_view_t next,
    bool animate_eclipse)
{
    const bool entering_iss_from_eclipse =
        space_view == SPACE_VIEW_ECLIPSE &&
        next == SPACE_VIEW_ISS;
    stop_eclipse_terminator_animation();
    space_view = next;
    space_iss_flags_fade_started_us =
        entering_iss_from_eclipse
            ? esp_timer_get_time()
            : 0;
    space_planet_annotation_mode = 0;
    space_city_marker_visible = false;
    space_solar_intro_active = next == SPACE_VIEW_SOLAR;
    space_solar_intro_start_us = 0;
    if (next == SPACE_VIEW_ECLIPSE && animate_eclipse) {
        start_eclipse_terminator_animation();
    }
}

static void set_screen(app_screen_t next)
{
    const bool screen_changed = current_screen != next;
    if (current_screen == SCREEN_WEATHER &&
        next != SCREEN_WEATHER) {
        app_config_t cfg;
        app_config_get(&cfg);
        app_data_set_weather_location_key(cfg.location_key);
    }
    gmt_live_hands_layer = NULL;
    gmt_live_hands_second_seen = -1;
    if (current_screen == SCREEN_SPACE ||
        next == SCREEN_SPACE) {
        space_planet_annotation_mode = 0;
        if (next != SCREEN_SPACE) {
            stop_eclipse_terminator_animation();
        }
    }
    aircraft_detail_active = false;
    radar_range_menu_open = false;
    radar_menu_overlay = NULL;
    weather_radar_playing = false;
    weather_radar_playback_index = -1;
    weather_radar_playback_step_us = 0;
    if (next == SCREEN_TRACKED_FLIGHT) {
        tracked_flight_view = TRACKED_FLIGHT_GLOBE;
        tracked_panels_visible = true;
        tracked_panels_entry_animation_pending = true;
    }
    if (next == SCREEN_WEATHER) {
        app_config_t cfg;
        app_config_get(&cfg);
        snprintf(
            weather_selected_key,
            sizeof(weather_selected_key),
            "%.7s",
            cfg.location_key);
        app_data_set_weather_location_key(
            weather_selected_key);
        weather_radar_visible = false;
        weather_forecast_visible = false;
        weather_entry_animation_pending = true;
    }
    if (next == SCREEN_GMT) {
        clock_digital_mode = false;
        clock_selected_key[0] = '\0';
        clock_secondary_key[0] = '\0';
        clock_bezel_uses_secondary = false;
        gmt_secondary_transition_active = false;
        gmt_secondary_transition_finish_pending = false;
        gmt_date_day_seen = -1;
        gmt_date_location_key_seen[0] = '\0';
        memset(
            clock_city_buttons,
            0,
            sizeof(clock_city_buttons));
    }
    if (next == SCREEN_SPACE) {
        select_space_view(SPACE_VIEW_SOLAR, false);
        space_animation_ticks = 0;
    }
    if (next == SCREEN_LAUNCH) {
        if (current_screen != SCREEN_LAUNCH) {
            launch_select_global_next();
        }
        launch_view = LAUNCH_VIEW_MAIN;
        launch_animation_ticks = 0;
        launch_countdown_second_seen = -1;
    }
    current_screen = next;
    dock_mark_visible_state_changed();
#ifndef AIRSPACE_DESKTOP_RENDERER
    if (screen_changed &&
        !startup_splash_visible &&
        !display_sleeping &&
        !power_off_pending &&
        !power_menu_visible &&
        !dock_feedback_visible) {
        begin_screen_transition();
        return;
    }
#endif
    render_requested = true;
}

static void show_weather_radar(void)
{
    weather_forecast_visible = false;
    weather_radar_visible = true;
    weather_radar_loading = true;
    weather_radar_playing = false;
    weather_radar_playback_index = -1;
    weather_radar_playback_step_us = 0;
    app_data_request_weather_radar(radar_range_miles);
}

static void next_screen(void)
{
    app_config_t config;
    app_config_get(&config);
    int current_index = -1;
    for (uint8_t index = 0; index < config.main_screen_count; index++) {
        if (config.main_screens[index] == (uint8_t)current_screen) {
            current_index = index;
            break;
        }
    }
    const int next_index = current_index >= 0
        ? (current_index + 1) % config.main_screen_count
        : 0;
    set_screen((app_screen_t)config.main_screens[next_index]);
}

static void previous_screen(void)
{
    app_config_t config;
    app_config_get(&config);
    int current_index = -1;
    for (uint8_t index = 0; index < config.main_screen_count; index++) {
        if (config.main_screens[index] == (uint8_t)current_screen) {
            current_index = index;
            break;
        }
    }
    const int previous_index = current_index >= 0
        ? (current_index + config.main_screen_count - 1) %
            config.main_screen_count
        : config.main_screen_count - 1;
    set_screen((app_screen_t)config.main_screens[previous_index]);
}

static app_screen_t screen_for_remote_style(app_remote_style_t style)
{
    switch (style) {
    case APP_REMOTE_STYLE_XDR_RADAR:
        return SCREEN_RADAR;
    case APP_REMOTE_STYLE_CARTOGRAPHIC_GLASS:
        return SCREEN_TRACKED_FLIGHT;
    case APP_REMOTE_STYLE_GMT_CHRONOGRAPH:
        return SCREEN_GMT;
    case APP_REMOTE_STYLE_WEATHER:
    case APP_REMOTE_STYLE_WEATHER_RADAR:
        return SCREEN_WEATHER;
    case APP_REMOTE_STYLE_SPACE:
    case APP_REMOTE_STYLE_SPACE_ISS:
    case APP_REMOTE_STYLE_SPACE_MOON:
        return SCREEN_SPACE;
    case APP_REMOTE_STYLE_LAUNCH:
    case APP_REMOTE_STYLE_LAUNCH_MAP:
    case APP_REMOTE_STYLE_LAUNCH_QR:
        return SCREEN_LAUNCH;
    default:
        return SCREEN_RADAR;
    }
}

static int find_radar_target_at(lv_point_t point)
{
    for (int i = 0; i < APP_MAX_AIRCRAFT; i++) {
        const radar_aircraft_view_t *view = &radar_aircraft_views[i];
        if (!view->allocated || !view->visible) {
            continue;
        }
        const int dx = point.x - view->x;
        const int dy = point.y - view->y;
        if (dx * dx + dy * dy <= RADAR_TARGET_HIT_RADIUS * RADAR_TARGET_HIT_RADIUS) {
            return view->aircraft_index;
        }
    }
    return -1;
}

static int find_radar_range_button_at(lv_point_t point)
{
    for (int i = 0; i < radar_range_button_count; i++) {
        const radar_range_button_t *button = &radar_range_buttons[i];
        if (!button->valid) {
            continue;
        }
        if (point.x >= button->x && point.x <= button->x + button->w &&
            point.y >= button->y && point.y <= button->y + button->h) {
            return i;
        }
    }
    return -1;
}

static int find_weather_radar_range_button_at(lv_point_t point)
{
    for (int i = 0; i < RADAR_RANGE_BUTTON_COUNT; i++) {
        const radar_range_button_t *button =
            &weather_radar_range_buttons[i];
        if (!button->valid) {
            continue;
        }
        if (point.x >= button->x &&
            point.x <= button->x + button->w &&
            point.y >= button->y &&
            point.y <= button->y + button->h) {
            return i;
        }
    }
    return -1;
}

static int find_radar_wifi_profile_button_at(lv_point_t point)
{
    for (int i = 0; i < radar_wifi_profile_button_count; i++) {
        const radar_wifi_profile_button_t *button = &radar_wifi_profile_buttons[i];
        if (!button->valid) {
            continue;
        }
        if (point.x >= button->x && point.x <= button->x + button->w &&
            point.y >= button->y && point.y <= button->y + button->h) {
            return i;
        }
    }
    return -1;
}

static int find_radar_location_button_at(lv_point_t point)
{
    for (int i = 0; i < radar_location_button_count; i++) {
        const radar_location_button_t *button = &radar_location_buttons[i];
        if (!button->valid) {
            continue;
        }
        if (point.x >= button->x && point.x <= button->x + button->w &&
            point.y >= button->y && point.y <= button->y + button->h) {
            return i;
        }
    }
    return -1;
}

static bool is_point_in_range_button(lv_point_t point, const radar_range_button_t *button)
{
    return button != NULL &&
           button->valid &&
           point.x >= button->x &&
           point.x <= button->x + button->w &&
           point.y >= button->y &&
           point.y <= button->y + button->h;
}

static bool is_point_in_labels_button(lv_point_t point)
{
    return is_point_in_range_button(point, &radar_labels_button);
}

static bool is_point_in_wedge_button(lv_point_t point)
{
    return is_point_in_range_button(point, &radar_wedge_button);
}

static int find_radar_weather_unit_button_at(lv_point_t point)
{
    for (int index = 0; index < APP_WEATHER_UNITS_COUNT; index++) {
        if (is_point_in_range_button(
                point,
                &radar_weather_unit_buttons[index])) {
            return index;
        }
    }
    return -1;
}

static int find_radar_page_button_at(lv_point_t point)
{
    for (int i = 0; i < RADAR_MENU_PAGE_COUNT; i++) {
        const radar_page_button_t *button = &radar_page_buttons[i];
        if (!button->valid) {
            continue;
        }
        if (point.x >= button->x && point.x <= button->x + button->w &&
            point.y >= button->y && point.y <= button->y + button->h) {
            return button->page;
        }
    }
    return -1;
}

static int find_radar_accent_button_at(lv_point_t point)
{
    for (int i = 0; i < radar_accent_button_count; i++) {
        const radar_accent_button_t *button = &radar_accent_buttons[i];
        if (!button->valid) {
            continue;
        }
        if (point.x >= button->x && point.x <= button->x + button->w &&
            point.y >= button->y && point.y <= button->y + button->h) {
            return i;
        }
    }
    return -1;
}

static void set_radar_range_miles(float miles)
{
    if (fabsf(radar_range_miles - miles) < 0.1f) {
        render_requested = true;
        return;
    }
    radar_range_miles = miles;
    app_data_set_aircraft_radius_miles(radar_range_miles);
    app_config_t cfg;
    app_config_get(&cfg);
    cfg.radar_range_miles = (uint16_t)lroundf(radar_range_miles);
    if (app_config_save(&cfg) == ESP_OK) {
        app_remote_refresh_config_file();
    }
    if (current_screen == SCREEN_WEATHER && weather_radar_visible) {
        weather_radar_loading = true;
        weather_radar_playing = false;
        weather_radar_playback_index = -1;
        weather_radar_playback_step_us = 0;
        weather_composite_overlay_revision_seen = UINT32_MAX;
        weather_composite_frame_index_seen = -1;
        padded_map_revision_seen = UINT32_MAX;
        app_data_request_weather_radar(radar_range_miles);
    }
    render_requested = true;
}

static float clamp_display_brightness_percent(float percent)
{
    if (percent < 10.0f) {
        return 10.0f;
    }
    if (percent > 100.0f) {
        return 100.0f;
    }
    return percent;
}

static void update_radar_brightness_controls(float percent)
{
    percent = clamp_display_brightness_percent(percent);
    const int rounded_percent = (int)lroundf(percent);
    const int end_angle = percent >= 99.95f
        ? 359
        : (int)lroundf(percent * 359.0f / 100.0f);

    if (radar_brightness_arc != NULL &&
        lv_obj_is_valid(radar_brightness_arc)) {
        lv_arc_set_angles(radar_brightness_arc, 0, end_angle);
    }
    if (radar_brightness_value_label != NULL &&
        lv_obj_is_valid(radar_brightness_value_label)) {
        char value[8];
        snprintf(value, sizeof(value), "%d%%", rounded_percent);
        lv_label_set_text(radar_brightness_value_label, value);
    }

#ifndef AIRSPACE_DESKTOP_RENDERER
    const int raw = (int)lroundf(percent * 255.0f / 100.0f);
    if (raw != radar_brightness_last_raw) {
        (void)app_display_brightness_raw_set((uint8_t)raw);
        radar_brightness_last_raw = raw;
    }
#endif
}

static void radar_brightness_animation_cb(lv_timer_t *timer)
{
    const float difference =
        radar_brightness_target_percent - radar_brightness_visual_percent;
    if (fabsf(difference) < 0.08f) {
        radar_brightness_visual_percent = radar_brightness_target_percent;
    } else {
        /* A fast critically-damped feel: responsive to the finger while the
         * 16 ms timer hides the 50 ms touch-sampling cadence. */
        radar_brightness_visual_percent += difference * 0.46f;
    }
    update_radar_brightness_controls(radar_brightness_visual_percent);

    if (!radar_brightness_drag_active &&
        fabsf(radar_brightness_target_percent -
              radar_brightness_visual_percent) < 0.08f) {
        radar_brightness_visual_percent = radar_brightness_target_percent;
        update_radar_brightness_controls(radar_brightness_visual_percent);
        radar_brightness_animation_timer = NULL;
        lv_timer_delete(timer);
    }
}

static void start_radar_brightness_animation(void)
{
    if (radar_brightness_animation_timer == NULL) {
        radar_brightness_animation_timer =
            lv_timer_create(radar_brightness_animation_cb, 16, NULL);
    }
}

static void set_radar_brightness_target(float percent)
{
    radar_brightness_target_percent =
        clamp_display_brightness_percent(percent);
    start_radar_brightness_animation();
}

static void detach_radar_brightness_controls(bool apply_target)
{
    const bool had_controls =
        radar_brightness_arc != NULL ||
        radar_brightness_value_label != NULL ||
        radar_brightness_animation_timer != NULL;
    if (radar_brightness_animation_timer != NULL) {
        lv_timer_delete(radar_brightness_animation_timer);
        radar_brightness_animation_timer = NULL;
    }
    if (apply_target && had_controls) {
        radar_brightness_visual_percent =
            clamp_display_brightness_percent(
                radar_brightness_target_percent);
#ifndef AIRSPACE_DESKTOP_RENDERER
        const uint8_t raw = (uint8_t)lroundf(
            radar_brightness_visual_percent * 255.0f / 100.0f);
        (void)app_display_brightness_raw_set(raw);
#endif
    }
    radar_brightness_arc = NULL;
    radar_brightness_value_label = NULL;
    radar_brightness_drag_candidate = false;
    radar_brightness_drag_active = false;
    radar_brightness_last_raw = -1;
}

static void set_display_brightness_percent(int percent)
{
    if (percent < 10) {
        percent = 10;
    } else if (percent > 100) {
        percent = 100;
    }

    app_config_t cfg;
    app_config_get(&cfg);
    if (cfg.display_brightness_percent != percent) {
        cfg.display_brightness_percent = (uint8_t)percent;
        if (app_config_save(&cfg) == ESP_OK) {
            app_remote_refresh_config_file();
        }
    }
    splash_restore_brightness = percent;
    display_sleep_brightness = percent;
    screen_transition_target_brightness = percent;
    if (radar_range_menu_open && radar_menu_page == 2 &&
        radar_brightness_arc != NULL &&
        lv_obj_is_valid(radar_brightness_arc)) {
        set_radar_brightness_target((float)percent);
    } else {
#ifndef AIRSPACE_DESKTOP_RENDERER
        (void)bsp_display_brightness_set(percent);
#endif
        radar_brightness_visual_percent = (float)percent;
        radar_brightness_target_percent = (float)percent;
        render_requested = true;
    }
}

static void set_active_wifi_profile(app_wifi_profile_id_t profile)
{
    app_config_t cfg;
    app_config_get(&cfg);
    if (cfg.active_wifi_profile == profile) {
        render_requested = true;
        return;
    }

    cfg.active_wifi_profile = profile;
    if (app_config_save(&cfg) == ESP_OK) {
        app_remote_refresh_config_file();
        app_network_apply_config(true);
    }
    render_requested = true;
}

static void set_radar_location_key(const char *key)
{
    app_config_t cfg;
    app_config_get(&cfg);
    if (key == NULL || key[0] == '\0') {
        render_requested = true;
        return;
    }
    if (strcasecmp(cfg.location_key, key) == 0) {
        render_requested = true;
        return;
    }

    if (app_location_apply_preset(&cfg, key) && app_config_save(&cfg) == ESP_OK) {
        app_remote_refresh_config_file();
        app_network_apply_config(false);
        app_remote_refresh_map_storage_index();
        app_data_request_refresh();
    }
    render_requested = true;
}

static float next_radar_range_miles(void)
{
    if (radar_range_miles < 15.0f) {
        return 20.0f;
    }
    if (radar_range_miles < 35.0f) {
        return 50.0f;
    }
    return 10.0f;
}

static const app_location_preset_t *next_location_preset(
    const char *current_key)
{
    app_config_t cfg;
    app_config_get(&cfg);

    char keys[sizeof(cfg.radar_city_keys)];
    snprintf(keys, sizeof(keys), "%s", cfg.radar_city_keys);
    const app_location_preset_t *first = NULL;
    bool return_next = false;
    char *save_pointer = NULL;
    for (char *key = strtok_r(keys, ",", &save_pointer);
         key != NULL;
         key = strtok_r(NULL, ",", &save_pointer)) {
        const app_location_preset_t *preset =
            app_location_preset_find(key);
        if (preset == NULL) {
            continue;
        }
        if (first == NULL) {
            first = preset;
        }
        if (return_next) {
            return preset;
        }
        if (current_key != NULL &&
            strcasecmp(preset->key, current_key) == 0) {
            return_next = true;
        }
    }
    return first;
}

static void select_weather_location(const char *key)
{
    const app_location_preset_t *preset =
        app_location_preset_find(key);
    if (preset == NULL) {
        return;
    }
    snprintf(
        weather_selected_key,
        sizeof(weather_selected_key),
        "%.7s",
        preset->key);
    app_data_set_weather_location_key(
        weather_selected_key);
    weather_entry_animation_pending = true;
    if (weather_radar_visible) {
        weather_radar_loading = true;
        weather_radar_playing = false;
        weather_radar_playback_index = -1;
        weather_radar_playback_step_us = 0;
        weather_composite_overlay_revision_seen = UINT32_MAX;
        weather_composite_frame_index_seen = -1;
        padded_map_revision_seen = UINT32_MAX;
        app_data_request_weather_radar(
            radar_range_miles);
    }
    render_requested = true;
}

static void cycle_weather_location(void)
{
    const app_location_preset_t *next =
        next_location_preset(weather_selected_key);
    if (next != NULL) {
        select_weather_location(next->key);
    }
}

static void cycle_radar_location(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    const app_location_preset_t *next =
        next_location_preset(cfg.location_key);
    if (next == NULL) {
        return;
    }
    set_radar_location_key(next->key);
    radar_range_menu_open = false;
    set_screen(SCREEN_RADAR);
}

static void handle_bottom_button_short_press(void)
{
    if (aircraft_detail_active) {
        return;
    }
    switch (current_screen) {
    case SCREEN_RADAR:
        radar_range_menu_open = false;
        set_radar_range_miles(
            next_radar_range_miles());
        break;
    case SCREEN_WEATHER:
        cycle_weather_location();
        break;
    case SCREEN_GMT:
        if (clock_digital_mode) {
            if (clock_select_next_displayed_city()) {
                render_requested = true;
            }
        } else {
            (void)clock_begin_secondary_transition();
        }
        break;
    case SCREEN_LAUNCH:
        (void)cycle_launch_agency();
        break;
    case SCREEN_SPACE:
        if (space_view == SPACE_VIEW_ECLIPSE) {
            select_space_view(SPACE_VIEW_ISS, false);
            const app_data_status_t *data = ui_data_status();
            if (data == NULL || !data->iss_valid) {
                app_data_request_refresh();
            }
        } else if (space_view == SPACE_VIEW_ISS) {
            /* Enter the eclipse page in its quiet, completed state.  The
             * bottom-button long press is the deliberate replay gesture. */
            select_space_view(SPACE_VIEW_ECLIPSE, false);
        } else {
            break;
        }
        if (!begin_space_map_overlay_transition()) {
            begin_screen_transition();
        }
        break;
    case SCREEN_TRACKED_FLIGHT:
    default:
        break;
    }
}

static void handle_bottom_button_long_press(void)
{
    if (aircraft_detail_active) {
        return;
    }
    if (current_screen == SCREEN_RADAR) {
        cycle_radar_location();
    } else if (
        current_screen == SCREEN_WEATHER &&
        weather_radar_visible) {
        set_radar_range_miles(
            next_radar_range_miles());
    } else if (
        current_screen == SCREEN_SPACE &&
        space_view == SPACE_VIEW_ECLIPSE) {
        start_eclipse_terminator_animation();
    }
}

static void toggle_aircraft_labels(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    cfg.aircraft_labels_enabled = !cfg.aircraft_labels_enabled;
    if (app_config_save(&cfg) == ESP_OK) {
        app_remote_refresh_config_file();
        app_network_apply_config(false);
    }
    render_requested = true;
}

static void toggle_radar_wedge(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    cfg.radar_wedge_enabled = !cfg.radar_wedge_enabled;
    if (app_config_save(&cfg) == ESP_OK) {
        app_remote_refresh_config_file();
        radar_wedge_visible = cfg.radar_wedge_enabled;
        app_network_apply_config(false);
    }
    render_requested = true;
}

static void toggle_iss_flags(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    cfg.iss_flags_enabled = !cfg.iss_flags_enabled;
    if (app_config_save(&cfg) == ESP_OK) {
        space_iss_flags_fade_started_us =
            cfg.iss_flags_enabled &&
                    space_view == SPACE_VIEW_ISS
                ? esp_timer_get_time()
                : 0;
        app_remote_refresh_config_file();
        app_network_apply_config(false);
    }
    render_requested = true;
}

static void set_weather_units(app_weather_units_t units)
{
    if (units < 0 || units >= APP_WEATHER_UNITS_COUNT) {
        return;
    }
    app_config_t cfg;
    app_config_get(&cfg);
    if (cfg.weather_units == units) {
        return;
    }
    cfg.weather_units = units;
    if (app_config_save(&cfg) == ESP_OK) {
        app_remote_refresh_config_file();
        app_network_apply_config(false);
    }
    render_requested = true;
}

static void set_radar_accent_color(app_accent_color_t color)
{
    app_config_t cfg;
    app_config_get(&cfg);
    if (cfg.accent_color == color) {
        render_requested = true;
        return;
    }
    cfg.accent_color = color;
    if (app_config_save(&cfg) == ESP_OK) {
        app_remote_refresh_config_file();
        radar_palette_active = palette_for_accent(color);
        app_network_apply_config(false);
    }
    render_requested = true;
}

static void show_aircraft_detail(int aircraft_index)
{
    const app_data_status_t *data =
        ui_data_status();
    if (data == NULL ||
        !data->aircraft_valid ||
        aircraft_index < 0 ||
        aircraft_index >= data->aircraft_count) {
        return;
    }

    selected_aircraft =
        data->aircraft[aircraft_index];
    app_data_request_aircraft_enrichment(selected_aircraft.icao24, selected_aircraft.callsign);
    char logo_code[4] = {0};
    if (app_data_airline_logo_code(&selected_aircraft, logo_code)) {
        app_data_request_airline_logo(logo_code);
    }
    detail_entry_animation_pending = true;
    detail_logo_revision_seen = 0;
    detail_route_signature_seen[0] = '\0';
    aircraft_detail_active = true;
    render_requested = true;
}

static void draw_shell_on(lv_obj_t *screen)
{
    detach_radar_brightness_controls(false);
    const bool preserve_gmt_transition_mask =
        screen_transition != SCREEN_TRANSITION_IDLE &&
        screen_transition_visual == SCREEN_TRANSITION_GMT_INNER &&
        screen_transition_inner_mask != NULL &&
        lv_obj_is_valid(screen_transition_inner_mask);
    lv_obj_clean(screen);
    if (!preserve_gmt_transition_mask) {
        screen_transition_inner_mask = NULL;
    }
    if (current_screen != SCREEN_SPACE &&
        current_screen != SCREEN_LAUNCH) {
        trim_space_asset_cache(NULL, 0);
    }
    line_pair_count = 0;
    polyline_count = 0;
    radar_range_button_count = 0;
    radar_wifi_profile_button_count = 0;
    radar_location_button_count = 0;
    radar_accent_button_count = 0;
    memset(radar_page_buttons, 0, sizeof(radar_page_buttons));
    radar_menu_button.valid = false;
    radar_labels_button.valid = false;
    radar_wedge_button.valid = false;
    radar_menu_overlay = NULL;
    radar_aircraft_layer = NULL;
    memset(radar_aircraft_views, 0, sizeof(radar_aircraft_views));
    radar_aircraft_status_unix_seen = -1;
    radar_aircraft_sprite_cursor = 0;
    memset(pushed_map_tile_objects, 0, sizeof(pushed_map_tile_objects));
    weather_temperature_label = NULL;
    weather_summary_label = NULL;
    weather_precipitation_label = NULL;
    weather_humidity_label = NULL;
    weather_wind_label = NULL;
    weather_pressure_label = NULL;
    weather_aqi_label = NULL;
    weather_uv_label = NULL;
    memset(weather_scale_markers, 0, sizeof(weather_scale_markers));
    memset(weather_forecast_day_labels, 0, sizeof(weather_forecast_day_labels));
    memset(weather_forecast_high_labels, 0, sizeof(weather_forecast_high_labels));
    memset(weather_forecast_low_labels, 0, sizeof(weather_forecast_low_labels));
    memset(weather_forecast_precip_labels, 0, sizeof(weather_forecast_precip_labels));
    weather_radar_temperature_label = NULL;
    weather_radar_time_label = NULL;
    weather_radar_progress_fill = NULL;
    weather_radar_progress_marker = NULL;
    gmt_date_animation_window = NULL;
    memset(space_planets, 0, sizeof(space_planets));
    memset(space_planet_symbols, 0, sizeof(space_planet_symbols));
    memset(space_planet_labels, 0, sizeof(space_planet_labels));
    space_earth_glow = NULL;
    space_iss_marker = NULL;
    space_map_overlay_layer = NULL;
    space_map_overlay_transition_active = false;
    space_city_marker = NULL;
    space_city_marker_glow = NULL;
    space_eclipse_time_label = NULL;
    space_eclipse_path_asset = NULL;
    space_iss_night_image_object = NULL;
    space_iss_terminator_image_object = NULL;
    space_eclipse_motion_image_object = NULL;
    space_eclipse_motion_dirty_count = 0;
    space_eclipse_shade_step = -1;
    for (int index = 0;
         index < ECLIPSE_TERMINATOR_MAX_GROUPS;
         index++) {
        space_eclipse_terminator_groups[index].point_count = 0;
    }
    launch_countdown_prefix_label = NULL;
    launch_countdown_label = NULL;
    launch_map_marker = NULL;
    launch_map_pad_glow = NULL;
    launch_inner_ring = NULL;
#if RADAR_SWEEP_ENABLED
    radar_sweep_obj = NULL;
#endif

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x010101), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    screen_root = lv_obj_create(screen);
    lv_obj_remove_style_all(screen_root);
    lv_obj_set_size(screen_root, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_center(screen_root);
    lv_obj_add_flag(screen_root, LV_OBJ_FLAG_CLICKABLE);
    tracked_panel_primary = NULL;
    tracked_panel_secondary = NULL;
    tracked_time_pages[0] = NULL;
    tracked_time_pages[1] = NULL;
    memset(
        tracked_gate_value_pages,
        0,
        sizeof(tracked_gate_value_pages));
    tracked_time_page_base_y = 0;
    tracked_preflight_days_label = NULL;
    tracked_preflight_days_units_label = NULL;
    tracked_preflight_countdown_label = NULL;
    tracked_preflight_countdown_units_label = NULL;
    tracked_preflight_gate_sign = NULL;
    tracked_preflight_countdown_minute_seen = -1;
    tracked_aircraft_holder = NULL;
    tracked_aircraft_image = NULL;
    tracked_aircraft_progress_epoch_us = 0;
}

static void draw_shell(void)
{
    draw_shell_on(lv_screen_active());
}

static bool point_in_rect(lv_point_t point, int x, int y, int width, int height)
{
    return point.x >= x && point.x < x + width &&
           point.y >= y && point.y < y + height;
}

static void render_power_menu_button(int y,
                                     const char *title,
                                     const char *subtitle,
                                     uint32_t accent,
                                     bool destructive)
{
    lv_obj_t *button = card(
        screen_root,
        55,
        y,
        356,
        92,
        lv_color_hex(destructive ? 0x110607 : 0x050a07),
        (lv_opa_t)246,
        lv_color_hex(accent),
        destructive ? (lv_opa_t)145 : (lv_opa_t)105,
        31);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_width(button, 15, 0);
    lv_obj_set_style_shadow_opa(button, destructive ? (lv_opa_t)26 : (lv_opa_t)18, 0);
    dot(button, 48, 46, 14, lv_color_hex(accent), LV_OPA_COVER);
    dot(button, 48, 46, 28, lv_color_hex(accent), (lv_opa_t)22);
    label(
        button,
        title,
        &lv_font_montserrat_24,
        lv_color_hex(0xf2f5f3),
        LV_ALIGN_LEFT_MID,
        82,
        -11);
    label(
        button,
        subtitle,
        &lv_font_montserrat_12,
        lv_color_hex(0x78817c),
        LV_ALIGN_LEFT_MID,
        82,
        19);
}

static void render_power_menu(void)
{
    draw_shell();
    const uint32_t accent = radar_palette_active->main;
    ring(screen_root, 438, lv_color_hex(0x1a201c), 1, (lv_opa_t)110);
    ring(screen_root, 404, lv_color_hex(accent), 1, (lv_opa_t)32);
    label(
        screen_root,
        "POWER",
        &lv_font_montserrat_14,
        lv_color_hex(0x7c8781),
        LV_ALIGN_TOP_MID,
        0,
        55);
    label(
        screen_root,
        "Choose an action",
        &lv_font_montserrat_24,
        lv_color_hex(0xf2f5f3),
        LV_ALIGN_TOP_MID,
        0,
        83);
    render_power_menu_button(
        151,
        "Sleep",
        "DISPLAY OFF  /  STATE HELD",
        accent,
        false);
    render_power_menu_button(
        267,
        "Power Off",
        "COMPLETE SYSTEM SHUTDOWN",
        0xc94a51,
        true);
    label(
        screen_root,
        "TAP OUTSIDE TO CLOSE",
        &lv_font_montserrat_12,
        lv_color_hex(0x555e59),
        LV_ALIGN_BOTTOM_MID,
        0,
        -49);
}

#ifndef AIRSPACE_DESKTOP_RENDERER
extern const uint8_t startup_png_start[]
    asm("_binary_startup_png_start");
extern const uint8_t startup_png_end[]
    asm("_binary_startup_png_end");

static float splash_smoothstep(float value)
{
    if (value <= 0.0f) {
        return 0.0f;
    }
    if (value >= 1.0f) {
        return 1.0f;
    }
    return value * value * (3.0f - 2.0f * value);
}

static bool prepare_startup_splash_image(void)
{
    if (startup_splash_buffer != NULL &&
        startup_splash_image.data == startup_splash_buffer) {
        return true;
    }

    const size_t png_size =
        (size_t)(startup_png_end - startup_png_start);
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (png_size == 0 ||
        !png_image_begin_read_from_memory(
            &image,
            startup_png_start,
            png_size)) {
        ESP_LOGE(TAG, "Startup artwork PNG header could not be decoded");
        return false;
    }
    image.format = PNG_FORMAT_RGB;
    const size_t rgb_bytes = PNG_IMAGE_SIZE(image);
    if (image.width != SCREEN_SIZE ||
        image.height != SCREEN_SIZE ||
        rgb_bytes !=
            (size_t)SCREEN_SIZE * SCREEN_SIZE * 3U) {
        ESP_LOGE(
            TAG,
            "Startup artwork has unexpected size %lux%lu",
            (unsigned long)image.width,
            (unsigned long)image.height);
        png_image_free(&image);
        return false;
    }

    uint8_t *rgb = heap_caps_malloc(
        rgb_bytes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t rgb565_bytes =
        (size_t)SCREEN_SIZE * SCREEN_SIZE * 2U;
    uint8_t *rgb565 = heap_caps_malloc(
        rgb565_bytes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb == NULL || rgb565 == NULL ||
        !png_image_finish_read(&image, NULL, rgb, 0, NULL)) {
        ESP_LOGE(
            TAG,
            "Not enough PSRAM to decode startup artwork");
        heap_caps_free(rgb);
        heap_caps_free(rgb565);
        png_image_free(&image);
        return false;
    }

    const size_t pixels =
        (size_t)SCREEN_SIZE * SCREEN_SIZE;
    for (size_t index = 0; index < pixels; index++) {
        const uint8_t red = rgb[index * 3U];
        const uint8_t green = rgb[index * 3U + 1U];
        const uint8_t blue = rgb[index * 3U + 2U];
        const uint16_t pixel =
            (uint16_t)(
                ((uint16_t)(red >> 3) << 11) |
                ((uint16_t)(green >> 2) << 5) |
                (uint16_t)(blue >> 3));
        rgb565[index * 2U] = (uint8_t)(pixel & 0xffU);
        rgb565[index * 2U + 1U] = (uint8_t)(pixel >> 8);
    }
    heap_caps_free(rgb);
    png_image_free(&image);

    startup_splash_buffer = rgb565;
    memset(&startup_splash_image, 0, sizeof(startup_splash_image));
    startup_splash_image.header.cf = LV_COLOR_FORMAT_RGB565;
    startup_splash_image.header.w = SCREEN_SIZE;
    startup_splash_image.header.h = SCREEN_SIZE;
    startup_splash_image.header.stride = SCREEN_SIZE * 2;
    startup_splash_image.data_size = (uint32_t)rgb565_bytes;
    startup_splash_image.data = startup_splash_buffer;
    ESP_LOGI(
        TAG,
        "Startup artwork ready: PNG=%uKB framebuffer=%uKB PSRAM-free=%uKB",
        (unsigned)(png_size / 1024U),
        (unsigned)(rgb565_bytes / 1024U),
        (unsigned)(
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM) /
            1024U));
    return true;
}

static void release_startup_splash_image(void)
{
    if (startup_splash_buffer == NULL) {
        splash_cleanup_pending = false;
        startup_splash_object = NULL;
        return;
    }
    lv_image_cache_drop(&startup_splash_image);
    heap_caps_free(startup_splash_buffer);
    startup_splash_buffer = NULL;
    startup_splash_object = NULL;
    memset(&startup_splash_image, 0, sizeof(startup_splash_image));
    splash_cleanup_pending = false;
}

static bool render_startup_shutdown_splash(void)
{
    if (!prepare_startup_splash_image()) {
        return false;
    }
    draw_shell();
    startup_splash_object = lv_image_create(screen_root);
    lv_image_set_src(
        startup_splash_object,
        &startup_splash_image);
    lv_obj_set_pos(startup_splash_object, 0, 0);
    lv_obj_remove_flag(
        startup_splash_object,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_image_recolor(
        startup_splash_object,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(
        startup_splash_object,
        LV_OPA_TRANSP,
        0);
    return true;
}

static void stop_splash_brightness_timer(void)
{
    if (splash_brightness_timer != NULL) {
        lv_timer_t *timer = splash_brightness_timer;
        splash_brightness_timer = NULL;
        lv_timer_delete(timer);
    }
}

static void splash_brightness_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const int64_t now_us = esp_timer_get_time();
    const int64_t elapsed_us =
        now_us - splash_transition_started_us;
    float progress = 0.0f;
    float brightness_level = (float)splash_restore_brightness;
    int brightness = splash_restore_brightness;
    bool use_full_resolution_brightness = false;

    switch (splash_transition) {
    case SPLASH_TRANSITION_STARTUP_HOLD:
        /* The panel is uploaded in six-row bands. Keep it completely dark
         * until the initial full-frame transfer has settled, otherwise the
         * artwork is visibly revealed from top to bottom. */
        use_full_resolution_brightness = true;
        brightness_level = 0.0f;
        brightness = 0;
        if (elapsed_us >= SPLASH_STARTUP_HOLD_US) {
            splash_transition = SPLASH_TRANSITION_STARTUP_RAMP;
            splash_transition_started_us = now_us;
        }
        break;
    case SPLASH_TRANSITION_STARTUP_RAMP:
        use_full_resolution_brightness = true;
        progress = splash_smoothstep(
            (float)elapsed_us /
            (float)SPLASH_STARTUP_RAMP_US);
        brightness_level =
            progress * (float)splash_restore_brightness;
        brightness = (int)lroundf(brightness_level);
        if (elapsed_us >= SPLASH_STARTUP_RAMP_US) {
            (void)bsp_display_brightness_set(
                splash_restore_brightness);
            splash_transition = SPLASH_TRANSITION_IDLE;
            stop_splash_brightness_timer();
            startup_splash_visible = false;
            splash_cleanup_pending = true;
            input_startup_guard_until_us = now_us + 200000LL;
            top_release_guard = top_button_held;
            boot_release_guard = boot_was_pressed;
            touch_release_guard = touch_was_pressed;
            render_requested = true;
            ESP_LOGI(
                TAG,
                "Startup brightness animation complete at %d%%",
                splash_restore_brightness);
            return;
        }
        break;
    case SPLASH_TRANSITION_SHUTDOWN_FADE:
        progress = splash_smoothstep(
            (float)elapsed_us /
            (float)SPLASH_SHUTDOWN_FADE_US);
        brightness = (int)lroundf(
            splash_shutdown_start_brightness *
            (1.0f - progress));
        if (elapsed_us >= SPLASH_SHUTDOWN_FADE_US) {
            (void)bsp_display_brightness_set(0);
            splash_transition = SPLASH_TRANSITION_IDLE;
            stop_splash_brightness_timer();
            power_off_deadline_us = now_us;
            return;
        }
        break;
    case SPLASH_TRANSITION_IDLE:
    default:
        stop_splash_brightness_timer();
        return;
    }

    if (brightness_level < 0.0f) {
        brightness_level = 0.0f;
        brightness = 0;
    } else if (brightness_level > 100.0f) {
        brightness_level = 100.0f;
        brightness = 100;
    }
    if (use_full_resolution_brightness) {
        const uint8_t raw_brightness = (uint8_t)lroundf(
            brightness_level * 255.0f / 100.0f);
        (void)app_display_brightness_raw_set(raw_brightness);
    } else {
        (void)bsp_display_brightness_set(brightness);
    }
}

static void begin_startup_splash_transition(void)
{
    splash_transition = SPLASH_TRANSITION_STARTUP_HOLD;
    splash_transition_started_us = esp_timer_get_time();
    stop_splash_brightness_timer();
    splash_brightness_timer = lv_timer_create(
        splash_brightness_timer_cb,
        SPLASH_STARTUP_FRAME_MS,
        NULL);
    if (splash_brightness_timer == NULL) {
        ESP_LOGW(TAG, "Could not create startup brightness timer");
        splash_transition = SPLASH_TRANSITION_IDLE;
        startup_splash_visible = false;
        splash_cleanup_pending = true;
        (void)bsp_display_brightness_set(
            splash_restore_brightness);
        render_requested = true;
    }
}

static void begin_shutdown_splash_transition(void)
{
    splash_shutdown_start_brightness =
        bsp_display_brightness_get();
    if (splash_shutdown_start_brightness < 1) {
        splash_shutdown_start_brightness =
            splash_restore_brightness;
    }
    splash_transition = SPLASH_TRANSITION_SHUTDOWN_FADE;
    splash_transition_started_us = esp_timer_get_time();
    stop_splash_brightness_timer();
    splash_brightness_timer = lv_timer_create(
        splash_brightness_timer_cb,
        SPLASH_SHUTDOWN_FRAME_MS,
        NULL);
}
#else
static void render_powering_off(void)
{
    draw_shell();
    label(
        screen_root,
        "POWERING OFF",
        &lv_font_montserrat_24,
        lv_color_hex(0xf4f4f3),
        LV_ALIGN_CENTER,
        0,
        0);
}
#endif

static float screen_transition_smoothstep(float value)
{
    if (value <= 0.0f) {
        return 0.0f;
    }
    if (value >= 1.0f) {
        return 1.0f;
    }
    return value * value * (3.0f - 2.0f * value);
}

static bool screen_transition_active(void)
{
#ifdef AIRSPACE_DESKTOP_RENDERER
    return false;
#else
    return screen_transition != SCREEN_TRANSITION_IDLE;
#endif
}

#ifndef AIRSPACE_DESKTOP_RENDERER
static void stop_screen_transition_timer(void)
{
    if (screen_transition_timer != NULL) {
        lv_timer_t *timer = screen_transition_timer;
        screen_transition_timer = NULL;
        lv_timer_delete(timer);
    }
}

static bool create_gmt_inner_transition_mask(lv_opa_t opacity)
{
    if (screen_root == NULL) {
        return false;
    }
    /* Keep the mask outside the active screen tree.  GMT snapshots rebuild
     * that tree while the dial is hidden; parenting the mask there caused a
     * one-frame delete/recreate discontinuity in every mode change. */
    screen_transition_inner_mask = lv_obj_create(lv_layer_top());
    if (screen_transition_inner_mask == NULL) {
        return false;
    }
    lv_obj_remove_style_all(screen_transition_inner_mask);
    lv_obj_set_size(
        screen_transition_inner_mask,
        GMT_INNER_FADE_DIAMETER,
        GMT_INNER_FADE_DIAMETER);
    lv_obj_center(screen_transition_inner_mask);
    lv_obj_set_style_radius(
        screen_transition_inner_mask,
        LV_RADIUS_CIRCLE,
        0);
    lv_obj_set_style_bg_color(
        screen_transition_inner_mask,
        lv_color_hex(0x000000),
        0);
    lv_obj_set_style_bg_opa(
        screen_transition_inner_mask,
        opacity,
        0);
    lv_obj_remove_flag(
        screen_transition_inner_mask,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(screen_transition_inner_mask);
    return true;
}

static int64_t screen_transition_duration_us(void)
{
    if (screen_transition_visual ==
        SCREEN_TRANSITION_GMT_INNER) {
        return screen_transition == SCREEN_TRANSITION_FADE_OUT
            ? GMT_INNER_FADE_OUT_US
            : GMT_INNER_FADE_IN_US;
    }
    if (screen_transition_dock_timing) {
        return screen_transition == SCREEN_TRANSITION_FADE_OUT
            ? DOCK_SCREEN_FADE_OUT_US
            : DOCK_SCREEN_FADE_IN_US;
    }
    return screen_transition == SCREEN_TRANSITION_FADE_OUT
        ? SCREEN_FADE_OUT_US
        : SCREEN_FADE_IN_US;
}

static void screen_transition_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (screen_transition == SCREEN_TRANSITION_RENDER_HIDDEN) {
        return;
    }
    if (screen_transition == SCREEN_TRANSITION_IDLE) {
        stop_screen_transition_timer();
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    const int64_t duration_us =
        screen_transition_duration_us();
    float phase =
        (float)(now_us - screen_transition_started_us) /
        (float)duration_us;
    if (phase < 0.0f) {
        phase = 0.0f;
    } else if (phase > 1.0f) {
        phase = 1.0f;
    }
    const float eased = screen_transition_smoothstep(phase);
    if (screen_transition_visual ==
        SCREEN_TRANSITION_GMT_INNER) {
        if (screen_transition_inner_mask != NULL) {
            const float opacity =
                screen_transition == SCREEN_TRANSITION_FADE_OUT
                    ? eased * 255.0f
                    : (1.0f - eased) * 255.0f;
            lv_obj_set_style_bg_opa(
                screen_transition_inner_mask,
                (lv_opa_t)lroundf(opacity),
                0);
        }
    } else {
        const float floor_brightness = fminf(
            SCREEN_TRANSITION_FLOOR_PERCENT,
            (float)screen_transition_target_brightness);
        const float brightness_range =
            (float)screen_transition_target_brightness -
            floor_brightness;
        float brightness = floor_brightness;
        if (screen_transition == SCREEN_TRANSITION_FADE_OUT) {
            brightness += (1.0f - eased) * brightness_range;
        } else {
            /* Use the exact same timing and easing in reverse so the two
             * halves read as one balanced transition around the hidden
             * render frame. */
            brightness += eased * brightness_range;
        }
        const uint8_t raw_brightness = (uint8_t)lroundf(
            brightness * 255.0f / 100.0f);
        (void)app_display_brightness_raw_set(raw_brightness);
    }

    if (now_us - screen_transition_started_us < duration_us) {
        return;
    }

    if (screen_transition == SCREEN_TRANSITION_FADE_OUT) {
        if (screen_transition_visual ==
            SCREEN_TRANSITION_GMT_INNER) {
            if (screen_transition_inner_mask != NULL) {
                lv_obj_set_style_bg_opa(
                    screen_transition_inner_mask,
                    LV_OPA_COVER,
                    0);
            }
        } else {
            const float floor_brightness = fminf(
                SCREEN_TRANSITION_FLOOR_PERCENT,
                (float)screen_transition_target_brightness);
            const uint8_t floor_raw = (uint8_t)lroundf(
                floor_brightness * 255.0f / 100.0f);
            (void)app_display_brightness_raw_set(floor_raw);
        }
        screen_transition = SCREEN_TRANSITION_RENDER_HIDDEN;
        render_requested = true;
        return;
    }

    if (screen_transition_visual ==
        SCREEN_TRANSITION_GMT_INNER) {
        if (screen_transition_inner_mask != NULL) {
            lv_obj_delete(screen_transition_inner_mask);
            screen_transition_inner_mask = NULL;
        }
    } else {
        (void)bsp_display_brightness_set(
            screen_transition_target_brightness);
    }
    screen_transition = SCREEN_TRANSITION_IDLE;
    stop_screen_transition_timer();
    if (dock_mode_enabled && dock_waiting_for_screen_render) {
        dock_waiting_for_screen_render = false;
        dock_deadline_us =
            now_us +
            (int64_t)dock_visible_duration_seconds() * 1000000LL;
    }
}
#endif

static void cancel_screen_transition(bool restore_brightness)
{
#ifndef AIRSPACE_DESKTOP_RENDERER
    if (screen_transition == SCREEN_TRANSITION_IDLE) {
        return;
    }
    screen_transition = SCREEN_TRANSITION_IDLE;
    stop_screen_transition_timer();
    if (screen_transition_inner_mask != NULL) {
        lv_obj_delete(screen_transition_inner_mask);
        screen_transition_inner_mask = NULL;
    }
    if (restore_brightness &&
        screen_transition_visual ==
            SCREEN_TRANSITION_FULL_DISPLAY) {
        (void)bsp_display_brightness_set(
            screen_transition_target_brightness);
    }
#else
    (void)restore_brightness;
#endif
}

static void begin_screen_transition(void)
{
#ifndef AIRSPACE_DESKTOP_RENDERER
    app_config_t cfg;
    app_config_get(&cfg);
    screen_transition_target_brightness =
        cfg.display_brightness_percent >= 10 &&
                cfg.display_brightness_percent <= 100
            ? cfg.display_brightness_percent
            : 100;
    cancel_screen_transition(false);
    screen_transition_visual =
        SCREEN_TRANSITION_FULL_DISPLAY;
    screen_transition_dock_timing = dock_mode_enabled;
    screen_transition = SCREEN_TRANSITION_FADE_OUT;
    screen_transition_started_us = esp_timer_get_time();
    render_requested = false;
    screen_transition_timer = lv_timer_create(
        screen_transition_timer_cb,
        SCREEN_TRANSITION_FRAME_MS,
        NULL);
    if (screen_transition_timer == NULL) {
        ESP_LOGW(TAG, "Could not create page transition timer");
        screen_transition = SCREEN_TRANSITION_IDLE;
        (void)bsp_display_brightness_set(
            screen_transition_target_brightness);
        render_requested = true;
    }
#else
    render_requested = true;
#endif
}

static void begin_gmt_inner_transition(void)
{
#ifndef AIRSPACE_DESKTOP_RENDERER
    app_config_t cfg;
    app_config_get(&cfg);
    screen_transition_target_brightness =
        cfg.display_brightness_percent >= 10 &&
                cfg.display_brightness_percent <= 100
            ? cfg.display_brightness_percent
            : 100;
    cancel_screen_transition(false);
    /* Compose the destination into a second PSRAM frame before the fade.
     * The visible dial keeps its own buffer, so fading out still reveals the
     * old view.  At full cover render_gmt() promotes the ready frame with a
     * descriptor swap instead of rebuilding the clock object-by-object. */
    (void)prepare_gmt_transition_snapshot();
    screen_transition_visual =
        SCREEN_TRANSITION_GMT_INNER;
    screen_transition_dock_timing = false;
    if (!create_gmt_inner_transition_mask(
            LV_OPA_TRANSP)) {
        begin_screen_transition();
        return;
    }
    screen_transition = SCREEN_TRANSITION_FADE_OUT;
    screen_transition_started_us = esp_timer_get_time();
    render_requested = false;
    screen_transition_timer = lv_timer_create(
        screen_transition_timer_cb,
        SCREEN_TRANSITION_FRAME_MS,
        NULL);
    if (screen_transition_timer == NULL) {
        ESP_LOGW(TAG, "Could not create GMT inner transition timer");
        screen_transition = SCREEN_TRANSITION_IDLE;
        lv_obj_delete(screen_transition_inner_mask);
        screen_transition_inner_mask = NULL;
        render_requested = true;
    }
#else
    render_requested = true;
#endif
}

static void render_dock_feedback(void)
{
    draw_shell();
    const uint32_t accent = dock_feedback_on
        ? radar_palette_active->main
        : 0x737c77;
    ring(screen_root, 398, lv_color_hex(0x202522), 1, (lv_opa_t)105);
    ring(screen_root, 320, lv_color_hex(accent), 2, (lv_opa_t)80);
    dot(screen_root, CX, 156, 18, lv_color_hex(accent), LV_OPA_COVER);
    label(
        screen_root,
        "DOCK MODE",
        &lv_font_montserrat_20,
        lv_color_hex(0x9aa39e),
        LV_ALIGN_CENTER,
        0,
        -10);
    label(
        screen_root,
        dock_feedback_on ? "ON" : "OFF",
        &lv_font_montserrat_48,
        lv_color_hex(0xf5f6f5),
        LV_ALIGN_CENTER,
        0,
        38);
}

static int dock_visible_duration_seconds(void)
{
    app_config_t config;
    app_config_get(&config);
    if (dock_sequence_index >= 0 &&
        dock_sequence_index < config.dock_entry_count) {
        const int seconds =
            config.dock_entries[dock_sequence_index]
                .duration_seconds;
        if (seconds >= 5 && seconds <= 300) {
            return seconds;
        }
    }
    return 20;
}

static int dock_sequence_count(void)
{
    app_config_t config;
    app_config_get(&config);
    return config.dock_entry_count >= APP_DOCK_ENTRY_MIN &&
            config.dock_entry_count <= APP_DOCK_ENTRY_MAX
        ? config.dock_entry_count
        : 0;
}

static bool dock_sequence_entry_at(
    int index,
    dock_sequence_entry_t *entry)
{
    if (entry == NULL) {
        return false;
    }
    app_config_t config;
    app_config_get(&config);
    if (index < 0 || index >= config.dock_entry_count) {
        return false;
    }
    const app_dock_entry_t *configured =
        &config.dock_entries[index];
    *entry = (dock_sequence_entry_t) {
        .screen = SCREEN_RADAR,
        .duration_seconds = configured->duration_seconds,
        .variant = DOCK_VARIANT_DEFAULT,
    };
    switch ((app_dock_item_t)configured->item) {
    case APP_DOCK_TRACKER:
        entry->screen = SCREEN_TRACKED_FLIGHT;
        break;
    case APP_DOCK_WEATHER:
        entry->screen = SCREEN_WEATHER;
        break;
    case APP_DOCK_CLOCK:
        entry->screen = SCREEN_GMT;
        break;
    case APP_DOCK_SOLAR:
        entry->screen = SCREEN_SPACE;
        entry->variant = DOCK_VARIANT_SPACE_SOLAR;
        break;
    case APP_DOCK_ECLIPSE:
        entry->screen = SCREEN_SPACE;
        entry->variant = DOCK_VARIANT_SPACE_ECLIPSE;
        break;
    case APP_DOCK_MOON:
        entry->screen = SCREEN_SPACE;
        entry->variant = DOCK_VARIANT_SPACE_MOON;
        break;
    case APP_DOCK_ISS:
        entry->screen = SCREEN_SPACE;
        entry->variant = DOCK_VARIANT_SPACE_ISS;
        break;
    case APP_DOCK_LAUNCH_NASA:
        entry->screen = SCREEN_LAUNCH;
        entry->variant = DOCK_VARIANT_LAUNCH_NASA;
        break;
    case APP_DOCK_LAUNCH_ROSCOSMOS:
        entry->screen = SCREEN_LAUNCH;
        entry->variant = DOCK_VARIANT_LAUNCH_ROSCOSMOS;
        break;
    case APP_DOCK_LAUNCH_SPACEX:
        entry->screen = SCREEN_LAUNCH;
        entry->variant = DOCK_VARIANT_LAUNCH_SPACEX;
        break;
    case APP_DOCK_TRACKER_FRAME:
        entry->screen = SCREEN_TRACKED_FLIGHT;
        entry->variant = DOCK_VARIANT_TRACKER_FRAME;
        break;
    case APP_DOCK_TRACKER_DETAIL:
        entry->screen = SCREEN_TRACKED_FLIGHT;
        entry->variant = DOCK_VARIANT_TRACKER_DETAIL;
        break;
    case APP_DOCK_WEATHER_FORECAST:
        entry->screen = SCREEN_WEATHER;
        entry->variant = DOCK_VARIANT_WEATHER_FORECAST;
        break;
    case APP_DOCK_WEATHER_RADAR:
        entry->screen = SCREEN_WEATHER;
        entry->variant = DOCK_VARIANT_WEATHER_RADAR;
        break;
    case APP_DOCK_CLOCK_WORLD:
        entry->screen = SCREEN_GMT;
        entry->variant = DOCK_VARIANT_CLOCK_WORLD;
        break;
    case APP_DOCK_LAUNCH_NASA_MAP:
        entry->screen = SCREEN_LAUNCH;
        entry->variant = DOCK_VARIANT_LAUNCH_NASA_MAP;
        break;
    case APP_DOCK_LAUNCH_ROSCOSMOS_MAP:
        entry->screen = SCREEN_LAUNCH;
        entry->variant = DOCK_VARIANT_LAUNCH_ROSCOSMOS_MAP;
        break;
    case APP_DOCK_LAUNCH_SPACEX_MAP:
        entry->screen = SCREEN_LAUNCH;
        entry->variant = DOCK_VARIANT_LAUNCH_SPACEX_MAP;
        break;
    case APP_DOCK_RADAR:
        entry->screen = SCREEN_RADAR;
        break;
    default:
        return false;
    }
    return true;
}

static void dock_mark_visible_state_changed(void)
{
    if (!dock_mode_enabled || dock_feedback_visible) {
        return;
    }
    dock_deadline_us = 0;
    dock_waiting_for_screen_render = true;
}

static void dock_activate_step(int index)
{
    const int count = dock_sequence_count();
    if (count <= 0) {
        return;
    }
    index %= count;
    if (index < 0) {
        index += count;
    }
    dock_sequence_entry_t entry;
    if (!dock_sequence_entry_at(index, &entry)) {
        return;
    }

    dock_sequence_index = index;
    const app_screen_t previous_screen = current_screen;
    dock_weather_radar_autoplay_pending = false;
    set_screen(entry.screen);

    if (entry.screen == SCREEN_TRACKED_FLIGHT) {
        if (entry.variant == DOCK_VARIANT_TRACKER_FRAME) {
            tracked_flight_view = TRACKED_FLIGHT_FRAME;
            tracked_panels_visible = true;
            tracked_panels_entry_animation_pending = true;
        } else if (entry.variant == DOCK_VARIANT_TRACKER_DETAIL) {
            tracked_flight_view = TRACKED_FLIGHT_DETAIL;
            tracked_detail_entry_animation_pending = true;
        }
    } else if (entry.screen == SCREEN_WEATHER) {
        if (entry.variant == DOCK_VARIANT_WEATHER_FORECAST) {
            weather_forecast_visible = true;
            weather_radar_visible = false;
        } else if (entry.variant == DOCK_VARIANT_WEATHER_RADAR) {
            show_weather_radar();
            dock_weather_radar_autoplay_pending = true;
        }
    } else if (entry.screen == SCREEN_GMT) {
        clock_digital_mode =
            entry.variant == DOCK_VARIANT_CLOCK_WORLD;
    } else if (entry.screen == SCREEN_SPACE) {
        space_solar_intro_active = false;
        space_solar_intro_start_us = 0;
        space_planet_annotation_mode = 0;
        if (entry.variant == DOCK_VARIANT_SPACE_MOON) {
            select_space_view(SPACE_VIEW_MOON, false);
        } else if (entry.variant == DOCK_VARIANT_SPACE_ISS) {
            select_space_view(SPACE_VIEW_ISS, false);
            const app_data_status_t *data = ui_data_status();
            if (data == NULL || !data->iss_valid) {
                app_data_request_refresh();
            }
        } else if (
            entry.variant == DOCK_VARIANT_SPACE_ECLIPSE) {
            select_space_view(SPACE_VIEW_ECLIPSE, false);
        } else {
            select_space_view(SPACE_VIEW_SOLAR, false);
        }
    } else if (
        entry.screen == SCREEN_LAUNCH) {
        const bool map_view =
            entry.variant == DOCK_VARIANT_LAUNCH_NASA_MAP ||
            entry.variant == DOCK_VARIANT_LAUNCH_ROSCOSMOS_MAP ||
            entry.variant == DOCK_VARIANT_LAUNCH_SPACEX_MAP;
        launch_view = map_view
            ? LAUNCH_VIEW_MAP
            : LAUNCH_VIEW_MAIN;
        if (entry.variant == DOCK_VARIANT_LAUNCH_NASA ||
            entry.variant == DOCK_VARIANT_LAUNCH_NASA_MAP) {
            launch_selected_agency =
                APP_REMOTE_LAUNCH_NASA;
        } else if (
            entry.variant == DOCK_VARIANT_LAUNCH_ROSCOSMOS ||
            entry.variant == DOCK_VARIANT_LAUNCH_ROSCOSMOS_MAP) {
            launch_selected_agency =
                APP_REMOTE_LAUNCH_ROSCOSMOS;
        } else {
            launch_selected_agency =
                APP_REMOTE_LAUNCH_SPACEX;
        }
        launch_animation_ticks = 0;
        launch_countdown_second_seen = -1;
    }
    if (previous_screen == entry.screen) {
#ifndef AIRSPACE_DESKTOP_RENDERER
        begin_screen_transition();
#else
        render_requested = true;
#endif
    } else if (!screen_transition_active()) {
        render_requested = true;
    }
    dock_waiting_for_screen_render = true;
    dock_deadline_us = 0;
    ESP_LOGI(
        TAG,
        "Dock step %d/%d screen=%d duration=%ds",
        index + 1,
        count,
        (int)entry.screen,
        entry.duration_seconds);
}

static void toggle_dock_mode(void)
{
    cancel_screen_transition(true);
    dock_mode_enabled = !dock_mode_enabled;
    if (!dock_mode_enabled) {
        dock_weather_radar_autoplay_pending = false;
    }
    dock_feedback_visible = true;
    dock_feedback_on = dock_mode_enabled;
    dock_feedback_deadline_us = esp_timer_get_time() + DOCK_FEEDBACK_US;
    dock_deadline_us = 0;
    dock_waiting_for_screen_render = false;
    if (dock_mode_enabled) {
        dock_sequence_index = 0;
    }
    render_requested = true;
    ESP_LOGI(TAG, "Dock Mode %s", dock_mode_enabled ? "ON" : "OFF");
}

static void open_power_menu(void)
{
    cancel_screen_transition(true);
    power_menu_visible = true;
    power_menu_deadline_us = esp_timer_get_time() + POWER_MENU_TIMEOUT_US;
    power_menu_opening_release_guard = top_button_held;
    boot_release_guard = boot_was_pressed;
    touch_release_guard = touch_was_pressed;
    radar_range_menu_open = false;
    render_requested = true;
    ESP_LOGI(TAG, "Power menu opened after %.1fs PWR hold", (double)POWER_MENU_HOLD_US / 1000000.0);
}

static void dismiss_power_menu(void)
{
    if (!power_menu_visible) {
        return;
    }
    power_menu_visible = false;
    power_menu_deadline_us = 0;
    power_menu_opening_release_guard = false;
    top_release_guard = top_button_held;
    boot_release_guard = boot_was_pressed;
    touch_release_guard = touch_was_pressed;
    render_requested = true;
    ESP_LOGI(TAG, "Power menu dismissed");
}

static void enter_display_sleep(void)
{
    bool network_suspended = false;
    cancel_screen_transition(true);
    power_menu_visible = false;
    power_menu_deadline_us = 0;
    power_menu_opening_release_guard = false;
#ifndef AIRSPACE_DESKTOP_RENDERER
    display_sleep_brightness = bsp_display_brightness_get();
    if (display_sleep_brightness < 1) {
        display_sleep_brightness = 100;
    }
    app_data_set_paused(true);
    network_suspended = app_network_suspend();
    lv_anim_delete_all();
    (void)bsp_display_brightness_set(0);
    const esp_err_t panel_result = app_display_panel_on_off(false);
    if (panel_result != ESP_OK) {
        ESP_LOGW(TAG, "CO5300 display-off failed: %s", esp_err_to_name(panel_result));
        (void)bsp_display_brightness_set(display_sleep_brightness);
        if (network_suspended) {
            (void)app_network_resume();
        }
        app_data_set_paused(false);
        render_requested = true;
        return;
    }
#endif
    display_sleeping = true;
    radar_sweep_epoch_us = 0;
    radar_sweep_last_tick_us = 0;
    ESP_LOGI(
        TAG,
        "Sleep: CO5300 off, Wi-Fi %s, entering 500ms PWR-polled light sleep",
        network_suspended ? "off" : "unchanged");

#ifndef AIRSPACE_DESKTOP_RENDERER
    /* AXP2101's PWR key is not routed to an ESP32 GPIO on this board. Its
     * key events are, however, latched in REG49 until they are cleared. A
     * low-duty-cycle RTC timer therefore lets us preserve RAM while polling
     * only that register, without leaving the CPU and Wi-Fi active. */
    (void)take_top_power_button_events();
    esp_err_t sleep_result = esp_sleep_enable_timer_wakeup(
        DISPLAY_SLEEP_PWR_POLL_US);
    if (sleep_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Could not configure light-sleep timer: %s; using idle fallback",
            esp_err_to_name(sleep_result));
        return;
    }

    while (display_sleeping) {
        sleep_result = esp_light_sleep_start();
        if (sleep_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Light sleep failed: %s; using idle fallback",
                esp_err_to_name(sleep_result));
            break;
        }

        const axp_power_key_events_t wake_events =
            take_top_power_button_events();
        const bool pwr_wake =
            wake_events.press_edge ||
            wake_events.release_edge ||
            wake_events.short_press ||
            wake_events.long_press;
        if (!pwr_wake) {
            continue;
        }

        top_button_held =
            wake_events.press_edge && !wake_events.release_edge;
        wake_display();
        top_release_guard = top_button_held;
        boot_release_guard = false;
        touch_release_guard = false;
        boot_press_started_us = 0;
        top_button_pressed_us = 0;
        ESP_LOGI(TAG, "PWR event consumed as light-sleep wake input");
    }
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
#endif
}

static void wake_display(void)
{
    if (!display_sleeping) {
        return;
    }
#ifndef AIRSPACE_DESKTOP_RENDERER
    const esp_err_t panel_result = app_display_panel_on_off(true);
    if (panel_result != ESP_OK) {
        ESP_LOGW(TAG, "CO5300 display-on failed: %s", esp_err_to_name(panel_result));
    }
    (void)bsp_display_brightness_set(display_sleep_brightness);
    (void)app_network_resume();
    app_data_set_paused(false);
#endif
    display_sleeping = false;
    render_requested = true;
    dock_mark_visible_state_changed();
    ESP_LOGI(TAG, "Wake: panel and brightness restored to %d%%", display_sleep_brightness);
}

static void begin_power_off(void)
{
    cancel_screen_transition(true);
    power_menu_visible = false;
    power_menu_deadline_us = 0;
    power_menu_opening_release_guard = false;
    power_off_pending = true;
    power_off_deadline_us = esp_timer_get_time() + POWER_OFF_MESSAGE_US;
    splash_transition = SPLASH_TRANSITION_IDLE;
#ifndef AIRSPACE_DESKTOP_RENDERER
    app_data_set_paused(true);
#endif
    lv_anim_delete_all();
    render_requested = true;
    ESP_LOGI(TAG, "Software power-off requested");
}

static void draw_airplane(lv_obj_t *parent, int x, int y, int angle_deg, lv_color_t color)
{
    const float a = (float)angle_deg * DEG_TO_RAD;
    const float lateral = a + 3.14159265f / 2.0f;
    const float ca = cosf(a);
    const float sa = sinf(a);
    const float cw = cosf(lateral);
    const float sw = sinf(lateral);

    const int nose_x = x + (int)(ca * 11.0f);
    const int nose_y = y + (int)(sa * 11.0f);
    const int tail_x = x - (int)(ca * 9.0f);
    const int tail_y = y - (int)(sa * 9.0f);

    const int wing_root_x = x - (int)(ca * 1.0f);
    const int wing_root_y = y - (int)(sa * 1.0f);
    const int left_wing_x = x - (int)(ca * 3.0f) + (int)(cw * 8.0f);
    const int left_wing_y = y - (int)(sa * 3.0f) + (int)(sw * 8.0f);
    const int right_wing_x = x - (int)(ca * 3.0f) - (int)(cw * 8.0f);
    const int right_wing_y = y - (int)(sa * 3.0f) - (int)(sw * 8.0f);

    line(parent, tail_x, tail_y, nose_x, nose_y, color, 3, LV_OPA_90);
    line(parent, wing_root_x, wing_root_y, left_wing_x, left_wing_y, color, 3, LV_OPA_80);
    line(parent, wing_root_x, wing_root_y, right_wing_x, right_wing_y, color, 3, LV_OPA_80);
    line(parent, x - (int)(ca * 5.0f), y - (int)(sa * 5.0f), nose_x, nose_y,
         lv_color_hex(radar_palette_active->text), 1, LV_OPA_60);
    soft_blob(parent, x, y, 4, color, LV_OPA_COVER);
}

static lv_obj_t *draw_aircraft_map_icon(lv_obj_t *parent,
                                        const app_aircraft_t *aircraft,
                                        int x,
                                        int y,
                                        lv_color_t aircraft_color)
{
    const int holder_size = 76;
    const int holder_center = holder_size / 2;
    lv_obj_t *holder = lv_obj_create(parent);
    lv_obj_remove_style_all(holder);
    lv_obj_set_size(holder, holder_size, holder_size);
    lv_obj_set_pos(
        holder,
        x - holder_center,
        y - holder_center);
    lv_obj_remove_flag(
        holder,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *shadow = dot(
        holder,
        holder_center,
        holder_center + 2,
        8,
        lv_color_hex(0x000000),
        (lv_opa_t)185);
    lv_obj_set_style_shadow_color(
        shadow,
        lv_color_hex(0x000000),
        0);
    lv_obj_set_style_shadow_width(shadow, 30, 0);
    lv_obj_set_style_shadow_spread(shadow, 3, 0);
    lv_obj_set_style_shadow_opa(
        shadow,
        (lv_opa_t)205,
        0);

    const lv_image_dsc_t *icon = airspace_aircraft_map_icon_for_aircraft(aircraft);
    if (icon == NULL) {
        draw_airplane(
            holder,
            holder_center,
            holder_center,
            aircraft != NULL ? aircraft->track_deg - 90 : -90,
            aircraft_color);
        tracked_aircraft_image = NULL;
        return holder;
    }

    const int w = (int)icon->header.w;
    const int h = (int)icon->header.h;
    const int heading = aircraft != NULL
        ? (aircraft->track_valid ? aircraft->track_deg : aircraft->bearing_deg)
        : 0;

    lv_obj_t *img = lv_image_create(holder);
    lv_image_set_src(img, icon);
    lv_obj_set_pos(
        img,
        holder_center - w / 2,
        holder_center - h / 2);
    lv_image_set_pivot(img, w / 2, h / 2);
    lv_image_set_rotation(img, ((heading + 180) % 360) * 10);
    lv_image_set_antialias(img, true);
    lv_obj_set_style_image_recolor(img, aircraft_color, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(img, LV_OPA_COVER, 0);
    tracked_aircraft_image = img;
    return holder;
}

static lv_obj_t *draw_aircraft_detail_icon(lv_obj_t *parent,
                                           const app_aircraft_t *aircraft,
                                           int x,
                                           int y)
{
    const lv_image_dsc_t *source_icon = airspace_aircraft_detail_icon_for_aircraft(aircraft);
    const lv_image_dsc_t *icon = source_icon;
    if (icon == NULL) {
        draw_airplane(parent, x, y, aircraft != NULL ? aircraft->track_deg - 90 : -90, lv_color_hex(0xffffff));
        return NULL;
    }

    const size_t row_bytes = source_icon->header.stride;
    const size_t required_bytes = row_bytes * source_icon->header.h;
    const size_t pixel_bytes =
        source_icon->header.cf == LV_COLOR_FORMAT_A8
            ? 1U
            : (source_icon->header.cf ==
                   LV_COLOR_FORMAT_ARGB8888
                   ? 4U
                   : 0U);
    if (pixel_bytes > 0 &&
        row_bytes > 0 &&
        row_bytes >=
            (size_t)source_icon->header.w *
                pixel_bytes &&
        required_bytes <= source_icon->data_size) {
        if (flipped_detail_aircraft_capacity < required_bytes) {
            heap_caps_free(flipped_detail_aircraft_buffer);
            flipped_detail_aircraft_buffer = (uint8_t *)heap_caps_malloc(
                required_bytes,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
            );
            flipped_detail_aircraft_capacity =
                flipped_detail_aircraft_buffer != NULL ? required_bytes : 0;
            flipped_detail_aircraft_source = NULL;
        }
        if (flipped_detail_aircraft_buffer != NULL) {
            if (flipped_detail_aircraft_source != source_icon->data) {
                const uint8_t *source = source_icon->data;
                for (uint32_t row = 0; row < source_icon->header.h; row++) {
                    const uint8_t *source_row =
                        source + (source_icon->header.h - 1U - row) * row_bytes;
                    uint8_t *destination_row =
                        flipped_detail_aircraft_buffer + row * row_bytes;
                    for (uint32_t column = 0; column < source_icon->header.w; column++) {
                        memcpy(
                            destination_row +
                                (size_t)column *
                                    pixel_bytes,
                            source_row +
                                (size_t)(
                                    source_icon->header.w -
                                    1U -
                                    column) *
                                    pixel_bytes,
                            pixel_bytes);
                    }
                    const size_t active_row_bytes =
                        (size_t)source_icon->header.w *
                        pixel_bytes;
                    if (row_bytes > active_row_bytes) {
                        memset(
                            destination_row +
                                active_row_bytes,
                            0,
                            row_bytes -
                                active_row_bytes);
                    }
                }
                flipped_detail_aircraft_source = source_icon->data;
            }
            flipped_detail_aircraft_image = *source_icon;
            flipped_detail_aircraft_image.data = flipped_detail_aircraft_buffer;
            flipped_detail_aircraft_image.data_size = (uint32_t)required_bytes;
            icon = &flipped_detail_aircraft_image;
        }
    }

    const int w = (int)icon->header.w;
    const int h = (int)icon->header.h;

    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, icon);
    lv_obj_set_pos(img, x - w / 2, y - h / 2);
    lv_image_set_pivot(img, w / 2, h / 2);
    lv_image_set_rotation(img, 0);
    // Keep the generated A8 side view at its native resolution. The source
    // pixels above are flipped on both axes without resampling, preserving
    // the sharp antialiased silhouette.
    lv_image_set_antialias(img, true);
    lv_obj_set_style_image_recolor(img, lv_color_hex(radar_palette_active->main), 0);
    lv_obj_set_style_image_recolor_opa(img, (lv_opa_t)185, 0);
    lv_obj_set_style_image_opa(img, LV_OPA_COVER, 0);
    return img;
}

static lv_obj_t *draw_airline_logo(lv_obj_t *parent,
                                   const char *iata,
                                   int x,
                                   int y,
                                   uint32_t *revision)
{
    size_t logo_len = 0;
    uint16_t logo_width = 0;
    uint16_t logo_height = 0;
    uint32_t logo_revision = 0;
    const uint8_t *logo = app_data_get_airline_logo(
        iata,
        &logo_len,
        &logo_width,
        &logo_height,
        &logo_revision
    );
    if (logo == NULL || logo_len == 0 || logo_width == 0 || logo_height == 0) {
        return NULL;
    }
    if (revision != NULL) {
        *revision = logo_revision;
    }

    memset(&airline_logo_image, 0, sizeof(airline_logo_image));
    airline_logo_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    airline_logo_image.header.cf = LV_COLOR_FORMAT_RGB565A8;
    airline_logo_image.header.flags = 0;
    airline_logo_image.header.w = logo_width;
    airline_logo_image.header.h = logo_height;
    airline_logo_image.header.stride = logo_width * 2;
    airline_logo_image.data_size = (uint32_t)logo_len;
    airline_logo_image.data = logo;

    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, &airline_logo_image);
    lv_obj_set_pos(image, x - logo_width / 2, y - logo_height / 2);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_image_opa(image, LV_OPA_COVER, 0);
    return image;
}

#define TRACKED_LOGO_GLOW_SIZE 78

static lv_obj_t *tracked_logo_radial_glow(lv_obj_t *parent,
                                          int center_x,
                                          int center_y)
{
    if (tracked_logo_glow_buffer == NULL) {
        tracked_logo_glow_buffer = heap_caps_malloc(
            TRACKED_LOGO_GLOW_SIZE * TRACKED_LOGO_GLOW_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (tracked_logo_glow_buffer == NULL) {
            return NULL;
        }

        const float center =
            ((float)TRACKED_LOGO_GLOW_SIZE - 1.0f) * 0.5f;
        const float radius = center;
        for (int y = 0; y < TRACKED_LOGO_GLOW_SIZE; y++) {
            for (int x = 0; x < TRACKED_LOGO_GLOW_SIZE; x++) {
                const float dx = (float)x - center;
                const float dy = (float)y - center;
                const float normalized =
                    fminf(1.0f, sqrtf(dx * dx + dy * dy) / radius);
                const float remaining = 1.0f - normalized;
                const float smooth =
                    remaining * remaining *
                    (3.0f - 2.0f * remaining);
                tracked_logo_glow_buffer[
                    y * TRACKED_LOGO_GLOW_SIZE + x] =
                    (uint8_t)lroundf(82.0f * smooth);
            }
        }

        memset(
            &tracked_logo_glow_image,
            0,
            sizeof(tracked_logo_glow_image));
        tracked_logo_glow_image.header.magic =
            LV_IMAGE_HEADER_MAGIC;
        tracked_logo_glow_image.header.cf =
            LV_COLOR_FORMAT_A8;
        tracked_logo_glow_image.header.w =
            TRACKED_LOGO_GLOW_SIZE;
        tracked_logo_glow_image.header.h =
            TRACKED_LOGO_GLOW_SIZE;
        tracked_logo_glow_image.header.stride =
            TRACKED_LOGO_GLOW_SIZE;
        tracked_logo_glow_image.data_size =
            TRACKED_LOGO_GLOW_SIZE * TRACKED_LOGO_GLOW_SIZE;
        tracked_logo_glow_image.data =
            tracked_logo_glow_buffer;
    }

    lv_obj_t *glow = lv_image_create(parent);
    lv_image_set_src(glow, &tracked_logo_glow_image);
    lv_obj_set_pos(
        glow,
        center_x - TRACKED_LOGO_GLOW_SIZE / 2,
        center_y - TRACKED_LOGO_GLOW_SIZE / 2);
    lv_obj_set_style_image_recolor(
        glow,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(
        glow,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(glow, LV_OPA_COVER, 0);
    lv_obj_remove_flag(
        glow,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return glow;
}

static void tracked_attach_aircraft_identity(
    lv_obj_t *aircraft_holder,
    const app_remote_status_t *remote)
{
    if (aircraft_holder == NULL || remote == NULL) {
        return;
    }

    /*
     * The 6-to-12 route leaves a deliberate central lane between the two
     * instrument panels. Keep the identity elements in that lane and make
     * them children of the aircraft holder so interpolation moves the logo,
     * aircraft, and flight tag as a single unit.
     */
    const int left_extension = 58;
    const int original_x = lv_obj_get_x(aircraft_holder);
    const int original_y = lv_obj_get_y(aircraft_holder);
    const uint32_t child_count =
        lv_obj_get_child_count(aircraft_holder);
    for (uint32_t index = 0;
         index < child_count;
         index++) {
        lv_obj_t *child =
            lv_obj_get_child(aircraft_holder, index);
        lv_obj_set_x(
            child,
            lv_obj_get_x(child) + left_extension);
    }
    lv_obj_set_pos(
        aircraft_holder,
        original_x - left_extension,
        original_y);
    lv_obj_set_size(
        aircraft_holder,
        198,
        76);

    if (remote->tracked_flight_airline_iata[0] != '\0') {
        const int logo_center_x = 33;
        const int logo_center_y = 38;
        lv_obj_t *logo_glow = tracked_logo_radial_glow(
            aircraft_holder,
            logo_center_x,
            logo_center_y);

        uint32_t logo_revision = 0;
        lv_obj_t *logo = draw_airline_logo(
            aircraft_holder,
            remote->tracked_flight_airline_iata,
            logo_center_x,
            logo_center_y,
            &logo_revision);
        if (logo != NULL) {
            tracked_logo_revision_seen = logo_revision;
        } else if (logo_glow != NULL) {
            lv_obj_delete(logo_glow);
        }
    }

    const char *flight_number =
        remote->tracked_flight_number[0] != '\0'
            ? remote->tracked_flight_number
            : "-";
    int flight_text_width = 6;
    for (size_t index = 0;
         flight_number[index] != '\0';
         index++) {
        const uint32_t current =
            (uint8_t)flight_number[index];
        const uint32_t next =
            flight_number[index + 1] != '\0'
                ? (uint8_t)flight_number[index + 1]
                : 0;
        flight_text_width += lv_font_get_glyph_width(
            &lv_font_montserrat_14,
            current,
            next);
    }
    /*
     * Keep the familiar compact badge for short callsigns, but let longer
     * commercial flight numbers grow leftward. Anchoring the right edge
     * preserves the aircraft/tag composition and avoids pushing the badge
     * into the curved screen edge.
     */
    int tag_width = flight_text_width + 14;
    if (tag_width < 68) {
        tag_width = 68;
    } else if (tag_width > 96) {
        tag_width = 96;
    }
    const int tag_right = 198;
    const int tag_x = tag_right - tag_width;
    const int tag_height = 26;
    const lv_color_t airport_yellow =
        lv_color_hex(0xffc72c);
    lv_obj_t *tag = card(
        aircraft_holder,
        tag_x,
        25,
        tag_width,
        tag_height,
        lv_color_hex(0x010201),
        (lv_opa_t)238,
        airport_yellow,
        LV_OPA_COVER,
        3);
    lv_obj_remove_flag(
        tag,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(tag, 1, 0);
    lv_obj_set_style_shadow_color(
        tag,
        lv_color_hex(0x000000),
        0);
    lv_obj_set_style_shadow_width(tag, 9, 0);
    lv_obj_set_style_shadow_opa(
        tag,
        (lv_opa_t)180,
        0);
    lv_obj_t *flight_label = label_box(
        tag,
        flight_number,
        &lv_font_montserrat_14,
        airport_yellow,
        LV_ALIGN_CENTER,
        0,
        0,
        tag_width - 8);
    lv_obj_set_style_text_align(
        flight_label,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_set_style_text_letter_space(
        flight_label,
        0,
        0);
    lv_obj_set_height(flight_label, 20);
    lv_label_set_long_mode(
        flight_label,
        LV_LABEL_LONG_CLIP);
}

static const uint8_t *prepare_padded_map_tiles(uint8_t map_kind, size_t map_len, const uint8_t *map, uint32_t revision)
{
    if (map == NULL || map_len != APP_REMOTE_MAP_BYTES) {
        return NULL;
    }

    const size_t padded_bytes = MAP_TILE_COUNT * MAP_TILE_HEIGHT * MAP_TILE_STRIDE_BYTES;
    if (padded_map_buffer == NULL) {
        padded_map_buffer = (uint8_t *)heap_caps_aligned_alloc(
            128,
            padded_bytes,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (padded_map_buffer == NULL) {
            ESP_LOGW(TAG, "No PSRAM for padded map copy");
            return NULL;
        }
    }

    if (padded_map_revision_seen != revision || padded_map_kind_seen != map_kind) {
        const size_t row_bytes = APP_REMOTE_MAP_WIDTH * 2;
        for (int y = 0; y < APP_REMOTE_MAP_HEIGHT; y++) {
            uint8_t *dst = padded_map_buffer + (size_t)y * MAP_TILE_STRIDE_BYTES;
            const uint8_t *src = map + (size_t)y * row_bytes;
            memcpy(dst, src, row_bytes);
            memset(dst + row_bytes, 0, MAP_TILE_STRIDE_BYTES - row_bytes);
        }

        const int padded_rows = MAP_TILE_COUNT * MAP_TILE_HEIGHT;
        for (int y = APP_REMOTE_MAP_HEIGHT; y < padded_rows; y++) {
            memset(padded_map_buffer + (size_t)y * MAP_TILE_STRIDE_BYTES, 0, MAP_TILE_STRIDE_BYTES);
        }

        padded_map_revision_seen = revision;
        padded_map_kind_seen = map_kind;
        ESP_LOGI(TAG, "Padded map copy refreshed: kind=%u rev=%lu stride=%d bytes",
                 (unsigned)map_kind, (unsigned long)revision, MAP_TILE_STRIDE_BYTES);
    }

    return padded_map_buffer;
}

static void draw_pushed_map_tiles(const uint8_t *map)
{
    for (int tile = 0; tile < MAP_TILE_COUNT; tile++) {
        const int y = tile * MAP_TILE_HEIGHT;
        const int tile_h = (APP_REMOTE_MAP_HEIGHT - y) > MAP_TILE_HEIGHT
            ? MAP_TILE_HEIGHT
            : (APP_REMOTE_MAP_HEIGHT - y);
        if (tile_h <= 0) {
            break;
        }

        lv_image_dsc_t *tile_dsc = &pushed_map_tiles[tile];
        memset(tile_dsc, 0, sizeof(*tile_dsc));
        tile_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
        tile_dsc->header.cf = LV_COLOR_FORMAT_RGB565;
        tile_dsc->header.flags = 0;
        tile_dsc->header.w = APP_REMOTE_MAP_WIDTH;
        tile_dsc->header.h = tile_h;
        tile_dsc->header.stride = MAP_TILE_STRIDE_BYTES;
        tile_dsc->data_size = tile_h * MAP_TILE_STRIDE_BYTES;
        tile_dsc->data = map + (size_t)y * MAP_TILE_STRIDE_BYTES;

        lv_obj_t *img = lv_image_create(screen_root);
        lv_image_set_src(img, tile_dsc);
        lv_obj_set_pos(img, 0, y);
        lv_obj_set_size(img, APP_REMOTE_MAP_WIDTH, tile_h);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
        pushed_map_tile_objects[tile] = img;
    }
}

#if RADAR_SWEEP_ENABLED
static void draw_radar_sweep_sector(lv_layer_t *layer, float trailing_deg, float leading_deg, lv_opa_t opacity)
{
    const float trailing = trailing_deg * DEG_TO_RAD;
    const float leading = leading_deg * DEG_TO_RAD;
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.base.layer = layer;
    dsc.color = lv_color_hex(radar_palette_active->sweep);
    dsc.opa = opacity;
    dsc.p[0].x = CX;
    dsc.p[0].y = CY;
    dsc.p[1].x = CX + RADAR_SWEEP_RADIUS_PX * cosf(trailing);
    dsc.p[1].y = CY + RADAR_SWEEP_RADIUS_PX * sinf(trailing);
    dsc.p[2].x = CX + RADAR_SWEEP_RADIUS_PX * cosf(leading);
    dsc.p[2].y = CY + RADAR_SWEEP_RADIUS_PX * sinf(leading);
    lv_draw_triangle(layer, &dsc);
}

static int clamp_screen_coord(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value >= SCREEN_SIZE) {
        return SCREEN_SIZE - 1;
    }
    return value;
}

static void radar_sweep_band_area(float trailing_deg,
                                  float leading_deg,
                                  float inner_radius,
                                  float outer_radius,
                                  lv_area_t *area)
{
    if (area == NULL) {
        return;
    }

    float min_x = (float)CX;
    float max_x = (float)CX;
    float min_y = (float)CY;
    float max_y = (float)CY;
    bool initialized = false;
    for (int sample = 0; sample <= 4; sample++) {
        const float fraction = (float)sample / 4.0f;
        const float angle = (trailing_deg + (leading_deg - trailing_deg) * fraction) * DEG_TO_RAD;
        const float ca = cosf(angle);
        const float sa = sinf(angle);
        const float radii[2] = {inner_radius, outer_radius};
        for (int radius_index = 0; radius_index < 2; radius_index++) {
            const float x = (float)CX + radii[radius_index] * ca;
            const float y = (float)CY + radii[radius_index] * sa;
            if (!initialized) {
                min_x = max_x = x;
                min_y = max_y = y;
                initialized = true;
            } else {
                min_x = fminf(min_x, x);
                max_x = fmaxf(max_x, x);
                min_y = fminf(min_y, y);
                max_y = fmaxf(max_y, y);
            }
        }
    }
    const int margin = 4;

    area->x1 = clamp_screen_coord((int)floorf(min_x) - margin);
    area->y1 = clamp_screen_coord((int)floorf(min_y) - margin);
    area->x2 = clamp_screen_coord((int)ceilf(max_x) + margin);
    area->y2 = clamp_screen_coord((int)ceilf(max_y) + margin);
}

static void radar_invalidate_sweep(float leading_deg)
{
    if (radar_sweep_obj == NULL) {
        return;
    }

    const float band_width = RADAR_SWEEP_RADIUS_PX / (float)RADAR_SWEEP_DIRTY_BANDS;
    for (int band = 0; band < RADAR_SWEEP_DIRTY_BANDS; band++) {
        const float inner_radius = fmaxf(0.0f, (float)band * band_width - 2.0f);
        const float outer_radius = fminf(RADAR_SWEEP_RADIUS_PX + 2.0f,
                                         (float)(band + 1) * band_width + 2.0f);
        lv_area_t area;
        radar_sweep_band_area(leading_deg - RADAR_SWEEP_SECTOR_DEG,
                              leading_deg,
                              inner_radius,
                              outer_radius,
                              &area);
        lv_obj_invalidate_area(radar_sweep_obj, &area);
    }
}

static double deg_to_rad_double(double deg)
{
    return deg * 3.14159265358979323846 / 180.0;
}

static double rad_to_deg_double(double rad)
{
    return rad * 180.0 / 3.14159265358979323846;
}

static double normalize_lon_deg(double lon)
{
    while (lon > 180.0) {
        lon -= 360.0;
    }
    while (lon < -180.0) {
        lon += 360.0;
    }
    return lon;
}

static float geo_distance_nm(double lat1, double lon1, double lat2, double lon2)
{
    const double r_nm = 3440.065;
    const double p1 = deg_to_rad_double(lat1);
    const double p2 = deg_to_rad_double(lat2);
    const double dp = deg_to_rad_double(lat2 - lat1);
    const double dl = deg_to_rad_double(lon2 - lon1);
    const double a = sin(dp / 2.0) * sin(dp / 2.0) +
                     cos(p1) * cos(p2) * sin(dl / 2.0) * sin(dl / 2.0);
    const double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return (float)(r_nm * c);
}

static int geo_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    const double p1 = deg_to_rad_double(lat1);
    const double p2 = deg_to_rad_double(lat2);
    const double dl = deg_to_rad_double(lon2 - lon1);
    const double y = sin(dl) * cos(p2);
    const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    int bearing = (int)lround(rad_to_deg_double(atan2(y, x)));
    return (bearing + 360) % 360;
}

static void project_lat_lon(double lat,
                            double lon,
                            int track_deg,
                            float speed_kt,
                            double age_seconds,
                            double *projected_lat,
                            double *projected_lon)
{
    const double r_nm = 3440.065;
    const double distance_nm = ((double)speed_kt * (double)age_seconds) / 3600.0;
    const double angular = distance_nm / r_nm;
    const double bearing = deg_to_rad_double((double)track_deg);
    const double p1 = deg_to_rad_double(lat);
    const double l1 = deg_to_rad_double(lon);

    const double p2 = asin(sin(p1) * cos(angular) + cos(p1) * sin(angular) * cos(bearing));
    const double l2 = l1 + atan2(sin(bearing) * sin(angular) * cos(p1),
                                 cos(angular) - sin(p1) * sin(p2));

    *projected_lat = rad_to_deg_double(p2);
    *projected_lon = normalize_lon_deg(rad_to_deg_double(l2));
}

static app_aircraft_t projected_aircraft_for_radar(const app_aircraft_t *aircraft,
                                                   double origin_lat,
                                                   double origin_lon)
{
    app_aircraft_t projected = *aircraft;
    double lat = aircraft->latitude;
    double lon = aircraft->longitude;

    struct timeval now = {0};
    gettimeofday(&now, NULL);
    if (now.tv_sec > 1700000000 &&
        aircraft->position_unix > 0 &&
        aircraft->track_valid &&
        aircraft->speed_kt > 15) {
        double age_seconds = (double)(now.tv_sec - aircraft->position_unix) +
                             (double)now.tv_usec / 1000000.0;
        if (age_seconds > 0.0) {
            if (age_seconds > (double)RADAR_AIRCRAFT_PROJECTION_MAX_SECONDS) {
                age_seconds = (double)RADAR_AIRCRAFT_PROJECTION_MAX_SECONDS;
            }
            project_lat_lon(aircraft->latitude,
                            aircraft->longitude,
                            aircraft->track_deg,
                            (float)aircraft->speed_kt,
                            age_seconds,
                            &lat,
                            &lon);
        }
    }

    projected.latitude = (float)lat;
    projected.longitude = (float)lon;
    projected.distance_nm = geo_distance_nm(origin_lat, origin_lon, lat, lon);
    projected.bearing_deg = geo_bearing_deg(origin_lat, origin_lon, lat, lon);
    return projected;
}

typedef struct {
    bool valid;
    app_aircraft_t aircraft;
    int x;
    int y;
    float precise_x;
    float precise_y;
} radar_aircraft_contact_t;

static bool radar_compute_aircraft_contact(const app_aircraft_t *aircraft,
                                           int aircraft_index,
                                           double origin_lat,
                                           double origin_lon,
                                           float range_miles,
                                           radar_aircraft_contact_t *contact)
{
    if (aircraft == NULL || contact == NULL || !aircraft->valid) {
        return false;
    }

    app_aircraft_t display_aircraft = projected_aircraft_for_radar(aircraft, origin_lat, origin_lon);
    const float radar_range_nm = fmaxf(1.0f, range_miles * STATUTE_MI_TO_NM);
    const double origin_lat_rad = origin_lat * (double)DEG_TO_RAD;
    const double nm_per_lon_degree = 60.0 * cos(origin_lat_rad);
    const double north_nm = ((double)display_aircraft.latitude - origin_lat) * 60.0;
    const double east_nm = ((double)display_aircraft.longitude - origin_lon) * nm_per_lon_degree;

    display_aircraft.distance_nm = sqrtf((float)(north_nm * north_nm + east_nm * east_nm));
    display_aircraft.bearing_deg = geo_bearing_deg(origin_lat, origin_lon, display_aircraft.latitude, display_aircraft.longitude);
    if (display_aircraft.distance_nm > radar_range_nm * 1.02f) {
        return false;
    }

    const float precise_x = (float)CX +
                            (float)((east_nm / (double)radar_range_nm) * (double)RADAR_RADIUS_PX);
    const float precise_y = (float)CY -
                            (float)((north_nm / (double)radar_range_nm) * (double)RADAR_RADIUS_PX);
    *contact = (radar_aircraft_contact_t) {
        .valid = true,
        .aircraft = display_aircraft,
        .x = (int)lroundf(precise_x),
        .y = (int)lroundf(precise_y),
        .precise_x = precise_x,
        .precise_y = precise_y,
    };
    contact->aircraft.valid = true;
    (void)aircraft_index;
    return true;
}

static bool radar_ensure_aircraft_sprite_pool(void)
{
    if (radar_aircraft_sprite_pool != NULL) {
        return true;
    }

    const size_t pool_bytes = (size_t)APP_MAX_AIRCRAFT * RADAR_AIRCRAFT_SPRITE_BYTES;
    radar_aircraft_sprite_pool = heap_caps_malloc(pool_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (radar_aircraft_sprite_pool == NULL) {
        radar_aircraft_sprite_pool = heap_caps_malloc(pool_bytes, MALLOC_CAP_8BIT);
    }
    if (radar_aircraft_sprite_pool == NULL) {
        ESP_LOGE(TAG, "Could not allocate %u-byte aircraft sprite pool", (unsigned)pool_bytes);
        return false;
    }
    memset(radar_aircraft_sprite_pool, 0, pool_bytes);
    ESP_LOGI(TAG, "Aircraft sprite pool: %u KB A8 in PSRAM", (unsigned)(pool_bytes / 1024));
    return true;
}

static void radar_initialize_sprite_descriptor(radar_aircraft_view_t *view)
{
    if (view == NULL || !radar_ensure_aircraft_sprite_pool()) {
        return;
    }

    const ptrdiff_t index = view - radar_aircraft_views;
    if (index < 0 || index >= APP_MAX_AIRCRAFT) {
        return;
    }

    memset(&view->sprite, 0, sizeof(view->sprite));
    view->sprite.header.magic = LV_IMAGE_HEADER_MAGIC;
    view->sprite.header.cf = LV_COLOR_FORMAT_A8;
    view->sprite.header.w = RADAR_AIRCRAFT_SPRITE_SIZE;
    view->sprite.header.h = RADAR_AIRCRAFT_SPRITE_SIZE;
    view->sprite.header.stride = RADAR_AIRCRAFT_SPRITE_SIZE;
    view->sprite.data_size = RADAR_AIRCRAFT_SPRITE_BYTES;
    view->sprite.data = radar_aircraft_sprite_pool + (size_t)index * RADAR_AIRCRAFT_SPRITE_BYTES;
}

static float radar_icon_coverage_at(const lv_image_dsc_t *icon, int x, int y)
{
    if (icon == NULL || icon->data == NULL ||
        x < 0 || y < 0 ||
        x >= (int)icon->header.w ||
        y >= (int)icon->header.h) {
        return 0.0f;
    }

    if (icon->header.cf == LV_COLOR_FORMAT_A8) {
        const uint8_t coverage =
            icon->data[
                (size_t)y * icon->header.stride +
                (size_t)x
            ];
        return (float)coverage / 255.0f;
    }
    if (icon->header.cf != LV_COLOR_FORMAT_ARGB8888) {
        return 0.0f;
    }

    const uint8_t *pixel = icon->data + (size_t)y * icon->header.stride + (size_t)x * 4;
    const uint8_t brightness = (uint8_t)fmaxf((float)pixel[0], fmaxf((float)pixel[1], (float)pixel[2]));
    const float glass_luminance = 0.58f + 0.42f * ((float)brightness / 255.0f);
    return ((float)pixel[3] / 255.0f) * glass_luminance;
}

static float radar_sample_icon_coverage(const lv_image_dsc_t *icon, float x, float y)
{
    const int x0 = (int)floorf(x);
    const int y0 = (int)floorf(y);
    const float fx = x - (float)x0;
    const float fy = y - (float)y0;
    const float c00 = radar_icon_coverage_at(icon, x0, y0);
    const float c10 = radar_icon_coverage_at(icon, x0 + 1, y0);
    const float c01 = radar_icon_coverage_at(icon, x0, y0 + 1);
    const float c11 = radar_icon_coverage_at(icon, x0 + 1, y0 + 1);
    const float top = c00 + (c10 - c00) * fx;
    const float bottom = c01 + (c11 - c01) * fx;
    return top + (bottom - top) * fy;
}

static void radar_hide_aircraft_view(radar_aircraft_view_t *view)
{
    if (view == NULL) {
        return;
    }
    view->visible = false;
    if (view->img != NULL) {
        lv_obj_add_flag(view->img, LV_OBJ_FLAG_HIDDEN);
    }
    if (view->label != NULL) {
        lv_obj_add_flag(view->label, LV_OBJ_FLAG_HIDDEN);
    }
}

static radar_aircraft_view_t *radar_existing_view_for_icao(const char *icao24)
{
    if (icao24 == NULL || icao24[0] == '\0') {
        return NULL;
    }

    for (int i = 0; i < APP_MAX_AIRCRAFT; i++) {
        radar_aircraft_view_t *view = &radar_aircraft_views[i];
        if (view->allocated && strcmp(view->icao24, icao24) == 0) {
            return view;
        }
    }
    return NULL;
}

static radar_aircraft_view_t *radar_view_for_aircraft(const app_aircraft_t *aircraft)
{
    if (aircraft == NULL || aircraft->icao24[0] == '\0') {
        return NULL;
    }

    radar_aircraft_view_t *empty = NULL;
    for (int i = 0; i < APP_MAX_AIRCRAFT; i++) {
        radar_aircraft_view_t *view = &radar_aircraft_views[i];
        if (view->allocated && strcmp(view->icao24, aircraft->icao24) == 0) {
            return view;
        }
        if (empty == NULL && !view->allocated) {
            empty = view;
        }
    }

    if (empty == NULL) {
        return NULL;
    }

    lv_obj_t *existing_img = empty->img;
    lv_obj_t *existing_label = empty->label;
    memset(empty, 0, sizeof(*empty));
    empty->img = existing_img;
    empty->label = existing_label;
    radar_initialize_sprite_descriptor(empty);
    empty->allocated = true;
    empty->w = RADAR_AIRCRAFT_SPRITE_SIZE;
    empty->h = RADAR_AIRCRAFT_SPRITE_SIZE;
    empty->sprite_dirty = true;
    snprintf(empty->icao24, sizeof(empty->icao24), "%s", aircraft->icao24);
    if (empty->img != NULL) {
        lv_obj_add_flag(empty->img, LV_OBJ_FLAG_HIDDEN);
    }
    if (empty->label != NULL) {
        lv_obj_add_flag(empty->label, LV_OBJ_FLAG_HIDDEN);
    }
    return empty;
}

static void radar_hide_stale_aircraft_views(const app_data_status_t *data)
{
    for (int i = 0; i < APP_MAX_AIRCRAFT; i++) {
        radar_aircraft_view_t *view = &radar_aircraft_views[i];
        if (!view->allocated) {
            continue;
        }
        bool found = false;
        if (data != NULL && data->aircraft_valid) {
            for (int j = 0; j < data->aircraft_count && j < APP_MAX_AIRCRAFT; j++) {
                if (data->aircraft[j].valid && strcmp(view->icao24, data->aircraft[j].icao24) == 0) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            radar_hide_aircraft_view(view);
            view->allocated = false;
            view->icao24[0] = '\0';
        }
    }
}

static void radar_update_aircraft_label(radar_aircraft_view_t *view, bool labels_enabled)
{
    if (view == NULL || radar_aircraft_layer == NULL) {
        return;
    }

    const char *flight_label = airspace_aircraft_flight_label_for_aircraft(&view->aircraft);
    const bool show_label = labels_enabled && view->commercial && flight_label[0] != '\0';
    if (!show_label) {
        if (view->label != NULL) {
            lv_obj_add_flag(view->label, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (view->label == NULL) {
        view->label = lv_label_create(radar_aircraft_layer);
        lv_obj_remove_flag(view->label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_font(view->label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_letter_space(view->label, 0, 0);
        lv_obj_set_style_text_color(view->label, lv_color_hex(radar_palette_active->text), 0);
        lv_obj_set_style_text_opa(view->label, (lv_opa_t)224, 0);
    }
    lv_label_set_text(view->label, flight_label);
    if (view->visible) {
        lv_obj_clear_flag(view->label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void radar_apply_aircraft_view_position(radar_aircraft_view_t *view, int x, int y)
{
    if (view == NULL) {
        return;
    }

    view->x = x;
    view->y = y;
    if (view->img != NULL) {
        lv_obj_set_pos(view->img, x - RADAR_AIRCRAFT_SPRITE_SIZE / 2,
                       y - RADAR_AIRCRAFT_SPRITE_SIZE / 2);
    }
    if (view->label != NULL) {
        lv_obj_set_pos(view->label, x + 16, y - 12);
    }
}

static bool radar_build_aircraft_sprite(radar_aircraft_view_t *view)
{
    if (view == NULL || !view->allocated || !view->sprite_dirty ||
        radar_aircraft_layer == NULL || !radar_ensure_aircraft_sprite_pool()) {
        return false;
    }

    if (view->sprite.data == NULL) {
        radar_initialize_sprite_descriptor(view);
    }
    uint8_t *sprite = (uint8_t *)view->sprite.data;
    if (sprite == NULL) {
        return false;
    }

    const lv_image_dsc_t *icon = airspace_aircraft_map_icon_for_aircraft(&view->aircraft);
    if (icon == NULL || icon->data == NULL) {
        return false;
    }
    view->icon = icon;

    const float center = ((float)RADAR_AIRCRAFT_SPRITE_SIZE - 1.0f) * 0.5f;
    const float angle = (float)((view->heading_deg + 180) % 360) * DEG_TO_RAD;
    const float ca = cosf(angle);
    const float sa = sinf(angle);
    const float opacity = view->commercial ? 1.0f : 0.75f;

    for (int y = 0; y < RADAR_AIRCRAFT_SPRITE_SIZE; y++) {
        for (int x = 0; x < RADAR_AIRCRAFT_SPRITE_SIZE; x++) {
            const float dx = (float)x - center;
            const float dy = (float)y - center;
            const float source_x = ca * dx + sa * dy + center;
            const float source_y = -sa * dx + ca * dy + center;
            float coverage = radar_sample_icon_coverage(icon, source_x, source_y) * opacity;
            if (coverage < 0.0f) {
                coverage = 0.0f;
            } else if (coverage > 1.0f) {
                coverage = 1.0f;
            }
            sprite[y * RADAR_AIRCRAFT_SPRITE_SIZE + x] = (uint8_t)lroundf(coverage * 255.0f);
        }
    }

    if (view->img == NULL) {
        view->img = lv_image_create(radar_aircraft_layer);
        lv_obj_remove_flag(view->img, LV_OBJ_FLAG_CLICKABLE);
        lv_image_set_antialias(view->img, false);
        lv_image_set_src(view->img, &view->sprite);
    }
    lv_obj_set_style_image_recolor(
        view->img,
        lv_color_hex(view->commercial
                         ? radar_palette_active->aircraft
                         : radar_palette_active->secondary_aircraft),
        0);
    lv_obj_set_style_image_recolor_opa(view->img, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(view->img, LV_OPA_COVER, 0);
    lv_obj_invalidate(view->img);
    view->sprite_dirty = false;
    radar_apply_aircraft_view_position(view, view->x, view->y);
    if (view->visible) {
        if (view->label != NULL) {
            lv_obj_clear_flag(view->label, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(view->img, LV_OBJ_FLAG_HIDDEN);
    }
    return true;
}

static void radar_build_one_pending_aircraft_sprite(void)
{
    for (int step = 0; step < APP_MAX_AIRCRAFT; step++) {
        const int index = (radar_aircraft_sprite_cursor + step) % APP_MAX_AIRCRAFT;
        radar_aircraft_view_t *view = &radar_aircraft_views[index];
        if (!view->allocated || !view->sprite_dirty) {
            continue;
        }
        radar_aircraft_sprite_cursor = (index + 1) % APP_MAX_AIRCRAFT;
        radar_build_aircraft_sprite(view);
        return;
    }
}

static void radar_sync_aircraft_views(const app_data_status_t *data,
                                      double origin_lat,
                                      double origin_lon)
{
    radar_hide_stale_aircraft_views(data);
    if (data == NULL || !data->aircraft_valid) {
        return;
    }

    app_config_t cfg;
    app_config_get(&cfg);
    const int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < data->aircraft_count && i < APP_MAX_AIRCRAFT; i++) {
        const app_aircraft_t *aircraft = &data->aircraft[i];
        radar_aircraft_view_t *view = radar_existing_view_for_icao(aircraft->icao24);
        const bool commercial =
            airspace_aircraft_likely_commercial(aircraft);
        if (radar_range_miles >= 49.5f && !commercial) {
            if (view != NULL) {
                radar_hide_aircraft_view(view);
            }
            continue;
        }
        const bool existing = view != NULL;
        if (view == NULL) {
            view = radar_view_for_aircraft(aircraft);
        }
        if (view == NULL) {
            continue;
        }

        const float previous_x = view->display_x;
        const float previous_y = view->display_y;
        const bool had_visible_position = existing && view->visible;
        const int heading = aircraft->track_valid ? aircraft->track_deg : aircraft->bearing_deg;
        const lv_image_dsc_t *icon = airspace_aircraft_map_icon_for_aircraft(aircraft);
        if (!existing || view->heading_deg != heading || view->commercial != commercial || view->icon != icon) {
            view->sprite_dirty = true;
        }

        view->aircraft = *aircraft;
        view->aircraft_index = i;
        view->heading_deg = heading;
        view->commercial = commercial;
        view->icon = icon;

        radar_aircraft_contact_t contact;
        if (!radar_compute_aircraft_contact(aircraft, i, origin_lat, origin_lon, radar_range_miles, &contact)) {
            radar_hide_aircraft_view(view);
            continue;
        }

        view->correction_x = had_visible_position ? previous_x - contact.precise_x : 0.0f;
        view->correction_y = had_visible_position ? previous_y - contact.precise_y : 0.0f;
        view->correction_epoch_us = now_us;
        view->display_x = contact.precise_x + view->correction_x;
        view->display_y = contact.precise_y + view->correction_y;
        view->visible = true;
        radar_apply_aircraft_view_position(view,
                                           (int)lroundf(view->display_x),
                                           (int)lroundf(view->display_y));
        if (view->img != NULL) {
            lv_obj_clear_flag(view->img, LV_OBJ_FLAG_HIDDEN);
        }
        radar_update_aircraft_label(view, cfg.aircraft_labels_enabled);
    }
}

static void radar_update_aircraft_animation(void)
{
    if (radar_aircraft_layer == NULL ||
        current_screen != SCREEN_RADAR ||
        aircraft_detail_active ||
        radar_range_menu_open) {
        return;
    }

    const app_data_status_t *data =
        ui_data_status();
    if (data == NULL) {
        return;
    }
    app_config_t cfg;
    app_config_get(&cfg);
    const double origin_lat = atof(cfg.location_lat);
    const double origin_lon = atof(cfg.location_lon);

    if (data->fetched_unix !=
        radar_aircraft_status_unix_seen) {
        radar_aircraft_status_unix_seen =
            data->fetched_unix;
        radar_sync_aircraft_views(
            data,
            origin_lat,
            origin_lon);
    }

    const int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < APP_MAX_AIRCRAFT; i++) {
        radar_aircraft_view_t *view = &radar_aircraft_views[i];
        if (!view->allocated) {
            continue;
        }

        radar_aircraft_contact_t contact;
        if (!radar_compute_aircraft_contact(&view->aircraft,
                                            view->aircraft_index,
                                            origin_lat,
                                            origin_lon,
                                            radar_range_miles,
                                            &contact)) {
            radar_hide_aircraft_view(view);
            continue;
        }

        float correction_scale = 0.0f;
        if (view->correction_epoch_us > 0) {
            const float correction_age =
                (float)(now_us - view->correction_epoch_us) / 1000000.0f;
            correction_scale = expf(-correction_age / RADAR_AIRCRAFT_CORRECTION_SECONDS);
            if (correction_scale < 0.005f) {
                correction_scale = 0.0f;
                view->correction_x = 0.0f;
                view->correction_y = 0.0f;
            }
        }

        view->display_x = contact.precise_x + view->correction_x * correction_scale;
        view->display_y = contact.precise_y + view->correction_y * correction_scale;
        const int next_x = (int)lroundf(view->display_x);
        const int next_y = (int)lroundf(view->display_y);
        view->visible = true;
        if (next_x != view->x || next_y != view->y) {
            radar_apply_aircraft_view_position(view, next_x, next_y);
            radar_aircraft_move_count++;
        }
        if (view->img != NULL) {
            lv_obj_clear_flag(view->img, LV_OBJ_FLAG_HIDDEN);
        }
        if (view->label != NULL && cfg.aircraft_labels_enabled && view->commercial) {
            lv_obj_clear_flag(view->label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    radar_build_one_pending_aircraft_sprite();
}

static void radar_sweep_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
        return;
    }

    lv_layer_t *layer = lv_event_get_layer(event);
    draw_radar_sweep_sector(layer, radar_sweep_angle_deg - RADAR_SWEEP_SECTOR_DEG, radar_sweep_angle_deg, (lv_opa_t)46);
}

static void create_radar_sweep(void)
{
    radar_sweep_obj = lv_obj_create(screen_root);
    lv_obj_remove_style_all(radar_sweep_obj);
    lv_obj_set_pos(radar_sweep_obj, 0, 0);
    lv_obj_set_size(radar_sweep_obj, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_remove_flag(radar_sweep_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(radar_sweep_obj, radar_sweep_event_cb, LV_EVENT_DRAW_MAIN, NULL);
}
#endif

static void draw_radar_range_menu_button(float miles, int cx, int cy, int w, int h)
{
    if (radar_range_button_count < RADAR_RANGE_BUTTON_COUNT) {
        radar_range_buttons[radar_range_button_count++] = (radar_range_button_t) {
            .valid = true,
            .x = cx - w / 2,
            .y = cy - h / 2,
            .w = w,
            .h = h,
            .miles = miles,
        };
    }

    const bool selected = fabsf(radar_range_miles - miles) < 0.1f;
    const int x = cx - w / 2;
    const int y = cy - h / 2;
    lv_obj_t *button = card(radar_menu_overlay,
                            x,
                            y,
                            w,
                            h,
                            selected ? lv_color_hex(0x172019) : lv_color_hex(0x070807),
                            selected ? (lv_opa_t)245 : (lv_opa_t)155,
                            lv_color_hex(0x000000),
                            LV_OPA_TRANSP,
                            h / 2);
    lv_obj_set_style_border_width(button, 0, 0);

    char value[12];
    snprintf(value, sizeof(value), "%d", (int)miles);
    label(button,
          value,
          &lv_font_montserrat_18,
          selected ? lv_color_hex(radar_palette_active->text) : lv_color_hex(0x858c86),
          LV_ALIGN_CENTER,
          0,
          0);
}

static const char *radar_location_short_label(const app_location_preset_t *preset)
{
    return preset != NULL ? preset->key : "";
}

static void draw_radar_location_button(const app_location_preset_t *preset, int cx, int cy)
{
    const int w = 90;
    const int h = 100;
    if (preset == NULL) {
        return;
    }
    const int x = cx - w / 2;
    const int y = cy - h / 2;
    if (radar_location_button_count < RADAR_LOCATION_BUTTON_COUNT) {
        radar_location_button_t *button = &radar_location_buttons[radar_location_button_count++];
        button->valid = true;
        button->x = x;
        button->y = y;
        button->w = w;
        button->h = h;
        snprintf(button->key, sizeof(button->key), "%s", preset->key);
    }

    app_config_t cfg;
    app_config_get(&cfg);
    const bool selected = strcasecmp(cfg.location_key, preset->key) == 0;
    lv_obj_t *button = lv_obj_create(radar_menu_overlay);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    if (selected) {
        lv_obj_t *selection_stroke = lv_obj_create(button);
        lv_obj_remove_style_all(selection_stroke);
        lv_obj_set_pos(selection_stroke, (w - 64) / 2, 0);
        lv_obj_set_size(selection_stroke, 64, 64);
        lv_obj_set_style_radius(selection_stroke, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(selection_stroke, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(selection_stroke, 2, 0);
        lv_obj_set_style_border_color(selection_stroke, lv_color_hex(radar_palette_active->text), 0);
        lv_obj_set_style_border_opa(selection_stroke, LV_OPA_COVER, 0);
        lv_obj_remove_flag(selection_stroke, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    const lv_image_dsc_t *icon = airspace_city_icon_for_key(preset->key);
    if (icon != NULL) {
        lv_obj_t *img = lv_image_create(button);
        lv_image_set_src(img, icon);
        lv_obj_set_pos(img, (w - (int)icon->header.w) / 2, 4);
        lv_obj_set_style_image_opa(img, selected ? LV_OPA_COVER : (lv_opa_t)220, 0);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t *code = label_box(button,
                               radar_location_short_label(preset),
                               &lv_font_montserrat_14,
                               selected ? lv_color_hex(radar_palette_active->text)
                                        : lv_color_hex(0xd2d6d3),
                               LV_ALIGN_TOP_LEFT,
                               0,
                               0,
                               w);
    lv_obj_set_pos(code, 0, 66);
    lv_obj_set_style_text_align(code, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(code, 1, 0);

    if (preset->label != NULL && preset->label[0] != '\0') {
        lv_obj_t *city = label_box(button,
                                   preset->label,
                                   &lv_font_montserrat_12,
                                   lv_color_hex(0xa9afaa),
                                   LV_ALIGN_TOP_LEFT,
                                   0,
                                   0,
                                   w);
        lv_obj_set_pos(city, 0, 84);
        lv_obj_set_style_text_align(city, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_letter_space(city, 0, 0);
        lv_obj_set_style_text_opa(city, LV_OPA_COVER, 0);
    }
}

static void draw_radar_toggle_pill(radar_range_button_t *hit_box,
                                   const char *title,
                                   bool enabled,
                                   int cx,
                                   int cy,
                                   int w,
                                   int h)
{
    const int x = cx - w / 2;
    const int y = cy - h / 2;
    if (hit_box != NULL) {
        *hit_box = (radar_range_button_t) {
            .valid = true,
            .x = x,
            .y = y,
            .w = w,
            .h = h,
            .miles = 0,
        };
    }

    lv_obj_t *button = card(radar_menu_overlay,
                            x,
                            y,
                            w,
                            h,
                            lv_color_hex(0x070807),
                            (lv_opa_t)220,
                            lv_color_hex(0x000000),
                            LV_OPA_TRANSP,
                            LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x343a35), 0);
    lv_obj_set_style_border_opa(button, (lv_opa_t)185, 0);
    label(button,
          title,
          &lv_font_montserrat_12,
          enabled ? lv_color_hex(0xf1fff3) : lv_color_hex(0xa0a8a2),
          LV_ALIGN_TOP_MID,
          0,
          18);
    dot(button,
        w / 2,
        h - 23,
        15,
        enabled ? lv_color_hex(radar_palette_active->main) : lv_color_hex(0x626863),
        LV_OPA_COVER);
}

static void draw_radar_labels_toggle(int cx, int cy, int w, int h)
{
    app_config_t cfg;
    app_config_get(&cfg);
    draw_radar_toggle_pill(&radar_labels_button, "CALLSIGN", cfg.aircraft_labels_enabled, cx, cy, w, h);
}

static void draw_radar_wedge_toggle(int cx, int cy, int w, int h)
{
    app_config_t cfg;
    app_config_get(&cfg);
    draw_radar_toggle_pill(&radar_wedge_button, "WEDGE", cfg.radar_wedge_enabled, cx, cy, w, h);
}

static void draw_radar_weather_units_switch(int cx, int cy, int w, int h)
{
    const int x = cx - w / 2;
    const int y = cy - h / 2;
    const int segment_w = w / APP_WEATHER_UNITS_COUNT;
    app_config_t cfg;
    app_config_get(&cfg);

    lv_obj_t *control = card(
        radar_menu_overlay,
        x,
        y,
        w,
        h,
        lv_color_hex(0x070807),
        (lv_opa_t)220,
        lv_color_hex(0x343a35),
        (lv_opa_t)185,
        LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(control, 1, 0);

    label(control,
          "UNITS",
          &lv_font_montserrat_12,
          lv_color_hex(0xf1fff3),
          LV_ALIGN_TOP_MID,
          0,
          18);

    for (int index = 0; index < APP_WEATHER_UNITS_COUNT; index++) {
        const bool selected = cfg.weather_units == index;
        const int segment_x = index * segment_w;
        radar_weather_unit_buttons[index] = (radar_range_button_t) {
            .valid = true,
            .x = x + segment_x,
            .y = y,
            .w = index == APP_WEATHER_UNITS_COUNT - 1
                ? w - segment_x
                : segment_w,
            .h = h,
            .miles = 0,
        };
        lv_obj_t *unit = label_box(
            control,
            index == APP_WEATHER_UNITS_FAHRENHEIT ? "F" : "C",
            &lv_font_montserrat_14,
            selected
                ? lv_color_hex(radar_palette_active->main)
                : lv_color_hex(0x747b76),
            LV_ALIGN_TOP_LEFT,
            0,
            0,
            segment_w);
        lv_obj_set_pos(unit, segment_x, h - 31);
        lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void add_radar_wifi_profile_hit_box(app_wifi_profile_id_t profile, int x, int y, int w, int h)
{
    if (radar_wifi_profile_button_count < APP_WIFI_PROFILE_COUNT) {
        radar_wifi_profile_buttons[radar_wifi_profile_button_count++] = (radar_wifi_profile_button_t) {
            .valid = true,
            .x = x,
            .y = y,
            .w = w,
            .h = h,
            .profile = profile,
        };
    }
}

static void draw_radar_wifi_profile_switch(int cx, int cy, int w, int h)
{
    const int x = cx - w / 2;
    const int y = cy - h / 2;
    const int segment_w = w / 2;
    add_radar_wifi_profile_hit_box(APP_WIFI_PROFILE_HOME, x, y, segment_w, h);
    add_radar_wifi_profile_hit_box(APP_WIFI_PROFILE_HOTSPOT, x + segment_w, y, w - segment_w, h);

    app_config_t cfg;
    app_config_get(&cfg);
    const bool home_selected = cfg.active_wifi_profile == APP_WIFI_PROFILE_HOME;
    lv_obj_t *control = card(radar_menu_overlay,
                            x,
                            y,
                            w,
                            h,
                            lv_color_hex(0x070807),
                            (lv_opa_t)220,
                            lv_color_hex(0x000000),
                            LV_OPA_TRANSP,
                            LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(control, 0, 0);

    const int thumb_size = h - 8;
    const int thumb_offset = (segment_w - thumb_size) / 2;
    lv_obj_t *thumb = card(control,
                           home_selected ? thumb_offset : segment_w + thumb_offset,
                           4,
                           thumb_size,
                           thumb_size,
                           lv_color_hex(0x121612),
                           LV_OPA_COVER,
                           lv_color_hex(0x000000),
                           LV_OPA_TRANSP,
                           LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(thumb, 0, 0);

    const lv_color_t selected_color = lv_color_hex(radar_palette_active->text);
    const lv_color_t idle_color = lv_color_hex(0x747b76);
    lv_obj_t *home_segment = lv_obj_create(control);
    lv_obj_remove_style_all(home_segment);
    lv_obj_set_pos(home_segment, 0, 0);
    lv_obj_set_size(home_segment, segment_w, h);
    lv_obj_remove_flag(home_segment, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    label(home_segment,
          LV_SYMBOL_WIFI,
          &lv_font_montserrat_24,
          home_selected ? selected_color : idle_color,
          LV_ALIGN_CENTER,
          0,
          -1);

    lv_obj_t *hotspot_segment = lv_obj_create(control);
    lv_obj_remove_style_all(hotspot_segment);
    lv_obj_set_pos(hotspot_segment, segment_w, 0);
    lv_obj_set_size(hotspot_segment, w - segment_w, h);
    lv_obj_remove_flag(hotspot_segment, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hotspot = lv_image_create(hotspot_segment);
    lv_image_set_src(hotspot, &airspace_menu_icon_hotspot);
    lv_obj_center(hotspot);
    lv_obj_set_style_image_recolor(hotspot, home_selected ? idle_color : selected_color, 0);
    lv_obj_set_style_image_recolor_opa(hotspot, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(hotspot, home_selected ? (lv_opa_t)180 : LV_OPA_COVER, 0);
    lv_obj_remove_flag(hotspot, LV_OBJ_FLAG_CLICKABLE);
}

static void draw_radar_accent_button(app_accent_color_t color, int cx, int cy)
{
    const int size = 54;
    const int x = cx - size / 2;
    const int y = cy - size / 2;
    if (radar_accent_button_count < RADAR_ACCENT_BUTTON_COUNT) {
        radar_accent_buttons[radar_accent_button_count++] = (radar_accent_button_t) {
            .valid = true,
            .x = x,
            .y = y,
            .w = size,
            .h = size,
            .color = color,
        };
    }

    app_config_t cfg;
    app_config_get(&cfg);
    const radar_palette_t *palette = palette_for_accent(color);
    const bool selected = cfg.accent_color == color;
    lv_obj_t *button = card(radar_menu_overlay,
                            x,
                            y,
                            size,
                            size,
                            lv_color_hex(0x080908),
                            selected ? (lv_opa_t)230 : (lv_opa_t)175,
                            lv_color_hex(0x000000),
                            LV_OPA_TRANSP,
                            LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(button, 0, 0);
    dot(button, size / 2, size / 2, 27, lv_color_hex(palette->main), LV_OPA_COVER);
    if (selected) {
        dot(button, size / 2, size / 2, 9, lv_color_hex(0xffffff), LV_OPA_COVER);
    }
}

static void draw_radar_page_indicator(void)
{
    const int y = 28;
    const int spacing = 20;
    const int first_x = CX - ((RADAR_MENU_PAGE_COUNT - 1) * spacing) / 2;
    for (int page = 0; page < RADAR_MENU_PAGE_COUNT; page++) {
        const int cx = first_x + page * spacing;
        radar_page_buttons[page] = (radar_page_button_t) {
            .valid = true,
            .x = cx - 12,
            .y = y - 12,
            .w = 24,
            .h = 24,
            .page = page,
        };
        dot(radar_menu_overlay,
            cx,
            y,
            page == radar_menu_page ? 8 : 5,
            page == radar_menu_page ? lv_color_hex(radar_palette_active->text) : lv_color_hex(0x606761),
            page == radar_menu_page ? LV_OPA_COVER : (lv_opa_t)150);
    }
}

static lv_color_t radar_battery_level_color(int percent)
{
    if (percent < 0) {
        return lv_color_hex(0xaeb5b0);
    }
    if (percent < 25) {
        return lv_color_hex(0xff453a);
    }
    if (percent < 50) {
        return lv_color_hex(0xffcc00);
    }
    return lv_color_hex(0x30d158);
}

static void draw_radar_battery_indicator(void)
{
    app_power_battery_status_t battery;
    app_power_get_battery_status(&battery);
    const lv_color_t level_color = radar_battery_level_color(
        battery.present ? battery.percent : -1);

    const int x = 201;
    const int y = 49;
    const int w = 29;
    const int h = 13;
    if (battery.present && battery.charging) {
        label(
            radar_menu_overlay,
            LV_SYMBOL_CHARGE,
            &lv_font_montserrat_14,
            level_color,
            LV_ALIGN_TOP_LEFT,
            184,
            48);
    }
    lv_obj_t *outline = card(
        radar_menu_overlay,
        x,
        y,
        w,
        h,
        lv_color_hex(0x000000),
        LV_OPA_TRANSP,
        lv_color_hex(0x7b837e),
        (lv_opa_t)180,
        4);
    lv_obj_set_style_border_width(outline, 1, 0);
    lv_obj_t *terminal = card(
        radar_menu_overlay,
        x + w + 2,
        y + 3,
        3,
        7,
        lv_color_hex(0x7b837e),
        (lv_opa_t)180,
        lv_color_hex(0x000000),
        LV_OPA_TRANSP,
        1);
    lv_obj_set_style_border_width(terminal, 0, 0);

    if (battery.present && battery.percent >= 0) {
        const int inner_width =
            (w - 4) * battery.percent / 100;
        if (inner_width > 0) {
            lv_obj_t *fill = card(
                radar_menu_overlay,
                x + 2,
                y + 2,
                inner_width,
                h - 4,
                level_color,
                LV_OPA_COVER,
                lv_color_hex(0x000000),
                LV_OPA_TRANSP,
                2);
            lv_obj_set_style_border_width(fill, 0, 0);
        }
    }

    char text[16];
    if (battery.present && battery.percent >= 0) {
        snprintf(
            text,
            sizeof(text),
            "%d%%",
            battery.percent);
    } else {
        snprintf(text, sizeof(text), "--");
    }
    label(
        radar_menu_overlay,
        text,
        &lv_font_montserrat_12,
        level_color,
        LV_ALIGN_TOP_LEFT,
        239,
        47);
}

static void draw_radar_brightness_page(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    int brightness = cfg.display_brightness_percent;
    if (brightness < 10 || brightness > 100) {
        brightness = 100;
    }

    const int diameter = 246;
    const int center_y = 216;
    radar_brightness_visual_percent = (float)brightness;
    radar_brightness_target_percent = (float)brightness;
    radar_brightness_last_raw =
        (int)lroundf((float)brightness * 255.0f / 100.0f);
    radar_brightness_arc = lv_arc_create(radar_menu_overlay);
    lv_obj_t *arc = radar_brightness_arc;
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_set_pos(
        arc,
        CX - diameter / 2,
        center_y - diameter / 2);
    lv_arc_set_bg_angles(arc, 0, 359);
    const int end_angle = brightness >= 100
        ? 359
        : (brightness * 359) / 100;
    lv_arc_set_angles(arc, 0, end_angle);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 15, LV_PART_MAIN);
    lv_obj_set_style_arc_color(
        arc,
        lv_color_hex(0x151a17),
        LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, (lv_opa_t)230, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(
        arc,
        lv_color_hex(radar_palette_active->main),
        LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    ring(
        radar_menu_overlay,
        186,
        lv_color_hex(radar_palette_active->main),
        1,
        (lv_opa_t)42);

    radar_brightness_up_button = (radar_range_button_t) {
        .valid = true,
        .x = CX - 45,
        .y = 104,
        .w = 90,
        .h = 54,
    };
    radar_brightness_down_button = (radar_range_button_t) {
        .valid = true,
        .x = CX - 45,
        .y = 273,
        .w = 90,
        .h = 54,
    };

    label(
        radar_menu_overlay,
        LV_SYMBOL_UP,
        &lv_font_montserrat_24,
        lv_color_hex(radar_palette_active->text),
        LV_ALIGN_TOP_MID,
        0,
        113);
    label(
        radar_menu_overlay,
        "BRIGHTNESS",
        &lv_font_montserrat_16,
        lv_color_hex(0xb9c1bc),
        LV_ALIGN_TOP_MID,
        0,
        183);
    char value[8];
    snprintf(value, sizeof(value), "%d%%", brightness);
    radar_brightness_value_label = label(
        radar_menu_overlay,
        value,
        &lv_font_montserrat_40,
        lv_color_hex(0xf5f7f5),
        LV_ALIGN_TOP_MID,
        0,
        211);
    label(
        radar_menu_overlay,
        LV_SYMBOL_DOWN,
        &lv_font_montserrat_24,
        lv_color_hex(radar_palette_active->text),
        LV_ALIGN_TOP_MID,
        0,
        282);
    label(
        radar_menu_overlay,
        "SWIPE TO ADJUST BRIGHTNESS",
        &lv_font_montserrat_12,
        lv_color_hex(0x747d78),
        LV_ALIGN_TOP_MID,
        0,
        350);
}

static void draw_api_separator(int y)
{
    const int x = 82;
    const int segment_count = 12;
    const int segment_width = 25;
    for (int segment = 0; segment < segment_count; segment++) {
        const int distance_from_edge = segment < segment_count / 2
            ? segment
            : segment_count - 1 - segment;
        const lv_opa_t opacity = (lv_opa_t)(24 + distance_from_edge * 22);
        lv_obj_t *dash = lv_obj_create(radar_menu_overlay);
        lv_obj_remove_style_all(dash);
        lv_obj_set_pos(dash, x + segment * segment_width, y);
        lv_obj_set_size(dash, segment_width + 1, 1);
        lv_obj_set_style_bg_color(dash, lv_color_hex(radar_palette_active->main), 0);
        lv_obj_set_style_bg_opa(dash, opacity, 0);
        lv_obj_remove_flag(dash, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void draw_api_logo(const lv_image_dsc_t *source, int center_x, int center_y)
{
    lv_obj_t *logo = lv_image_create(radar_menu_overlay);
    lv_image_set_src(logo, source);
    lv_obj_set_pos(
        logo,
        center_x - (int)source->header.w / 2,
        center_y - (int)source->header.h / 2);
    lv_obj_set_style_image_recolor(logo, lv_color_hex(0x62c9ff), 0);
    lv_obj_set_style_image_recolor_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(logo, LV_OPA_COVER, 0);
    lv_obj_remove_flag(logo, LV_OBJ_FLAG_CLICKABLE);
}

static void draw_api_status_mark(app_api_health_state_t health,
                                 int center_x,
                                 int center_y)
{
    if (health == APP_API_HEALTH_PENDING) {
        lv_obj_t *spinner = lv_spinner_create(radar_menu_overlay);
        lv_obj_set_size(spinner, 36, 36);
        lv_obj_set_pos(spinner, center_x - 18, center_y - 18);
        lv_spinner_set_anim_params(spinner, 850, 250);
        lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_color(
            spinner,
            lv_color_hex(0x6f3e0c),
            LV_PART_MAIN);
        lv_obj_set_style_arc_opa(spinner, (lv_opa_t)180, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(
            spinner,
            lv_color_hex(0xffa62b),
            LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(
            spinner,
            LV_OPA_COVER,
            LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(spinner, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spinner, 0, 0);
        lv_obj_remove_flag(
            spinner,
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        return;
    }

    const bool available = health == APP_API_HEALTH_VALID;
    lv_obj_t *halo = card(
        radar_menu_overlay,
        center_x - 18,
        center_y - 18,
        36,
        36,
        lv_color_hex(available ? 0x041a0b : 0x200506),
        (lv_opa_t)235,
        lv_color_hex(available ? 0x146629 : 0x8a2026),
        (lv_opa_t)220,
        LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(halo, 1, 0);
    label(
        halo,
        available ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE,
        &lv_font_montserrat_16,
        lv_color_hex(available ? 0x37e46a : 0xff4d57),
        LV_ALIGN_CENTER,
        0,
        0);
}

static void draw_radar_api_page(void)
{
    const app_data_status_t *data = ui_data_status();
    const app_api_health_state_t flightaware_health = data != NULL
        ? data->flightaware_api_health
        : APP_API_HEALTH_PENDING;
    const app_api_health_state_t opensky_health = data != NULL
        ? data->opensky_api_health
        : APP_API_HEALTH_PENDING;
    const app_api_health_state_t airlabs_health = data != NULL
        ? data->airlabs_api_health
        : APP_API_HEALTH_PENDING;

    label(
        radar_menu_overlay,
        "APIs",
        &lv_font_montserrat_20,
        lv_color_hex(0xf2f5f3),
        LV_ALIGN_TOP_MID,
        0,
        50);

    draw_api_logo(&airspace_api_logo_flightaware, 157, 112);
    label(
        radar_menu_overlay,
        "FlightAware",
        &lv_font_montserrat_14,
        lv_color_hex(0x62c9ff),
        LV_ALIGN_TOP_LEFT,
        205,
        104);
    draw_api_status_mark(flightaware_health, 351, 112);
    draw_api_separator(158);

    draw_api_logo(&airspace_api_logo_opensky, 192, 212);
    draw_api_status_mark(opensky_health, 351, 212);
    draw_api_separator(260);

    draw_api_logo(&airspace_api_logo_airlabs, 192, 313);
    draw_api_status_mark(airlabs_health, 351, 313);
}

static void draw_radar_close_button(void)
{
    const int size = 58;
    const int x = CX - size / 2;
    const int y = 386;
    radar_menu_button = (radar_range_button_t) {
        .valid = true,
        .x = x,
        .y = y,
        .w = size,
        .h = size,
        .miles = 0,
    };

    lv_obj_t *button = card(radar_menu_overlay,
                            x,
                            y,
                            size,
                            size,
                            lv_color_hex(0x0a0b0a),
                            (lv_opa_t)245,
                            lv_color_hex(0x424742),
                            (lv_opa_t)190,
                            LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_TRANSP, 0);
    label(button,
          LV_SYMBOL_CLOSE,
          &lv_font_montserrat_20,
          lv_color_hex(0xd8ddd9),
          LV_ALIGN_CENTER,
          0,
          0);
}

static void draw_radar_range_menu(void)
{
    if (!radar_range_menu_open) {
        return;
    }

    radar_range_button_count = 0;
    radar_wifi_profile_button_count = 0;
    radar_location_button_count = 0;
    radar_accent_button_count = 0;
    memset(radar_page_buttons, 0, sizeof(radar_page_buttons));
    radar_labels_button.valid = false;
    radar_wedge_button.valid = false;
    memset(
        radar_weather_unit_buttons,
        0,
        sizeof(radar_weather_unit_buttons));
    radar_brightness_up_button.valid = false;
    radar_brightness_down_button.valid = false;

    radar_menu_overlay = lv_obj_create(screen_root);
    lv_obj_remove_style_all(radar_menu_overlay);
    lv_obj_set_size(radar_menu_overlay, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_pos(radar_menu_overlay, 0, 0);
    lv_obj_remove_flag(radar_menu_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(radar_menu_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(radar_menu_overlay, LV_OPA_COVER, 0);

    if (radar_menu_page == 0) {
        app_config_t cfg;
        app_config_get(&cfg);
        char keys[sizeof(cfg.radar_city_keys)];
        snprintf(keys, sizeof(keys), "%s", cfg.radar_city_keys);
        char *save_pointer = NULL;
        int visible_index = 0;
        for (char *key = strtok_r(keys, ",", &save_pointer);
             key != NULL && visible_index < RADAR_LOCATION_BUTTON_COUNT;
             key = strtok_r(NULL, ",", &save_pointer)) {
            const app_location_preset_t *preset =
                app_location_preset_find(key);
            if (preset == NULL) {
                continue;
            }
            const int row = visible_index / 4;
            const int col = visible_index % 4;
            draw_radar_location_button(
                preset,
                88 + col * 96,
                129 + row * 102);
            visible_index++;
        }

        lv_obj_t *range_group = card(radar_menu_overlay,
                                     75,
                                     305,
                                     316,
                                     67,
                                     lv_color_hex(0x050605),
                                     (lv_opa_t)220,
                                     lv_color_hex(0x000000),
                                     LV_OPA_TRANSP,
                                     34);
        lv_obj_set_style_border_width(range_group, 0, 0);
        draw_radar_range_menu_button(10.0f, 139, 338, 82, 47);
        draw_radar_range_menu_button(20.0f, CX, 338, 82, 47);
        draw_radar_range_menu_button(50.0f, 327, 338, 82, 47);
    } else if (radar_menu_page == 1) {
        app_network_status_t net;
        app_network_get_status(&net);

        char buf[96];
        if (net.wifi_connected) {
            snprintf(buf, sizeof(buf), "%s  %ddBm", net.configured_ssid, net.rssi);
        } else if (net.wifi_configured) {
            snprintf(buf, sizeof(buf), "%s  %s", net.configured_ssid, app_wifi_state_text(net.wifi_state));
        } else {
            snprintf(buf, sizeof(buf), "Wi-Fi not configured");
        }
        lv_obj_t *status_text = label_box(
            radar_menu_overlay,
            buf,
            &lv_font_montserrat_12,
            net.wifi_connected ? lv_color_hex(0xf4f6f4) : lv_color_hex(0xffcf76),
            LV_ALIGN_TOP_LEFT,
            0,
            0,
            330);
        lv_obj_set_pos(status_text, (SCREEN_SIZE - 330) / 2, 72);
        lv_obj_set_style_text_align(status_text, LV_TEXT_ALIGN_CENTER, 0);

#ifndef AIRSPACE_DESKTOP_RENDERER
        if (net.wifi_connected &&
            net.ip[0] != '\0' &&
            strcmp(net.ip, "0.0.0.0") != 0) {
            lv_obj_t *ip_text = label_box(
                radar_menu_overlay,
                net.ip,
                &lv_font_montserrat_12,
                lv_color_hex(radar_palette_active->main),
                LV_ALIGN_TOP_LEFT,
                0,
                0,
                220);
            lv_obj_set_pos(
                ip_text,
                (SCREEN_SIZE - 220) / 2,
                91);
            lv_obj_set_style_text_align(
                ip_text,
                LV_TEXT_ALIGN_CENTER,
                0);
        }
#endif

        draw_radar_wifi_profile_switch(CX, 147, 164, 62);
        draw_radar_labels_toggle(105, 251, 112, 84);
        draw_radar_wedge_toggle(233, 251, 112, 84);
        draw_radar_weather_units_switch(361, 251, 112, 84);

        draw_radar_accent_button(APP_ACCENT_GREEN, 149, 339);
        draw_radar_accent_button(APP_ACCENT_ORANGE, 205, 339);
        draw_radar_accent_button(APP_ACCENT_CYAN, 261, 339);
        draw_radar_accent_button(APP_ACCENT_RED, 317, 339);
    } else if (radar_menu_page == 2) {
        draw_radar_brightness_page();
    } else {
        draw_radar_api_page();
    }

    draw_radar_page_indicator();
    if (radar_menu_page == 0) {
        draw_radar_battery_indicator();
    }
    draw_radar_close_button();
}

static void refresh_radar_menu_overlay(void)
{
    if (radar_menu_overlay != NULL) {
        detach_radar_brightness_controls(true);
        lv_obj_delete(radar_menu_overlay);
        radar_menu_overlay = NULL;
    }
    if (radar_range_menu_open) {
        draw_radar_range_menu();
    }
}

static void render_radar(void)
{
    sync_radar_display_config();

    size_t radar_map_len = 0;
    uint32_t radar_map_revision = 0;
    int radar_map_index = app_remote_radar_range_index_for_miles(radar_range_miles);
    const uint8_t *radar_map = app_remote_get_radar_map_for_range(
        radar_range_miles,
        &radar_map_len,
        &radar_map_revision,
        &radar_map_index
    );
    const uint8_t radar_map_kind = (uint8_t)(MAP_KIND_RADAR_BASE + radar_map_index);
    radar_map = prepare_padded_map_tiles(radar_map_kind, radar_map_len, radar_map, radar_map_revision);

    draw_shell();

    if (radar_map != NULL) {
        draw_pushed_map_tiles(radar_map);
    } else {
        soft_blob(screen_root, 232, 224, 230, lv_color_hex(0x1a1a1a), (lv_opa_t)18);
    }

    ring(screen_root, 454, lv_color_hex(0x1c241d), 1, (lv_opa_t)70);
    ring(screen_root, 340, lv_color_hex(0x243025), 1, (lv_opa_t)86);
    ring(screen_root, 226, lv_color_hex(radar_palette_active->dim), 1, (lv_opa_t)102);
    ring(screen_root, 112, lv_color_hex(radar_palette_active->main), 1, (lv_opa_t)118);
#if RADAR_SWEEP_ENABLED
    if (radar_wedge_visible) {
        create_radar_sweep();
    }
#endif

    radar_aircraft_layer = lv_obj_create(screen_root);
    lv_obj_remove_style_all(radar_aircraft_layer);
    lv_obj_set_size(radar_aircraft_layer, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_pos(radar_aircraft_layer, 0, 0);
    lv_obj_remove_flag(radar_aircraft_layer, LV_OBJ_FLAG_CLICKABLE);
    radar_update_aircraft_animation();

    draw_radar_range_menu();
}

typedef enum {
    TRACKED_PANEL_TOP,
    TRACKED_PANEL_BOTTOM,
    TRACKED_PANEL_LEFT,
    TRACKED_PANEL_RIGHT,
} tracked_panel_edge_t;

static void tracked_panel_x_animation(void *object, int32_t x)
{
    lv_obj_set_x((lv_obj_t *)object, x);
}

static void tracked_panel_y_animation(void *object, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)object, y);
}

static void tracked_animate_panel(lv_obj_t *panel,
                                  bool horizontal,
                                  int32_t from,
                                  int32_t to,
                                  uint32_t delay,
                                  lv_anim_path_cb_t path)
{
    if (panel == NULL) {
        return;
    }
    if (horizontal) {
        lv_obj_set_x(panel, from);
    } else {
        lv_obj_set_y(panel, from);
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, panel);
    lv_anim_set_exec_cb(
        &animation,
        horizontal ? tracked_panel_x_animation : tracked_panel_y_animation);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, 460);
    lv_anim_set_delay(&animation, delay);
    lv_anim_set_path_cb(&animation, path);
    lv_anim_start(&animation);
}

static void tracked_ensure_side_panel_gradients(void)
{
    if (tracked_side_panel_gradients_initialized) {
        return;
    }
    const lv_color_t colors[] = {
        lv_color_hex(0x000000),
        lv_color_hex(0x000000),
        lv_color_hex(0x000000),
    };
    const uint8_t stops[] = {0, 142, 255};
    const lv_opa_t left_opacity[] = {
        (lv_opa_t)244,
        (lv_opa_t)165,
        LV_OPA_TRANSP,
    };
    const lv_opa_t right_opacity[] = {
        LV_OPA_TRANSP,
        (lv_opa_t)165,
        (lv_opa_t)244,
    };
    lv_grad_init_stops(
        &tracked_left_panel_gradient,
        colors,
        left_opacity,
        stops,
        3);
    lv_grad_horizontal_init(
        &tracked_left_panel_gradient);
    lv_grad_init_stops(
        &tracked_right_panel_gradient,
        colors,
        right_opacity,
        stops,
        3);
    lv_grad_horizontal_init(
        &tracked_right_panel_gradient);
    tracked_side_panel_gradients_initialized = true;
}

static lv_obj_t *tracked_gradient_panel(int x,
                                        int y,
                                        int width,
                                        int height,
                                        tracked_panel_edge_t edge)
{
    lv_obj_t *panel = lv_obj_create(screen_root);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x000000), 0);
    if (edge == TRACKED_PANEL_LEFT ||
        edge == TRACKED_PANEL_RIGHT) {
        tracked_ensure_side_panel_gradients();
        lv_obj_set_style_bg_opa(
            panel,
            LV_OPA_COVER,
            0);
        lv_obj_set_style_bg_grad(
            panel,
            edge == TRACKED_PANEL_LEFT
                ? &tracked_left_panel_gradient
                : &tracked_right_panel_gradient,
            0);
        lv_obj_remove_flag(
            panel,
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        return panel;
    }
    const bool reverse =
        edge == TRACKED_PANEL_BOTTOM || edge == TRACKED_PANEL_RIGHT;
    lv_obj_set_style_bg_opa(
        panel,
        reverse ? LV_OPA_TRANSP : LV_OPA_COVER,
        0);
    lv_obj_set_style_bg_grad_opa(
        panel,
        reverse ? LV_OPA_COVER : LV_OPA_TRANSP,
        0);
    /*
     * Pin the opaque end to the physical edge of the display.  Relying on
     * LVGL's default gradient stops made the side-panel shade vary when the
     * timing pages were animated or rebuilt.
     */
    lv_obj_set_style_bg_main_stop(panel, 0, 0);
    lv_obj_set_style_bg_grad_stop(panel, 255, 0);
    lv_obj_set_style_bg_grad_dir(
        panel,
        edge == TRACKED_PANEL_TOP || edge == TRACKED_PANEL_BOTTOM
            ? LV_GRAD_DIR_VER
            : LV_GRAD_DIR_HOR,
        0);
    lv_obj_remove_flag(
        panel,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static void tracked_endpoint_text(lv_obj_t *parent,
                                  const char *code,
                                  const char *city,
                                  int x,
                                  int y,
                                  int width,
                                  lv_text_align_t align)
{
    lv_obj_t *code_label = label_box(
        parent,
        code != NULL && code[0] != '\0' ? code : "-",
        &lv_font_montserrat_20,
        lv_color_hex(0xf4f6f4),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        width);
    lv_obj_set_pos(code_label, x, y);
    lv_obj_set_style_text_align(code_label, align, 0);

    if (city != NULL &&
        city[0] != '\0' &&
        (code == NULL || strcasecmp(city, code) != 0)) {
        lv_obj_t *city_label = label_box(
            parent,
            city,
            &lv_font_montserrat_12,
            lv_color_hex(0x858f89),
            LV_ALIGN_TOP_LEFT,
            0,
            0,
            width);
        lv_obj_set_pos(city_label, x, y + 26);
        lv_obj_set_style_text_align(city_label, align, 0);
        lv_obj_set_style_text_letter_space(
            city_label,
            0,
            0);
    }
}

static void tracked_preflight_endpoint_text(lv_obj_t *parent,
                                            const char *code,
                                            const char *city,
                                            int center_x)
{
    const int width = 132;
    const int x = center_x - width / 2;
    lv_obj_t *code_label = label_box(
        parent,
        code != NULL && code[0] != '\0' ? code : "-",
        &lv_font_montserrat_28,
        lv_color_hex(0xf4f6f4),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        width);
    lv_obj_set_pos(code_label, x, 101);
    lv_obj_set_style_text_align(code_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(code_label, LV_LABEL_LONG_CLIP);

    if (city != NULL &&
        city[0] != '\0' &&
        (code == NULL || strcasecmp(city, code) != 0)) {
        lv_obj_t *city_label = label_box(
            parent,
            city,
            &lv_font_montserrat_14,
            lv_color_hex(0x929d96),
            LV_ALIGN_TOP_LEFT,
            0,
            0,
            width);
        lv_obj_set_pos(city_label, x, 135);
        lv_obj_set_style_text_align(city_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_letter_space(city_label, 0, 0);
        lv_label_set_long_mode(city_label, LV_LABEL_LONG_CLIP);
    }
}

static bool tracked_gate_text_for_page(const char *terminal,
                                       const char *gate,
                                       int page,
                                       char *out,
                                       size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    const bool has_terminal = terminal != NULL && terminal[0] != '\0';
    const bool has_gate = gate != NULL && gate[0] != '\0';
    if (!has_terminal && !has_gate) {
        return false;
    }

    const bool show_terminal = has_terminal &&
        (!has_gate ||
         (page >= 0
              ? (page & 1) == 0
              : ((esp_timer_get_time() / 5000000LL) & 1LL) == 0));
    const char *value = show_terminal ? terminal : gate;
    const char prefix = 'T';
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    if (!show_terminal || toupper((unsigned char)value[0]) == prefix) {
        snprintf(out, out_size, "%.9s", value);
    } else {
        snprintf(out, out_size, "%c%.8s", prefix, value);
    }
    return out[0] != '\0';
}

static lv_obj_t *tracked_draw_gate_sign_shell(lv_obj_t *parent,
                                              int x,
                                              int y,
                                              int width,
                                              bool departure,
                                              const char *terminal,
                                              const char *gate)
{
    char text[12];
    if (!tracked_gate_text_for_page(
            terminal,
            gate,
            0,
            text,
            sizeof(text))) {
        return NULL;
    }

    const int height = 22;
    const int sign_width = width + 2;
    lv_obj_t *sign = card(
        parent,
        x - 1,
        y,
        sign_width,
        height,
        lv_color_hex(0xffd400),
        LV_OPA_COVER,
        lv_color_hex(0x050604),
        LV_OPA_COVER,
        2);
    lv_obj_remove_flag(sign, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    const lv_color_t ink = lv_color_hex(0x050604);
    lv_obj_t *arrow = lv_image_create(sign);
    lv_image_set_src(
        arrow,
        departure
            ? &airspace_gate_arrow_departure
            : &airspace_gate_arrow_arrival);
    lv_obj_set_pos(arrow, 3, departure ? 2 : 1);
    lv_image_set_antialias(arrow, true);
    lv_obj_set_style_image_recolor(arrow, ink, 0);
    lv_obj_set_style_image_recolor_opa(arrow, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(arrow, LV_OPA_COVER, 0);
    lv_obj_remove_flag(arrow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return sign;
}

static void tracked_draw_gate_value_for_page(lv_obj_t *parent,
                                             int width,
                                             const char *terminal,
                                             const char *gate,
                                             int page)
{
    char text[12];
    if (!tracked_gate_text_for_page(
            terminal,
            gate,
            page,
            text,
            sizeof(text))) {
        return;
    }

    const int sign_width = width + 2;
    const lv_color_t ink = lv_color_hex(0x050604);
    lv_obj_t *value = label_box(
        parent,
        text,
        &lv_font_montserrat_16,
        ink,
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        sign_width - 23);
    lv_obj_set_pos(value, 0, 0);
    lv_obj_set_height(value, 21);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_outline_stroke_color(value, ink, 0);
    lv_obj_set_style_text_outline_stroke_width(value, 1, 0);
    lv_obj_set_style_text_outline_stroke_opa(value, LV_OPA_COVER, 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
}

static lv_obj_t *tracked_draw_static_gate_sign(lv_obj_t *parent,
                                               int center_x,
                                               int y,
                                               bool departure,
                                               const char *terminal,
                                               const char *gate)
{
    const bool has_gate = gate != NULL && gate[0] != '\0';
    const int value_page = has_gate ? 1 : 0;
    char text[12];
    if (!tracked_gate_text_for_page(
            terminal,
            gate,
            value_page,
            text,
            sizeof(text))) {
        return NULL;
    }

    const int content_width = 62;
    lv_obj_t *sign = tracked_draw_gate_sign_shell(
        parent,
        center_x - (content_width + 2) / 2,
        y,
        content_width,
        departure,
        terminal,
        gate);
    if (sign == NULL) {
        return NULL;
    }

    lv_obj_t *value = label_box(
        sign,
        text,
        &lv_font_montserrat_16,
        lv_color_hex(0x050604),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        content_width - 21);
    lv_obj_set_pos(value, 22, 0);
    lv_obj_set_height(value, 21);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_outline_stroke_color(
        value,
        lv_color_hex(0x050604),
        0);
    lv_obj_set_style_text_outline_stroke_width(value, 1, 0);
    lv_obj_set_style_text_outline_stroke_opa(value, LV_OPA_COVER, 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
    return sign;
}

static lv_obj_t *tracked_centered_label(lv_obj_t *parent,
                                        const char *text,
                                        const lv_font_t *font,
                                        lv_color_t color,
                                        int x,
                                        int y,
                                        int width)
{
    lv_obj_t *object = label_box(
        parent,
        text,
        font,
        color,
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        width);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(object, 1, 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    return object;
}

static void tracked_endpoint_layout(bool destination,
                                    float progress,
                                    int aircraft_x,
                                    int aircraft_y,
                                    int *x,
                                    int *y,
                                    int *width,
                                    lv_text_align_t *align)
{
    const int last = tracked_route_point_count > 0
        ? tracked_route_point_count - 1
        : 0;
    const int endpoint_index = destination ? last : 0;
    const int adjacent_index = destination
        ? (last > 0 ? last - 1 : last)
        : (last > 0 ? 1 : 0);
    const float endpoint_x =
        tracked_route_points[endpoint_index][0];
    const float endpoint_y =
        tracked_route_points[endpoint_index][1];
    float route_x =
        tracked_route_points[adjacent_index][0] - endpoint_x;
    float route_y =
        tracked_route_points[adjacent_index][1] - endpoint_y;
    float length = hypotf(route_x, route_y);
    if (length < 0.5f) {
        route_x = CX - endpoint_x;
        route_y = CY - endpoint_y;
        length = hypotf(route_x, route_y);
    }
    if (length < 0.5f) {
        route_x = destination ? -1.0f : 1.0f;
        route_y = 0.0f;
        length = 1.0f;
    }
    route_x /= length;
    route_y /= length;

    const float outside_x = -route_x;
    const float outside_y = -route_y;
    const float normal_x = -route_y;
    const float normal_y = route_x;

    const float endpoint_proximity = destination
        ? fmaxf(0.0f, fminf(1.0f, (progress - 0.84f) / 0.16f))
        : fmaxf(0.0f, fminf(1.0f, (0.16f - progress) / 0.16f));
    const float smooth_proximity =
        endpoint_proximity * endpoint_proximity *
        (3.0f - 2.0f * endpoint_proximity);
    *width = 126;
    const float half_width = (float)*width * 0.5f;
    const float half_height = 21.0f;
    const float minimum_center_x = half_width + 8.0f;
    const float maximum_center_x =
        (float)SCREEN_SIZE - half_width - 8.0f;
    const float minimum_center_y = 102.0f + half_height;
    const float maximum_center_y = 310.0f + half_height;

    /*
     * Extending an endpoint label along the route tangent works only while
     * there is room beyond that endpoint.  Near the top and bottom of the
     * circular display, the safe-area clamp used to fold the label back over
     * the route and aircraft.  Score lateral and diagonal placements instead:
     * prefer the two sides of the local route, then reject any candidate that
     * enters the route corridor, aircraft footprint, or panel safe areas.
     */
    const float candidate_directions[][2] = {
        { normal_x, normal_y },
        { -normal_x, -normal_y },
        { outside_x + normal_x, outside_y + normal_y },
        { outside_x - normal_x, outside_y - normal_y },
        { -1.0f, 0.0f },
        { 1.0f, 0.0f },
        { outside_x, outside_y },
    };
    float best_center_x = CX;
    float best_center_y = CY;
    float best_score = 1.0e30f;
    for (size_t index = 0;
         index < sizeof(candidate_directions) /
             sizeof(candidate_directions[0]);
         index++) {
        float direction_x = candidate_directions[index][0];
        float direction_y = candidate_directions[index][1];
        const float direction_length =
            hypotf(direction_x, direction_y);
        if (direction_length < 0.5f) {
            continue;
        }
        direction_x /= direction_length;
        direction_y /= direction_length;
        const float projected_half_extent =
            fabsf(direction_x) * half_width +
            fabsf(direction_y) * half_height;
        const float offset =
            projected_half_extent + 8.0f +
            18.0f * smooth_proximity;
        const float raw_center_x =
            endpoint_x + direction_x * offset;
        const float raw_center_y =
            endpoint_y + direction_y * offset;
        const float center_x = fmaxf(
            minimum_center_x,
            fminf(maximum_center_x, raw_center_x));
        const float center_y = fmaxf(
            minimum_center_y,
            fminf(maximum_center_y, raw_center_y));

        const float clamp_distance = hypotf(
            center_x - raw_center_x,
            center_y - raw_center_y);
        float score = clamp_distance * 16.0f;
        if (index >= 2) {
            score += index >= 6 ? 180.0f : 36.0f;
        }

        const float route_normal_x = -route_y;
        const float route_normal_y = route_x;
        const float route_distance = fabsf(
            (center_x - endpoint_x) * route_normal_x +
            (center_y - endpoint_y) * route_normal_y);
        const float route_half_extent =
            fabsf(route_normal_x) * half_width +
            fabsf(route_normal_y) * half_height;
        const float route_clearance =
            route_distance - route_half_extent;
        if (route_clearance < 12.0f) {
            score += (12.0f - route_clearance) * 900.0f;
        }

        /* The endpoint tangent is not enough for a curved or short route:
         * another part of the spline can bend back through the label.  Check
         * the candidate's full two-line box against every route segment so
         * the placement remains valid for any airport pair or orientation. */
        for (int segment_index = 0;
             segment_index + 1 < tracked_route_point_count;
             segment_index++) {
            const float start_x =
                tracked_route_points[segment_index][0];
            const float start_y =
                tracked_route_points[segment_index][1];
            const float segment_x =
                tracked_route_points[segment_index + 1][0] - start_x;
            const float segment_y =
                tracked_route_points[segment_index + 1][1] - start_y;
            const float segment_length_squared =
                segment_x * segment_x + segment_y * segment_y;
            float nearest = 0.0f;
            if (segment_length_squared > 0.25f) {
                nearest =
                    ((center_x - start_x) * segment_x +
                     (center_y - start_y) * segment_y) /
                    segment_length_squared;
                nearest = fmaxf(0.0f, fminf(1.0f, nearest));
            }
            const float nearest_x = start_x + nearest * segment_x;
            const float nearest_y = start_y + nearest * segment_y;
            const float box_dx = fmaxf(
                0.0f,
                fabsf(center_x - nearest_x) - half_width);
            const float box_dy = fmaxf(
                0.0f,
                fabsf(center_y - nearest_y) - half_height);
            const float segment_clearance = hypotf(box_dx, box_dy);
            if (segment_clearance < 12.0f) {
                score +=
                    (12.0f - segment_clearance) * 1100.0f;
            }
        }

        const float aircraft_dx = fmaxf(
            0.0f,
            fabsf(center_x - (float)aircraft_x) - half_width);
        const float aircraft_dy = fmaxf(
            0.0f,
            fabsf(center_y - (float)aircraft_y) - half_height);
        const float aircraft_clearance =
            hypotf(aircraft_dx, aircraft_dy) - 29.0f;
        if (aircraft_clearance < 14.0f) {
            score += (14.0f - aircraft_clearance) * 1200.0f;
        }

        if (score < best_score) {
            best_score = score;
            best_center_x = center_x;
            best_center_y = center_y;
        }
    }

    *x = (int)lroundf(best_center_x - half_width);
    *y = (int)lroundf(best_center_y - half_height);
    *align = LV_TEXT_ALIGN_CENTER;
}

static int tracked_frame_endpoint_label_y(bool destination,
                                          float progress,
                                          int aircraft_y)
{
    const int last = tracked_route_point_count > 0
        ? tracked_route_point_count - 1
        : 0;
    const int endpoint_y =
        tracked_route_points[destination ? last : 0][1];
    const float endpoint_proximity = destination
        ? fmaxf(0.0f, fminf(1.0f, (progress - 0.82f) / 0.18f))
        : fmaxf(0.0f, fminf(1.0f, (0.18f - progress) / 0.18f));
    const float smooth_proximity =
        endpoint_proximity * endpoint_proximity *
        (3.0f - 2.0f * endpoint_proximity);

    /* The two-line endpoint block is about 42 px tall.  Anchor it outside
     * the actual route endpoint, then increase the gap as the aircraft enters
     * that endpoint's protected area.  This keeps the 6-to-12 composition
     * compact in cruise without allowing the plane, path, code, or city name
     * to collide during departure and arrival. */
    const int label_height = 42;
    const int aircraft_half_height = 25;
    const int base_clearance = 18;
    const int proximity_clearance =
        (int)lroundf(24.0f * smooth_proximity);
    int label_y = destination
        ? endpoint_y - label_height -
            base_clearance - proximity_clearance
        : endpoint_y + base_clearance + proximity_clearance;

    if (abs(aircraft_y - endpoint_y) < 72) {
        if (destination) {
            const int aircraft_safe_y =
                aircraft_y - aircraft_half_height -
                label_height - 8;
            if (label_y > aircraft_safe_y) {
                label_y = aircraft_safe_y;
            }
        } else {
            const int aircraft_safe_y =
                aircraft_y + aircraft_half_height + 8;
            if (label_y < aircraft_safe_y) {
                label_y = aircraft_safe_y;
            }
        }
    }

    /* These bounds describe the usable vertical diameter at the horizontal
     * centre of the round panel.  They are intentionally independent of the
     * optional metric panels, so hiding those panels cannot move an endpoint
     * outside the display. */
    const int minimum_y = 6;
    const int maximum_y = SCREEN_SIZE - label_height - 8;
    if (label_y < minimum_y) {
        label_y = minimum_y;
    } else if (label_y > maximum_y) {
        label_y = maximum_y;
    }
    return label_y;
}

static lv_obj_t *tracked_value(lv_obj_t *parent,
                               int x,
                               int y,
                               int width,
                               const char *title,
                               const char *value,
                               lv_color_t color)
{
    lv_obj_t *title_label = label_box(
        parent,
        title,
        &lv_font_montserrat_12,
        lv_color_hex(0x6f7973),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        width);
    lv_obj_set_pos(title_label, x, y);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_height(title_label, 17);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
    lv_obj_t *value_label = label_box(
        parent,
        value != NULL && value[0] != '\0' ? value : "-",
        &lv_font_montserrat_16,
        color,
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        width);
    lv_obj_set_pos(value_label, x, y + 20);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_height(value_label, 20);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
    return value_label;
}

static void tracked_metric(lv_obj_t *parent,
                           const lv_image_dsc_t *icon,
                           int center_x,
                           int top_y,
                           const char *value,
                           lv_color_t value_color)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, icon);
    lv_obj_set_pos(
        image,
        center_x - (int)icon->header.w / 2,
        top_y);
    lv_image_set_pivot(
        image,
        icon->header.w / 2,
        icon->header.h / 2);
    lv_image_set_scale(image, 190);
    lv_image_set_antialias(image, true);
    lv_obj_set_style_image_recolor(
        image,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(image, (lv_opa_t)220, 0);

    lv_obj_t *value_label = label_box(
        parent,
        value != NULL && value[0] != '\0' ? value : "-",
        &lv_font_montserrat_14,
        value_color,
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        104);
    lv_obj_set_pos(value_label, center_x - 52, top_y + 43);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
}

static void tracked_compass(lv_obj_t *parent,
                            int center_x,
                            int top_y,
                            const char *value,
                            int heading_degrees,
                            bool heading_valid)
{
    lv_obj_t *ring_image = lv_image_create(parent);
    lv_image_set_src(
        ring_image,
        &airspace_detail_metric_compass_ring);
    lv_obj_set_pos(
        ring_image,
        center_x -
            (int)airspace_detail_metric_compass_ring.header.w / 2,
        top_y);
    lv_image_set_scale(ring_image, 190);
    lv_obj_set_style_image_recolor(
        ring_image,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(
        ring_image,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(
        ring_image,
        (lv_opa_t)205,
        0);

    lv_obj_t *arrow = lv_image_create(parent);
    lv_image_set_src(
        arrow,
        &airspace_detail_metric_compass_arrow);
    lv_obj_set_pos(
        arrow,
        center_x -
            (int)airspace_detail_metric_compass_arrow.header.w / 2,
        top_y);
    lv_image_set_pivot(
        arrow,
        airspace_detail_metric_compass_arrow.header.w / 2,
        airspace_detail_metric_compass_arrow.header.h / 2);
    lv_image_set_scale(arrow, 142);
    lv_image_set_rotation(
        arrow,
        heading_valid ? heading_degrees * 10 : 0);
    lv_image_set_antialias(arrow, true);
    lv_obj_set_style_image_recolor(
        arrow,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(
        arrow,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(
        arrow,
        heading_valid ? (lv_opa_t)240 : (lv_opa_t)85,
        0);

    lv_obj_t *value_label = label_box(
        parent,
        value,
        &lv_font_montserrat_14,
        lv_color_hex(0xf0f3f0),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        104);
    lv_obj_set_pos(value_label, center_x - 52, top_y + 43);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
}

static void tracked_inline_metric(lv_obj_t *parent,
                                  const lv_image_dsc_t *icon,
                                  int icon_center_x,
                                  int top_y,
                                  int value_width,
                                  const char *value,
                                  lv_color_t value_color)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, icon);
    lv_obj_set_pos(
        image,
        icon_center_x - (int)icon->header.w / 2,
        top_y);
    lv_image_set_pivot(
        image,
        icon->header.w / 2,
        icon->header.h / 2);
    lv_image_set_scale(image, 160);
    lv_image_set_antialias(image, true);
    lv_obj_set_style_image_recolor(
        image,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(
        image,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(image, (lv_opa_t)235, 0);

    lv_obj_t *value_label = label_box(
        parent,
        value != NULL && value[0] != '\0' ? value : "-",
        &lv_font_montserrat_14,
        value_color,
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        value_width);
    lv_obj_set_pos(
        value_label,
        icon_center_x + 16,
        top_y + 13);
    lv_obj_set_height(value_label, 20);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(
        value_label,
        LV_TEXT_ALIGN_LEFT,
        0);
    lv_obj_set_style_text_letter_space(value_label, 0, 0);
}

static void tracked_inline_compass(lv_obj_t *parent,
                                   int icon_center_x,
                                   int top_y,
                                   int value_width,
                                   const char *value,
                                   int heading_degrees,
                                   bool heading_valid)
{
    lv_obj_t *ring_image = lv_image_create(parent);
    lv_image_set_src(
        ring_image,
        &airspace_detail_metric_compass_ring);
    lv_obj_set_pos(
        ring_image,
        icon_center_x -
            (int)airspace_detail_metric_compass_ring.header.w / 2,
        top_y);
    lv_image_set_pivot(
        ring_image,
        airspace_detail_metric_compass_ring.header.w / 2,
        airspace_detail_metric_compass_ring.header.h / 2);
    lv_image_set_scale(ring_image, 160);
    lv_obj_set_style_image_recolor(
        ring_image,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(
        ring_image,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(
        ring_image,
        (lv_opa_t)225,
        0);

    lv_obj_t *arrow = lv_image_create(parent);
    lv_image_set_src(
        arrow,
        &airspace_detail_metric_compass_arrow);
    lv_obj_set_pos(
        arrow,
        icon_center_x -
            (int)airspace_detail_metric_compass_arrow.header.w / 2,
        top_y);
    lv_image_set_pivot(
        arrow,
        airspace_detail_metric_compass_arrow.header.w / 2,
        airspace_detail_metric_compass_arrow.header.h / 2);
    lv_image_set_scale(arrow, 120);
    lv_image_set_rotation(
        arrow,
        heading_valid ? heading_degrees * 10 : 0);
    lv_image_set_antialias(arrow, true);
    lv_obj_set_style_image_recolor(
        arrow,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(
        arrow,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(
        arrow,
        heading_valid ? (lv_opa_t)245 : (lv_opa_t)90,
        0);

    lv_obj_t *value_label = label_box(
        parent,
        value != NULL && value[0] != '\0' ? value : "-",
        &lv_font_montserrat_14,
        lv_color_hex(0xf0f3f0),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        value_width);
    lv_obj_set_pos(
        value_label,
        icon_center_x + 16,
        top_y + 13);
    lv_obj_set_height(value_label, 20);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(
        value_label,
        LV_TEXT_ALIGN_LEFT,
        0);
}

static int tracked_inline_text_width(const char *value)
{
    const char *text =
        value != NULL && value[0] != '\0'
            ? value
            : "-";
    int width = 0;
    for (size_t index = 0;
         text[index] != '\0';
         index++) {
        const uint32_t current =
            (uint8_t)text[index];
        const uint32_t next =
            text[index + 1] != '\0'
                ? (uint8_t)text[index + 1]
                : 0;
        width += lv_font_get_glyph_width(
            &lv_font_montserrat_14,
            current,
            next);
    }
    /* Leave room for the final glyph and LVGL's integer glyph rounding. */
    width += 6;
    return width > 14 ? width : 14;
}

static void tracked_ensure_divider_gradient(void)
{
    if (tracked_divider_gradient_initialized) {
        return;
    }
    const lv_color_t colors[] = {
        lv_color_hex(0xffffff),
        lv_color_hex(0xffffff),
        lv_color_hex(0xffffff),
    };
    const lv_opa_t opacity[] = {
        LV_OPA_TRANSP,
        (lv_opa_t)205,
        LV_OPA_TRANSP,
    };
    const uint8_t stops[] = {0, 128, 255};
    lv_grad_init_stops(
        &tracked_divider_gradient,
        colors,
        opacity,
        stops,
        3);
    lv_grad_horizontal_init(&tracked_divider_gradient);
    tracked_divider_gradient_initialized = true;
}

static void tracked_draw_divider(lv_obj_t *parent,
                                 int x,
                                 int y,
                                 int width)
{
    tracked_ensure_divider_gradient();
    lv_obj_t *divider = lv_obj_create(parent);
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, x, y);
    lv_obj_set_size(divider, width, 1);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad(
        divider,
        &tracked_divider_gradient,
        0);
    lv_obj_remove_flag(
        divider,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static bool tracked_time_title_is_eta_or_etd(const char *title)
{
    return title != NULL &&
        (strcasecmp(title, "ETA") == 0 ||
         strcasecmp(title, "ETD") == 0);
}

static lv_color_t tracked_time_value_color(const char *title,
                                           bool delayed,
                                           lv_color_t normal)
{
    return delayed && tracked_time_title_is_eta_or_etd(title)
        ? lv_color_hex(0xff626b)
        : normal;
}

static lv_obj_t *tracked_footer_value(lv_obj_t *parent,
                                      int center_x,
                                      const char *title,
                                      const char *value,
                                      lv_color_t value_color)
{
    const int width = 108;
    lv_obj_t *title_label = label_box(
        parent,
        title,
        &lv_font_montserrat_12,
        lv_color_hex(0x737d77),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        width);
    lv_obj_set_pos(
        title_label,
        center_x - width / 2,
        64);
    lv_obj_set_style_text_align(
        title_label,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_set_height(title_label, 17);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);

    lv_obj_t *value_label = label_box(
        parent,
        value != NULL && value[0] != '\0' ? value : "-",
        &lv_font_montserrat_12,
        value_color,
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        width);
    lv_obj_set_pos(
        value_label,
        center_x - width / 2,
        82);
    lv_obj_set_style_text_align(
        value_label,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_set_height(value_label, 17);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
    return value_label;
}

typedef struct {
    char title[2][20];
    char value[2][24];
} tracked_time_pair_t;

#define TRACKED_POST_ARRIVAL_RETENTION_SECONDS (2 * 60 * 60)
#define TRACKED_ENDPOINT_ARRIVAL_GRACE_SECONDS (30 * 60)
#define TRACKED_UNCONFIRMED_ARRIVAL_TIMEOUT_SECONDS (24 * 60 * 60)

/* Free-tier schedule records occasionally retain an airborne status after the
 * final live occurrence has disappeared. Keep the device-side inference
 * conservative; provider-backed terminal validation is handled upstream.
 * A persisted occurrence that is still fractional a full day after its ETA
 * is nevertheless stale, which prevents an offline device from restoring the
 * same old flight forever after SNTP supplies the current time. */
static bool tracked_is_terminal(
    const app_remote_status_t *remote,
    int64_t now)
{
    if (remote == NULL) {
        return false;
    }
    if (remote->tracked_flight_actual_arrival_unix > 0 ||
        strcasecmp(
            remote->tracked_flight_journey_phase,
            "arrived") == 0 ||
        strcasecmp(
            remote->tracked_flight_journey_phase,
            "landed") == 0 ||
        strcasecmp(
            remote->tracked_flight_phase,
            "landed") == 0) {
        return true;
    }
    if (strcasecmp(remote->tracked_flight_status, "cancelled") == 0 ||
        strcasecmp(remote->tracked_flight_status, "canceled") == 0) {
        return false;
    }
    const bool occurrence_started =
        remote->tracked_flight_actual_departure_unix > 0 ||
        strcasecmp(
            remote->tracked_flight_journey_phase,
            "enroute") == 0 ||
        strcasecmp(
            remote->tracked_flight_journey_phase,
            "airborne") == 0;
    const int64_t eta =
        remote->tracked_flight_estimated_arrival_unix;
    if (!occurrence_started || eta <= 0) {
        return false;
    }
    const bool reached_endpoint =
        remote->route_progress >= 0.995f &&
        now >= eta + TRACKED_ENDPOINT_ARRIVAL_GRACE_SECONDS;
    const bool exceeded_stale_timeout =
        now >= eta + TRACKED_UNCONFIRMED_ARRIVAL_TIMEOUT_SECONDS;
    return reached_endpoint || exceeded_stale_timeout;
}

static int64_t tracked_terminal_reference_time(
    const app_remote_status_t *remote,
    int64_t now)
{
    if (!tracked_is_terminal(remote, now)) {
        return 0;
    }
    if (remote->tracked_flight_actual_arrival_unix > 0) {
        return remote->tracked_flight_actual_arrival_unix;
    }
    return remote->tracked_flight_estimated_arrival_unix;
}

static bool tracked_is_expired_after_arrival(
    const app_remote_status_t *remote,
    int64_t now)
{
    const int64_t arrival = tracked_terminal_reference_time(remote, now);
    return arrival > 0 &&
        now >= arrival + TRACKED_POST_ARRIVAL_RETENTION_SECONDS;
}

/* Normalize only the local render snapshot.  Persisted provider data remains
 * intact, while every tracker view consumes one consistent terminal state. */
static void tracked_reconcile_terminal_snapshot(
    app_remote_status_t *remote,
    int64_t now)
{
    if (!tracked_is_terminal(remote, now)) {
        return;
    }
    snprintf(
        remote->tracked_flight_journey_phase,
        sizeof(remote->tracked_flight_journey_phase),
        "arrived");
    snprintf(
        remote->tracked_flight_phase,
        sizeof(remote->tracked_flight_phase),
        "landed");
    snprintf(
        remote->tracked_flight_remaining,
        sizeof(remote->tracked_flight_remaining),
        "0m");
    remote->route_progress = 1.0f;
    remote->tracked_flight_altitude_m = 0;
    remote->tracked_flight_speed_kph = 0;
    if (remote->tracked_flight_actual_arrival_unix <= 0) {
        remote->tracked_flight_actual_arrival_unix =
            remote->tracked_flight_estimated_arrival_unix;
    }
    if (tracked_is_expired_after_arrival(remote, now)) {
        remote->tracked_flight_valid = false;
    }
}

static bool tracked_is_preflight(
    const app_remote_status_t *remote)
{
    if (remote == NULL) {
        return true;
    }
    if (strcasecmp(
            remote->tracked_flight_journey_phase,
            "preflight") == 0) {
        return true;
    }
    if (strcasecmp(
            remote->tracked_flight_journey_phase,
            "enroute") == 0 ||
        strcasecmp(
            remote->tracked_flight_journey_phase,
            "arrived") == 0) {
        return false;
    }
    return remote->tracked_flight_actual_departure_unix <= 0 &&
        remote->route_progress <= 0.001f;
}

static void tracked_format_local_time(
    int64_t unix_time,
    int utc_offset_seconds,
    char *out,
    size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    if (unix_time <= 0) {
        snprintf(out, out_size, "-");
        return;
    }
    const time_t local_epoch =
        (time_t)(unix_time + utc_offset_seconds);
    struct tm local_time = {0};
    gmtime_r(&local_epoch, &local_time);
    snprintf(
        out,
        out_size,
        "%02d:%02d",
        local_time.tm_hour,
        local_time.tm_min);
}

static void tracked_format_duration(
    int64_t seconds,
    char *out,
    size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    if (seconds < 0) {
        snprintf(out, out_size, "-");
        return;
    }
    const int64_t total_minutes =
        (seconds + 30) / 60;
    const int64_t hours = total_minutes / 60;
    const int64_t minutes = total_minutes % 60;
    if (hours > 0) {
        snprintf(
            out,
            out_size,
            "%lldh %02lldm",
            (long long)hours,
            (long long)minutes);
    } else {
        snprintf(
            out,
            out_size,
            "%lldm",
            (long long)minutes);
    }
}

static void tracked_copy_or_dash(
    char *out,
    size_t out_size,
    const char *value)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    if (value == NULL || value[0] == '\0') {
        snprintf(out, out_size, "-");
        return;
    }

    size_t source = 0;
    size_t destination = 0;
    while (value[source] != '\0' &&
           destination + 1 < out_size) {
        const uint8_t first =
            (uint8_t)value[source];
        if (first == 0xe2 &&
            value[source + 1] != '\0' &&
            value[source + 2] != '\0' &&
            (uint8_t)value[source + 1] == 0x89 &&
            (uint8_t)value[source + 2] == 0x88) {
            out[destination++] = '~';
            source += 3;
            continue;
        }
        if (first == 0xe2 &&
            value[source + 1] != '\0' &&
            value[source + 2] != '\0' &&
            (uint8_t)value[source + 1] == 0x88 &&
            (uint8_t)value[source + 2] == 0xbc) {
            out[destination++] = '~';
            source += 3;
            continue;
        }
        if (first == 0xc2 &&
            value[source + 1] != '\0' &&
            (uint8_t)value[source + 1] == 0xa0) {
            out[destination++] = ' ';
            source += 2;
            continue;
        }
        if (first == 0xe2 &&
            value[source + 1] != '\0' &&
            value[source + 2] != '\0' &&
            (uint8_t)value[source + 1] == 0x80 &&
            (uint8_t)value[source + 2] == 0xaf) {
            out[destination++] = ' ';
            source += 3;
            continue;
        }
        if (first < 0x80) {
            out[destination++] = value[source++];
            continue;
        }

        out[destination++] = ' ';
        source++;
        while (((uint8_t)value[source] & 0xc0) == 0x80) {
            source++;
        }
    }
    out[destination] = '\0';
}

static void tracked_time_pair(
    const app_remote_status_t *remote,
    int page,
    tracked_time_pair_t *pair)
{
    if (remote == NULL || pair == NULL) {
        return;
    }
    memset(pair, 0, sizeof(*pair));
    const int64_t now = (int64_t)time(NULL);
    const bool preflight = tracked_is_preflight(remote);
    const int64_t arrival =
        remote->tracked_flight_actual_arrival_unix > 0
            ? remote->tracked_flight_actual_arrival_unix
            : remote->tracked_flight_estimated_arrival_unix;
    const int64_t departure =
        remote->tracked_flight_actual_departure_unix > 0
            ? remote->tracked_flight_actual_departure_unix
            : remote->tracked_flight_estimated_departure_unix;

    if (preflight && page == 0) {
        snprintf(
            pair->title[0],
            sizeof(pair->title[0]),
            "%.7s TIME",
            remote->tracked_flight_origin[0] != '\0'
                ? remote->tracked_flight_origin
                : "DEP");
        tracked_format_local_time(
            now,
            remote
                ->tracked_flight_origin_current_utc_offset_s,
            pair->value[0],
            sizeof(pair->value[0]));
        snprintf(
            pair->title[1],
            sizeof(pair->title[1]),
            "ETD");
        tracked_format_local_time(
            remote->tracked_flight_estimated_departure_unix,
            remote->tracked_flight_departure_utc_offset_s,
            pair->value[1],
            sizeof(pair->value[1]));
        return;
    }
    if (preflight) {
        snprintf(
            pair->title[0],
            sizeof(pair->title[0]),
            "DURATION");
        if (departure > 0 && arrival > departure) {
            tracked_format_duration(
                arrival - departure,
                pair->value[0],
                sizeof(pair->value[0]));
        } else {
            tracked_copy_or_dash(
                pair->value[0],
                sizeof(pair->value[0]),
                remote->tracked_flight_duration);
        }
        snprintf(
            pair->title[1],
            sizeof(pair->title[1]),
            "ETA");
        tracked_format_local_time(
            arrival,
            remote->tracked_flight_arrival_utc_offset_s,
            pair->value[1],
            sizeof(pair->value[1]));
        return;
    }

    if (page == 0) {
        snprintf(
            pair->title[0],
            sizeof(pair->title[0]),
            "%.7s TIME",
            remote->tracked_flight_destination[0] != '\0'
                ? remote->tracked_flight_destination
                : "DEST");
        tracked_format_local_time(
            now,
            remote
                ->tracked_flight_destination_current_utc_offset_s,
            pair->value[0],
            sizeof(pair->value[0]));
        snprintf(
            pair->title[1],
            sizeof(pair->title[1]),
            "ETA");
        tracked_format_local_time(
            arrival,
            remote->tracked_flight_arrival_utc_offset_s,
            pair->value[1],
            sizeof(pair->value[1]));
        return;
    }

    snprintf(
        pair->title[0],
        sizeof(pair->title[0]),
        "ELAPSED");
    if (remote->tracked_flight_actual_departure_unix > 0) {
        const int64_t elapsed_end =
            remote->tracked_flight_actual_arrival_unix > 0
                ? remote->tracked_flight_actual_arrival_unix
                : now;
        tracked_format_duration(
            elapsed_end -
                remote->tracked_flight_actual_departure_unix,
            pair->value[0],
            sizeof(pair->value[0]));
    } else {
        tracked_copy_or_dash(
            pair->value[0],
            sizeof(pair->value[0]),
            remote->tracked_flight_elapsed);
    }
    snprintf(
        pair->title[1],
        sizeof(pair->title[1]),
        "LEFT");
    if (remote->tracked_flight_actual_arrival_unix > 0) {
        tracked_format_duration(
            0,
            pair->value[1],
            sizeof(pair->value[1]));
    } else if (arrival > 0) {
        tracked_format_duration(
            arrival - now,
            pair->value[1],
            sizeof(pair->value[1]));
    } else {
        tracked_copy_or_dash(
            pair->value[1],
            sizeof(pair->value[1]),
            remote->tracked_flight_remaining);
    }
}

static lv_obj_t *tracked_time_page(
    lv_obj_t *parent,
    int x,
    int y,
    int width,
    int height,
    lv_opa_t opacity)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, x, y);
    lv_obj_set_size(page, width, height);
    lv_obj_remove_flag(
        page,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(page, opacity, 0);
    return page;
}

static void tracked_object_opacity_animation(
    void *object,
    int32_t opacity)
{
    lv_obj_set_style_opa(
        (lv_obj_t *)object,
        (lv_opa_t)opacity,
        0);
}

static void tracked_start_time_page_transition(
    uint8_t next_page)
{
    if (next_page > 1) {
        return;
    }
    if (next_page == tracked_time_page_index ||
        tracked_time_pages[0] == NULL ||
        tracked_time_pages[1] == NULL) {
        tracked_time_page_index = next_page;
        return;
    }
    const int page_base_y = tracked_time_page_base_y;
    lv_obj_t *current_page =
        tracked_time_pages[tracked_time_page_index];
    lv_obj_t *incoming_page =
        tracked_time_pages[next_page];
    lv_obj_set_y(current_page, page_base_y);
    lv_obj_set_y(incoming_page, page_base_y + 14);
    lv_obj_set_style_opa(
        incoming_page,
        LV_OPA_TRANSP,
        0);

    lv_anim_t fade_out;
    lv_anim_init(&fade_out);
    lv_anim_set_var(
        &fade_out,
        current_page);
    lv_anim_set_exec_cb(
        &fade_out,
        tracked_object_opacity_animation);
    lv_anim_set_values(
        &fade_out,
        LV_OPA_COVER,
        LV_OPA_TRANSP);
    lv_anim_set_duration(&fade_out, 420);
    lv_anim_set_path_cb(
        &fade_out,
        lv_anim_path_ease_in_out);
    lv_anim_start(&fade_out);

    lv_anim_t slide_out;
    lv_anim_init(&slide_out);
    lv_anim_set_var(&slide_out, current_page);
    lv_anim_set_exec_cb(
        &slide_out,
        tracked_panel_y_animation);
    lv_anim_set_values(
        &slide_out,
        page_base_y,
        page_base_y - 14);
    lv_anim_set_duration(&slide_out, 460);
    lv_anim_set_path_cb(
        &slide_out,
        lv_anim_path_ease_in_out);
    lv_anim_start(&slide_out);

    lv_anim_t fade_in;
    lv_anim_init(&fade_in);
    lv_anim_set_var(
        &fade_in,
        incoming_page);
    lv_anim_set_exec_cb(
        &fade_in,
        tracked_object_opacity_animation);
    lv_anim_set_values(
        &fade_in,
        LV_OPA_TRANSP,
        LV_OPA_COVER);
    lv_anim_set_duration(&fade_in, 420);
    lv_anim_set_path_cb(
        &fade_in,
        lv_anim_path_ease_in_out);
    lv_anim_start(&fade_in);

    lv_anim_t slide_in;
    lv_anim_init(&slide_in);
    lv_anim_set_var(&slide_in, incoming_page);
    lv_anim_set_exec_cb(
        &slide_in,
        tracked_panel_y_animation);
    lv_anim_set_values(
        &slide_in,
        page_base_y + 14,
        page_base_y);
    lv_anim_set_duration(&slide_in, 460);
    lv_anim_set_path_cb(
        &slide_in,
        lv_anim_path_ease_in_out);
    lv_anim_start(&slide_in);

    /* Gate/terminal labels live in their own tight bounds on the detailed
     * screen. Cross-fading only these labels avoids invalidating the static
     * yellow sign shell and arrow. */
    for (int side = 0; side < 2; side++) {
        lv_obj_t *current_gate =
            tracked_gate_value_pages[side][tracked_time_page_index];
        lv_obj_t *incoming_gate =
            tracked_gate_value_pages[side][next_page];
        if (current_gate == NULL || incoming_gate == NULL) {
            continue;
        }
        lv_obj_set_style_opa(incoming_gate, LV_OPA_TRANSP, 0);

        lv_anim_t gate_fade_out;
        lv_anim_init(&gate_fade_out);
        lv_anim_set_var(&gate_fade_out, current_gate);
        lv_anim_set_exec_cb(
            &gate_fade_out,
            tracked_object_opacity_animation);
        lv_anim_set_values(
            &gate_fade_out,
            LV_OPA_COVER,
            LV_OPA_TRANSP);
        lv_anim_set_duration(&gate_fade_out, 320);
        lv_anim_set_path_cb(
            &gate_fade_out,
            lv_anim_path_ease_in_out);
        lv_anim_start(&gate_fade_out);

        lv_anim_t gate_fade_in;
        lv_anim_init(&gate_fade_in);
        lv_anim_set_var(&gate_fade_in, incoming_gate);
        lv_anim_set_exec_cb(
            &gate_fade_in,
            tracked_object_opacity_animation);
        lv_anim_set_values(
            &gate_fade_in,
            LV_OPA_TRANSP,
            LV_OPA_COVER);
        lv_anim_set_duration(&gate_fade_in, 320);
        lv_anim_set_path_cb(
            &gate_fade_in,
            lv_anim_path_ease_in_out);
        lv_anim_start(&gate_fade_in);
    }
    tracked_time_page_index = next_page;
}

static void tracked_prepare_time_rotation(
    const app_remote_status_t *remote)
{
    if (remote == NULL) {
        return;
    }
    const bool changed =
        strcasecmp(
            tracked_time_flight_seen,
            remote->tracked_flight_number) != 0 ||
        strcasecmp(
            tracked_time_journey_seen,
            remote->tracked_flight_journey_phase) != 0;
    if (changed ||
        tracked_time_page_epoch_us <= 0) {
        snprintf(
            tracked_time_flight_seen,
            sizeof(tracked_time_flight_seen),
            "%.7s",
            remote->tracked_flight_number);
        snprintf(
            tracked_time_journey_seen,
            sizeof(tracked_time_journey_seen),
            "%s",
            remote->tracked_flight_journey_phase);
        tracked_time_page_index = 0;
        tracked_time_page_epoch_us =
            esp_timer_get_time();
    }
}

static lv_color_t tracked_trend_color(
    tracked_metric_trend_t trend)
{
    switch (trend) {
    case TRACKED_METRIC_UP:
        return lv_color_hex(0x38d879);
    case TRACKED_METRIC_DOWN:
        return lv_color_hex(0xff626b);
    case TRACKED_METRIC_STEADY:
    default:
        return lv_color_hex(0xf0f3f0);
    }
}

static tracked_metric_trend_t tracked_metric_trend(
    int previous,
    int current,
    int minimum_delta)
{
    if (previous <= 0 || current <= 0 || previous == current) {
        return TRACKED_METRIC_STEADY;
    }
    const int percent_delta =
        (int)lroundf(fabsf((float)previous) * 0.015f);
    const int threshold =
        percent_delta > minimum_delta
            ? percent_delta
            : minimum_delta;
    const int delta = current - previous;
    if (delta >= threshold) {
        return TRACKED_METRIC_UP;
    }
    if (delta <= -threshold) {
        return TRACKED_METRIC_DOWN;
    }
    return TRACKED_METRIC_STEADY;
}

static void tracked_update_metric_trends(
    const app_remote_status_t *remote)
{
    if (remote == NULL ||
        remote->state_pushes ==
            tracked_metric_state_pushes_seen) {
        return;
    }
    tracked_metric_state_pushes_seen =
        remote->state_pushes;

    const bool new_flight =
        strcasecmp(
            tracked_metric_flight_seen,
            remote->tracked_flight_number) != 0;
    if (new_flight) {
        snprintf(
            tracked_metric_flight_seen,
            sizeof(tracked_metric_flight_seen),
            "%s",
            remote->tracked_flight_number);
        tracked_altitude_trend = TRACKED_METRIC_STEADY;
        tracked_speed_trend = TRACKED_METRIC_STEADY;
        tracked_metric_altitude_seen =
            remote->tracked_flight_altitude_m;
        tracked_metric_speed_seen =
            remote->tracked_flight_speed_kph;
        return;
    }

    if (remote->tracked_flight_altitude_m > 0 &&
        remote->tracked_flight_altitude_m !=
            tracked_metric_altitude_seen) {
        tracked_altitude_trend = tracked_metric_trend(
            tracked_metric_altitude_seen,
            remote->tracked_flight_altitude_m,
            50);
        tracked_metric_altitude_seen =
            remote->tracked_flight_altitude_m;
    }
    if (remote->tracked_flight_speed_kph > 0 &&
        remote->tracked_flight_speed_kph !=
            tracked_metric_speed_seen) {
        tracked_speed_trend = tracked_metric_trend(
            tracked_metric_speed_seen,
            remote->tracked_flight_speed_kph,
            10);
        tracked_metric_speed_seen =
            remote->tracked_flight_speed_kph;
    }
}

static void tracked_draw_globe_panels(const app_remote_status_t *remote,
                                      const app_aircraft_t *aircraft,
                                      const char *altitude,
                                      const char *speed,
                                      const char *distance_from,
                                      const char *distance_to,
                                      lv_color_t altitude_color,
                                      lv_color_t speed_color)
{
    tracked_panel_primary = tracked_gradient_panel(
        0,
        0,
        SCREEN_SIZE,
        98,
        TRACKED_PANEL_TOP);
    tracked_panel_secondary = tracked_gradient_panel(
        0,
        350,
        SCREEN_SIZE,
        116,
        TRACKED_PANEL_BOTTOM);

    if (remote->tracked_flight_airline_iata[0] != '\0') {
        uint32_t logo_revision = 0;
        lv_obj_t *logo = draw_airline_logo(
            tracked_panel_primary,
            remote->tracked_flight_airline_iata,
            CX,
            25,
            &logo_revision);
        if (logo != NULL) {
            tracked_logo_revision_seen = logo_revision;
        }
    }
    lv_obj_t *flight_number = label_box(
        tracked_panel_primary,
        remote->tracked_flight_number,
        &lv_font_montserrat_20,
        lv_color_hex(0xffffff),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        180);
    lv_obj_set_pos(flight_number, CX - 90, 53);
    lv_obj_set_style_text_align(
        flight_number,
        LV_TEXT_ALIGN_CENTER,
        0);

    tracked_draw_divider(
        tracked_panel_secondary,
        105,
        56,
        256);
    for (int page = 0; page < 2; page++) {
        tracked_time_pair_t pair;
        tracked_time_pair(remote, page, &pair);
        tracked_time_pages[page] =
            tracked_time_page(
                tracked_panel_secondary,
                0,
                0,
                SCREEN_SIZE,
                116,
                page == tracked_time_page_index
                    ? LV_OPA_COVER
                    : LV_OPA_TRANSP);
        if (page == 0) {
            /* Center the visible glyphs and values as one measured row. */
            const int metric_icon_inset = 13;
            const int metric_icon_to_value = 16;
            const int metric_gap = 24;
            const char *heading =
                detail_compass_direction(aircraft);
            const int altitude_width =
                tracked_inline_text_width(altitude);
            const int speed_width =
                tracked_inline_text_width(speed);
            const int heading_width =
                tracked_inline_text_width(heading);
            const int altitude_item_width =
                metric_icon_inset + metric_icon_to_value +
                altitude_width;
            const int speed_item_width =
                metric_icon_inset + metric_icon_to_value +
                speed_width;
            const int heading_item_width =
                metric_icon_inset + metric_icon_to_value +
                heading_width;
            const int metric_row_width =
                altitude_item_width + speed_item_width +
                heading_item_width + metric_gap * 2;
            int metric_cursor =
                (SCREEN_SIZE - metric_row_width) / 2;
            tracked_inline_metric(
                tracked_time_pages[page],
                &airspace_detail_metric_altitude,
                metric_cursor + metric_icon_inset,
                4,
                altitude_width,
                altitude,
                altitude_color);
            metric_cursor +=
                altitude_item_width + metric_gap;
            tracked_inline_metric(
                tracked_time_pages[page],
                &airspace_detail_metric_speed,
                metric_cursor + metric_icon_inset,
                4,
                speed_width,
                speed,
                speed_color);
            metric_cursor += speed_item_width + metric_gap;
            tracked_inline_compass(
                tracked_time_pages[page],
                metric_cursor + metric_icon_inset,
                4,
                heading_width,
                heading,
                aircraft->track_deg,
                aircraft->track_valid);
        } else {
            char from_title[20];
            char to_title[20];
            snprintf(
                from_title,
                sizeof(from_title),
                "FROM %.7s",
                remote->tracked_flight_origin[0] != '\0'
                    ? remote->tracked_flight_origin
                    : "DEP");
            snprintf(
                to_title,
                sizeof(to_title),
                "TO %.7s",
                remote->tracked_flight_destination[0] != '\0'
                    ? remote->tracked_flight_destination
                    : "DEST");
            tracked_value(
                tracked_time_pages[page],
                76,
                4,
                140,
                from_title,
                distance_from,
                lv_color_hex(0xf0f3f0));
            tracked_value(
                tracked_time_pages[page],
                250,
                4,
                140,
                to_title,
                distance_to,
                lv_color_hex(0xf0f3f0));
        }
        tracked_footer_value(
            tracked_time_pages[page],
            180,
            pair.title[0],
            pair.value[0],
            tracked_time_value_color(
                pair.title[0],
                remote->tracked_flight_delayed,
                lv_color_hex(0xd8deda)));
        tracked_footer_value(
            tracked_time_pages[page],
            286,
            pair.title[1],
            pair.value[1],
            tracked_time_value_color(
                pair.title[1],
                remote->tracked_flight_delayed,
                lv_color_hex(0xd8deda)));
    }

    if (tracked_panels_entry_animation_pending) {
        tracked_animate_panel(
            tracked_panel_primary,
            false,
            -98,
            0,
            0,
            lv_anim_path_ease_out);
        tracked_animate_panel(
            tracked_panel_secondary,
            false,
            SCREEN_SIZE,
            350,
            45,
            lv_anim_path_ease_out);
    }
}

static void tracked_draw_frame_panels(const app_remote_status_t *remote,
                                      const app_aircraft_t *aircraft,
                                      const char *altitude,
                                      const char *speed,
                                      const char *distance_preferred,
                                      const char *distance_alternate,
                                      lv_color_t route_color,
                                      lv_color_t altitude_color,
                                      lv_color_t speed_color)
{
    tracked_panel_primary = tracked_gradient_panel(
        0,
        0,
        130,
        SCREEN_SIZE,
        TRACKED_PANEL_LEFT);
    tracked_panel_secondary = tracked_gradient_panel(
        SCREEN_SIZE - 130,
        0,
        130,
        SCREEN_SIZE,
        TRACKED_PANEL_RIGHT);

    tracked_metric(
        tracked_panel_primary,
        &airspace_detail_metric_altitude,
        58,
        96,
        altitude,
        altitude_color);
    tracked_metric(
        tracked_panel_primary,
        &airspace_detail_metric_speed,
        58,
        190,
        speed,
        speed_color);
    tracked_compass(
        tracked_panel_primary,
        58,
        284,
        detail_compass_direction(aircraft),
        aircraft->track_deg,
        aircraft->track_valid);

    for (int page = 0; page < 2; page++) {
        tracked_time_pair_t pair;
        tracked_time_pair(remote, page, &pair);
        tracked_time_pages[page] =
            tracked_time_page(
                tracked_panel_secondary,
                0,
                0,
                130,
                SCREEN_SIZE,
                page == tracked_time_page_index
                    ? LV_OPA_COVER
                    : LV_OPA_TRANSP);
        tracked_value(
            tracked_time_pages[page],
            8,
            108,
            114,
            pair.title[0],
            pair.value[0],
            lv_color_hex(0xf1f4f2));
        tracked_draw_divider(
            tracked_time_pages[page],
            12,
            174,
            106);
        tracked_value(
            tracked_time_pages[page],
            8,
            201,
            114,
            "DIST LEFT",
            page == 0
                ? distance_preferred
                : distance_alternate,
            lv_color_hex(0xf1f4f2));
        tracked_draw_divider(
            tracked_time_pages[page],
            12,
            268,
            106);
        tracked_value(
            tracked_time_pages[page],
            8,
            295,
            114,
            pair.title[1],
            pair.value[1],
            tracked_time_value_color(
                pair.title[1],
                remote->tracked_flight_delayed,
                page == 0
                    ? route_color
                    : lv_color_hex(0xf1f4f2)));
    }

    if (tracked_panels_entry_animation_pending) {
        tracked_animate_panel(
            tracked_panel_primary,
            true,
            -130,
            0,
            0,
            lv_anim_path_ease_out);
        tracked_animate_panel(
            tracked_panel_secondary,
            true,
            SCREEN_SIZE,
            SCREEN_SIZE - 130,
            45,
            lv_anim_path_ease_out);
    }
}

static void tracked_hide_panels(void)
{
    if (tracked_panel_primary == NULL ||
        tracked_panel_secondary == NULL) {
        return;
    }
    if (tracked_flight_view == TRACKED_FLIGHT_FRAME) {
        tracked_animate_panel(
            tracked_panel_primary,
            true,
            lv_obj_get_x(tracked_panel_primary),
            -130,
            0,
            lv_anim_path_ease_in);
        tracked_animate_panel(
            tracked_panel_secondary,
            true,
            lv_obj_get_x(tracked_panel_secondary),
            SCREEN_SIZE,
            0,
            lv_anim_path_ease_in);
    } else {
        tracked_animate_panel(
            tracked_panel_primary,
            false,
            lv_obj_get_y(tracked_panel_primary),
            -98,
            0,
            lv_anim_path_ease_in);
        tracked_animate_panel(
            tracked_panel_secondary,
            false,
            lv_obj_get_y(tracked_panel_secondary),
            SCREEN_SIZE,
            0,
            lv_anim_path_ease_in);
    }
}

static int tracked_duration_seconds(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    int total = 0;
    const char *cursor = value;
    while (*cursor != '\0') {
        while (*cursor != '\0' &&
               !isdigit((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        char *end = NULL;
        const long amount = strtol(cursor, &end, 10);
        if (end == cursor) {
            cursor++;
            continue;
        }
        cursor = end;
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == 'h' || *cursor == 'H') {
            total += (int)amount * 3600;
        } else if (*cursor == 'm' || *cursor == 'M') {
            total += (int)amount * 60;
        }
        if (*cursor != '\0') {
            cursor++;
        }
    }
    return total;
}

static void tracked_prepare_route_geometry(
    const app_remote_status_t *remote,
    bool frame_view)
{
    tracked_route_point_count =
        APP_REMOTE_TRACKED_ROUTE_MAX_POINTS;
    if (frame_view) {
        for (int index = 0;
             index < tracked_route_point_count;
             index++) {
            const float fraction =
                (float)index /
                (float)(tracked_route_point_count - 1);
            tracked_route_points[index][0] = CX;
            tracked_route_points[index][1] =
                370 -
                (int16_t)lroundf(fraction * 274.0f);
        }
        return;
    }

    if (remote != NULL &&
        remote->tracked_route_point_count >= 2 &&
        remote->tracked_route_point_count <=
            APP_REMOTE_TRACKED_ROUTE_MAX_POINTS) {
        tracked_route_point_count =
            remote->tracked_route_point_count;
        memcpy(
            tracked_route_points,
            remote->tracked_route_points,
            tracked_route_point_count *
                sizeof(tracked_route_points[0]));
        return;
    }

    for (int index = 0;
         index < tracked_route_point_count;
         index++) {
        const float fraction =
            (float)index /
            (float)(tracked_route_point_count - 1);
        tracked_route_points[index][0] =
            72 +
            (int16_t)lroundf(fraction * 322.0f);
        tracked_route_points[index][1] =
            CX -
            (int16_t)lroundf(
                sinf(fraction * (float)M_PI) *
                78.0f);
    }
}

static void tracked_route_sample(float progress,
                                 bool frame_view,
                                 float *sample_x,
                                 float *sample_y,
                                 float *tangent_x,
                                 float *tangent_y)
{
    const float bounded =
        fminf(1.0f, fmaxf(0.0f, progress));
    if (tracked_route_point_count < 2) {
        *sample_x = CX;
        *sample_y = CY;
        *tangent_x = 0.0f;
        *tangent_y = -1.0f;
        return;
    }

    const float scaled =
        bounded *
        (float)(tracked_route_point_count - 1);
    int segment = (int)floorf(scaled);
    if (segment >= tracked_route_point_count - 1) {
        segment = tracked_route_point_count - 2;
    }
    const float local =
        fminf(1.0f, fmaxf(0.0f, scaled - segment));
    if (frame_view) {
        const float x0 =
            (float)tracked_route_points[segment][0];
        const float y0 =
            (float)tracked_route_points[segment][1];
        const float x1 =
            (float)tracked_route_points[segment + 1][0];
        const float y1 =
            (float)tracked_route_points[segment + 1][1];
        *sample_x = x0 + (x1 - x0) * local;
        *sample_y = y0 + (y1 - y0) * local;
        *tangent_x = x1 - x0;
        *tangent_y = y1 - y0;
        return;
    }

    /*
     * The pushed map contains a compact route polyline.  Sample a single
     * Catmull-Rom spline through those control points so both the drawn path
     * and the moving aircraft share the same smooth geometry.
     */
    const int index0 = segment > 0 ? segment - 1 : segment;
    const int index1 = segment;
    const int index2 = segment + 1;
    const int index3 = segment + 2 < tracked_route_point_count
        ? segment + 2
        : segment + 1;
    const float t2 = local * local;
    const float t3 = t2 * local;
    const float p0x = tracked_route_points[index0][0];
    const float p0y = tracked_route_points[index0][1];
    const float p1x = tracked_route_points[index1][0];
    const float p1y = tracked_route_points[index1][1];
    const float p2x = tracked_route_points[index2][0];
    const float p2y = tracked_route_points[index2][1];
    const float p3x = tracked_route_points[index3][0];
    const float p3y = tracked_route_points[index3][1];
    *sample_x = 0.5f * (
        2.0f * p1x +
        (-p0x + p2x) * local +
        (2.0f * p0x - 5.0f * p1x +
         4.0f * p2x - p3x) * t2 +
        (-p0x + 3.0f * p1x -
         3.0f * p2x + p3x) * t3);
    *sample_y = 0.5f * (
        2.0f * p1y +
        (-p0y + p2y) * local +
        (2.0f * p0y - 5.0f * p1y +
         4.0f * p2y - p3y) * t2 +
        (-p0y + 3.0f * p1y -
         3.0f * p2y + p3y) * t3);
    *tangent_x = 0.5f * (
        (-p0x + p2x) +
        2.0f * (2.0f * p0x - 5.0f * p1x +
                4.0f * p2x - p3x) * local +
        3.0f * (-p0x + 3.0f * p1x -
                3.0f * p2x + p3x) * t2);
    *tangent_y = 0.5f * (
        (-p0y + p2y) +
        2.0f * (2.0f * p0y - 5.0f * p1y +
                4.0f * p2y - p3y) * local +
        3.0f * (-p0y + 3.0f * p1y -
                3.0f * p2y + p3y) * t2);
}

static void tracked_aircraft_geometry(float progress,
                                      bool frame_view,
                                      int *x,
                                      int *y,
                                      int *heading)
{
    float sample_x = CX;
    float sample_y = CY;
    float tangent_x = 0.0f;
    float tangent_y = -1.0f;
    tracked_route_sample(
        progress,
        frame_view,
        &sample_x,
        &sample_y,
        &tangent_x,
        &tangent_y);
    *x = (int)lroundf(
        fminf(SCREEN_SIZE - 1.0f, fmaxf(0.0f, sample_x)));
    *y = (int)lroundf(
        fminf(SCREEN_SIZE - 1.0f, fmaxf(0.0f, sample_y)));
    *heading = (int)lroundf(
        atan2f(tangent_x, -tangent_y) /
        DEG_TO_RAD);
    if (*heading < 0) {
        *heading += 360;
    }
}

static void tracked_route_rasterize_segment(
    uint8_t *mask,
    int mask_width,
    int mask_height,
    int origin_x,
    int origin_y,
    float global_x0,
    float global_y0,
    float global_x1,
    float global_y1)
{
    const float x0 = global_x0 - (float)origin_x;
    const float y0 = global_y0 - (float)origin_y;
    const float x1 = global_x1 - (float)origin_x;
    const float y1 = global_y1 - (float)origin_y;
    const float vx = x1 - x0;
    const float vy = y1 - y0;
    const float length_squared = vx * vx + vy * vy;
    if (length_squared < 0.0001f) {
        return;
    }

    const float solid_radius = 1.65f;
    const float outer_radius = 2.55f;
    const float solid_squared = solid_radius * solid_radius;
    const float outer_squared = outer_radius * outer_radius;
    int min_x =
        (int)floorf(fminf(x0, x1) - outer_radius);
    int max_x =
        (int)ceilf(fmaxf(x0, x1) + outer_radius);
    int min_y =
        (int)floorf(fminf(y0, y1) - outer_radius);
    int max_y =
        (int)ceilf(fmaxf(y0, y1) + outer_radius);
    min_x = min_x < 0 ? 0 : min_x;
    min_y = min_y < 0 ? 0 : min_y;
    max_x = max_x >= mask_width
        ? mask_width - 1
        : max_x;
    max_y = max_y >= mask_height
        ? mask_height - 1
        : max_y;

    for (int pixel_y = min_y;
         pixel_y <= max_y;
         pixel_y++) {
        for (int pixel_x = min_x;
             pixel_x <= max_x;
             pixel_x++) {
            const float sample_x =
                (float)pixel_x + 0.5f;
            const float sample_y =
                (float)pixel_y + 0.5f;
            float projection =
                ((sample_x - x0) * vx +
                 (sample_y - y0) * vy) /
                length_squared;
            projection =
                fmaxf(0.0f, fminf(1.0f, projection));
            const float closest_x =
                x0 + projection * vx;
            const float closest_y =
                y0 + projection * vy;
            const float dx = sample_x - closest_x;
            const float dy = sample_y - closest_y;
            const float distance_squared =
                dx * dx + dy * dy;
            if (distance_squared >= outer_squared) {
                continue;
            }
            uint8_t alpha = 255;
            if (distance_squared > solid_squared) {
                const float distance =
                    sqrtf(distance_squared);
                alpha = (uint8_t)lroundf(
                    (outer_radius - distance) /
                    (outer_radius - solid_radius) *
                    255.0f);
            }
            uint8_t *destination =
                &mask[pixel_y * mask_width + pixel_x];
            if (alpha > *destination) {
                *destination = alpha;
            }
        }
    }
}

static lv_obj_t *tracked_route_spline_image(
    int point_count,
    lv_color_t color)
{
    if (point_count < 2) {
        return NULL;
    }
    if (tracked_route_mask_buffer == NULL) {
        tracked_route_mask_buffer = heap_caps_malloc(
            (size_t)SCREEN_SIZE * SCREEN_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (tracked_route_mask_buffer == NULL) {
        return NULL;
    }

    float min_x = tracked_route_render_samples[0][0];
    float max_x = min_x;
    float min_y = tracked_route_render_samples[0][1];
    float max_y = min_y;
    for (int index = 1;
         index < point_count;
         index++) {
        min_x = fminf(
            min_x,
            tracked_route_render_samples[index][0]);
        max_x = fmaxf(
            max_x,
            tracked_route_render_samples[index][0]);
        min_y = fminf(
            min_y,
            tracked_route_render_samples[index][1]);
        max_y = fmaxf(
            max_y,
            tracked_route_render_samples[index][1]);
    }
    const int margin = 4;
    const int origin_x = (int)fmaxf(
        0.0f,
        floorf(min_x) - margin);
    const int origin_y = (int)fmaxf(
        0.0f,
        floorf(min_y) - margin);
    const int end_x = (int)fminf(
        SCREEN_SIZE - 1.0f,
        ceilf(max_x) + margin);
    const int end_y = (int)fminf(
        SCREEN_SIZE - 1.0f,
        ceilf(max_y) + margin);
    const int mask_width = end_x - origin_x + 1;
    const int mask_height = end_y - origin_y + 1;
    if (mask_width <= 0 || mask_height <= 0) {
        return NULL;
    }

    memset(
        tracked_route_mask_buffer,
        0,
        (size_t)mask_width * mask_height);
    for (int index = 1;
         index < point_count;
         index++) {
        tracked_route_rasterize_segment(
            tracked_route_mask_buffer,
            mask_width,
            mask_height,
            origin_x,
            origin_y,
            tracked_route_render_samples[index - 1][0],
            tracked_route_render_samples[index - 1][1],
            tracked_route_render_samples[index][0],
            tracked_route_render_samples[index][1]);
    }

    memset(
        &tracked_route_mask_image,
        0,
        sizeof(tracked_route_mask_image));
    tracked_route_mask_image.header.magic =
        LV_IMAGE_HEADER_MAGIC;
    tracked_route_mask_image.header.cf =
        LV_COLOR_FORMAT_A8;
    tracked_route_mask_image.header.w = mask_width;
    tracked_route_mask_image.header.h = mask_height;
    tracked_route_mask_image.header.stride = mask_width;
    tracked_route_mask_image.data_size =
        (uint32_t)((size_t)mask_width * mask_height);
    tracked_route_mask_image.data =
        tracked_route_mask_buffer;

    lv_obj_t *image = lv_image_create(screen_root);
    lv_image_set_src(image, &tracked_route_mask_image);
    lv_obj_set_pos(image, origin_x, origin_y);
    lv_obj_set_style_image_recolor(image, color, 0);
    lv_obj_set_style_image_recolor_opa(
        image,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(
        image,
        (lv_opa_t)245,
        0);
    lv_obj_remove_flag(
        image,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return image;
}

static void tracked_draw_route(float progress,
                               lv_color_t color,
                               bool frame_view)
{
    if (tracked_route_point_count < 2) {
        return;
    }
    const float bounded =
        fminf(1.0f, fmaxf(0.0f, progress));

    if (bounded > 0.001f) {
        int completed_count =
            2 + (int)ceilf(
                bounded *
                (float)(TRACKED_ROUTE_RENDER_POINT_COUNT - 2));
        if (completed_count >
            TRACKED_ROUTE_RENDER_POINT_COUNT) {
            completed_count =
                TRACKED_ROUTE_RENDER_POINT_COUNT;
        }
        for (int index = 0;
             index < completed_count;
             index++) {
            const float sample_progress =
                bounded * (float)index /
                (float)(completed_count - 1);
            float sample_x = CX;
            float sample_y = CY;
            float tangent_x = 0.0f;
            float tangent_y = -1.0f;
            tracked_route_sample(
                sample_progress,
                frame_view,
                &sample_x,
                &sample_y,
                &tangent_x,
                &tangent_y);
            sample_x = fminf(
                SCREEN_SIZE - 1.0f,
                fmaxf(0.0f, sample_x));
            sample_y = fminf(
                SCREEN_SIZE - 1.0f,
                fmaxf(0.0f, sample_y));
            tracked_route_render_samples[index][0] =
                sample_x;
            tracked_route_render_samples[index][1] =
                sample_y;
            tracked_route_render_points[index].x =
                (lv_value_precise_t)lroundf(sample_x);
            tracked_route_render_points[index].y =
                (lv_value_precise_t)lroundf(sample_y);
        }

        if (completed_count >= 2) {
            lv_obj_t *completed_route = NULL;
            if (!frame_view) {
                completed_route = tracked_route_spline_image(
                    completed_count,
                    color);
            }
            if (completed_route == NULL) {
                completed_route = lv_line_create(screen_root);
                lv_line_set_points(
                    completed_route,
                    tracked_route_render_points,
                    completed_count);
                lv_obj_set_style_line_color(
                    completed_route,
                    color,
                    0);
                lv_obj_set_style_line_width(
                    completed_route,
                    4,
                    0);
                lv_obj_set_style_line_opa(
                    completed_route,
                    (lv_opa_t)245,
                    0);
                lv_obj_set_style_line_rounded(
                    completed_route,
                    true,
                    0);
                lv_obj_remove_flag(
                    completed_route,
                    LV_OBJ_FLAG_CLICKABLE |
                        LV_OBJ_FLAG_SCROLLABLE);
            }
        }
    }

    const int dash_count = 32;
    for (int dash = 0; dash < dash_count; dash++) {
        const float start =
            (float)dash / (float)dash_count;
        const float end =
            fminf(
                1.0f,
                start + 0.42f / (float)dash_count);
        if (end <= bounded + 0.002f) {
            continue;
        }
        const float clipped_start =
            fmaxf(start, bounded + 0.004f);
        if (end <= clipped_start) {
            continue;
        }
        int x1 = 0;
        int y1 = 0;
        int heading = 0;
        int x2 = 0;
        int y2 = 0;
        tracked_aircraft_geometry(
            clipped_start,
            frame_view,
            &x1,
            &y1,
            &heading);
        tracked_aircraft_geometry(
            end,
            frame_view,
            &x2,
            &y2,
            &heading);
        line(
            screen_root,
            x1,
            y1,
            x2,
            y2,
            color,
            2,
            (lv_opa_t)205);
    }

    dot(
        screen_root,
        tracked_route_points[0][0],
        tracked_route_points[0][1],
        8,
        color,
        (lv_opa_t)220);
    dot(
        screen_root,
        tracked_route_points[
            tracked_route_point_count - 1
        ][0],
        tracked_route_points[
            tracked_route_point_count - 1
        ][1],
        8,
        color,
        (lv_opa_t)220);
}

static void update_tracked_aircraft_animation(void)
{
    if (render_requested ||
        current_screen != SCREEN_TRACKED_FLIGHT ||
        tracked_flight_view == TRACKED_FLIGHT_DETAIL ||
        tracked_aircraft_holder == NULL ||
        tracked_aircraft_progress_epoch_us <= 0 ||
        tracked_aircraft_progress_per_second <= 0.0f) {
        return;
    }

    const float elapsed =
        (float)(esp_timer_get_time() -
                tracked_aircraft_progress_epoch_us) /
        1000000.0f;
    const float progress =
        fminf(
            1.0f,
            tracked_aircraft_progress_base +
                elapsed *
                    tracked_aircraft_progress_per_second);
    int x = CX;
    int y = CY;
    int heading = 0;
    tracked_aircraft_geometry(
        progress,
        tracked_aircraft_frame_view,
        &x,
        &y,
        &heading);
    lv_obj_set_pos(
        tracked_aircraft_holder,
        x -
            (tracked_aircraft_frame_view
                 ? 96
                 : 38),
        y - 38);
    if (tracked_aircraft_image != NULL) {
        lv_image_set_rotation(
            tracked_aircraft_image,
            ((heading + 180) % 360) * 10);
    }
}

static int tracked_format_scheduled_countdown(
    const app_remote_status_t *remote,
    int64_t now,
    char *days_out,
    size_t days_out_size,
    char *time_out,
    size_t time_out_size)
{
    if (remote == NULL ||
        days_out == NULL || days_out_size == 0 ||
        time_out == NULL || time_out_size == 0) {
        return 0;
    }
    const int64_t seconds =
        remote->tracked_flight_estimated_departure_unix - now;
    const int64_t remaining = seconds > 0 ? seconds : 0;
    const int days = (int)(remaining / 86400);
    const int hours = (int)((remaining / 3600) % 24);
    const int minutes = (int)((remaining / 60) % 60);
    if (days > 0) {
        snprintf(
            days_out,
            days_out_size,
            "%d",
            days);
    } else {
        days_out[0] = '\0';
    }
    snprintf(time_out, time_out_size, "%02d:%02d", hours, minutes);
    return days;
}

static void tracked_update_scheduled_countdown_labels(
    const app_remote_status_t *remote,
    int64_t now)
{
    if (remote == NULL || tracked_preflight_countdown_label == NULL) {
        return;
    }
    char days_text[20] = {0};
    char time_text[16] = "00:00";
    const int days = tracked_format_scheduled_countdown(
        remote,
        now,
        days_text,
        sizeof(days_text),
        time_text,
        sizeof(time_text));

    lv_label_set_text(tracked_preflight_countdown_label, time_text);
    if (days > 0 && tracked_preflight_days_label != NULL) {
        lv_label_set_text(tracked_preflight_days_label, days_text);
        lv_obj_remove_flag(tracked_preflight_days_label, LV_OBJ_FLAG_HIDDEN);
        if (tracked_preflight_days_units_label != NULL) {
            lv_label_set_text(
                tracked_preflight_days_units_label,
                days == 1 ? "DAY" : "DAYS");
            lv_obj_remove_flag(
                tracked_preflight_days_units_label,
                LV_OBJ_FLAG_HIDDEN);
        }
        if (tracked_preflight_gate_sign != NULL) {
            lv_obj_add_flag(
                tracked_preflight_gate_sign,
                LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_text_font(
            tracked_preflight_countdown_label,
            &lv_font_montserrat_24,
            0);
        lv_obj_set_pos(tracked_preflight_countdown_label, 33, 326);
        if (tracked_preflight_countdown_units_label != NULL) {
            lv_obj_set_pos(tracked_preflight_countdown_units_label, 33, 360);
        }
    } else {
        if (tracked_preflight_days_label != NULL) {
            lv_obj_add_flag(tracked_preflight_days_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (tracked_preflight_days_units_label != NULL) {
            lv_obj_add_flag(
                tracked_preflight_days_units_label,
                LV_OBJ_FLAG_HIDDEN);
        }
        if (tracked_preflight_gate_sign != NULL) {
            lv_obj_remove_flag(
                tracked_preflight_gate_sign,
                LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_text_font(
            tracked_preflight_countdown_label,
            &lv_font_montserrat_48,
            0);
        lv_obj_set_pos(tracked_preflight_countdown_label, 33, 244);
        if (tracked_preflight_countdown_units_label != NULL) {
            lv_obj_set_pos(tracked_preflight_countdown_units_label, 33, 301);
        }
    }
}

static void handle_tracked_time_rotation(void)
{
    if (current_screen != SCREEN_TRACKED_FLIGHT) {
        return;
    }

    app_remote_status_t expiry_status;
    app_remote_get_status(&expiry_status);
    const bool had_tracked_flight = expiry_status.tracked_flight_valid;
    tracked_reconcile_terminal_snapshot(
        &expiry_status,
        (int64_t)time(NULL));
    if (had_tracked_flight && !expiry_status.tracked_flight_valid) {
        if (!tracked_post_arrival_expiry_rendered) {
            tracked_post_arrival_expiry_rendered = true;
            tracked_flight_view = TRACKED_FLIGHT_GLOBE;
            render_requested = true;
        }
        return;
    }
    tracked_post_arrival_expiry_rendered = false;

    /* The scheduled-flight screen returns before the normal rotating time
     * panels are prepared.  Keep its countdown alive independently and
     * update only the label, leaving the map and logo untouched. */
    if (tracked_preflight_countdown_label != NULL) {
        const int64_t now = (int64_t)time(NULL);
        const int64_t minute = now > 0 ? now / 60 : -1;
        if (minute >= 0 &&
            minute != tracked_preflight_countdown_minute_seen) {
            app_remote_status_t remote;
            app_remote_get_status(&remote);
            const int64_t seconds =
                remote.tracked_flight_estimated_departure_unix - now;
            if (!remote.tracked_flight_valid ||
                !tracked_is_preflight(&remote) ||
                seconds <= 3600) {
                /* Crossed into the live-flight presentation window. */
                render_requested = true;
            } else {
                tracked_update_scheduled_countdown_labels(
                    &remote,
                    now);
            }
            tracked_preflight_countdown_minute_seen = minute;
        }
        return;
    }

    if (tracked_time_page_epoch_us <= 0) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    const int64_t elapsed =
        now_us - tracked_time_page_epoch_us;
    const int64_t interval_us = 5000000LL;
    if (elapsed >= interval_us) {
        const int64_t intervals =
            elapsed / interval_us;
        tracked_time_page_epoch_us +=
            intervals * interval_us;
        const uint8_t next_page =
            (uint8_t)(
                (tracked_time_page_index +
                 intervals) %
                2);
        tracked_start_time_page_transition(
            next_page);
    }

    const time_t now = time(NULL);
    if (now > 0) {
        const int minute = (int)(now / 60);
        if (tracked_time_minute_seen >= 0 &&
            minute != tracked_time_minute_seen) {
            render_requested = true;
        }
        tracked_time_minute_seen = minute;
    }
}

static void render_tracked_no_scheduled_flight(void)
{
    size_t map_len = 0;
    uint32_t map_revision = 0;
    int map_index = app_remote_radar_range_index_for_miles(
        radar_range_miles);
    const uint8_t *map = app_remote_get_radar_map_for_range(
        radar_range_miles,
        &map_len,
        &map_revision,
        &map_index);
    map = prepare_padded_map_tiles(
        (uint8_t)(MAP_KIND_RADAR_BASE + map_index),
        map_len,
        map,
        map_revision);

    draw_shell();
    if (map != NULL) {
        draw_pushed_map_tiles(map);
    }
    lv_obj_t *shade = card(
        screen_root,
        0,
        0,
        SCREEN_SIZE,
        SCREEN_SIZE,
        lv_color_hex(0x000000),
        (lv_opa_t)188,
        lv_color_hex(0x000000),
        LV_OPA_TRANSP,
        0);
    lv_obj_remove_flag(shade, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    label(
        screen_root,
        "NO SCHEDULED FLIGHT",
        &lv_font_montserrat_16,
        lv_color_hex(0xd5dbd7),
        LV_ALIGN_CENTER,
        0,
        0);
}

static bool tracked_should_show_countdown(
    const app_remote_status_t *remote)
{
    if (remote == NULL || !tracked_is_preflight(remote) ||
        remote->tracked_flight_estimated_departure_unix <= 0) {
        return false;
    }
    const int64_t seconds =
        remote->tracked_flight_estimated_departure_unix -
        (int64_t)time(NULL);
    return seconds > 3600;
}

static void tracked_draw_scheduled_countdown(
    const app_remote_status_t *remote)
{
    const int64_t now = (int64_t)time(NULL);

    lv_obj_t *shade = card(
        screen_root,
        0,
        0,
        SCREEN_SIZE,
        SCREEN_SIZE,
        lv_color_hex(0x000000),
        (lv_opa_t)122,
        lv_color_hex(0x000000),
        LV_OPA_TRANSP,
        0);
    lv_obj_remove_flag(shade, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    if (remote->tracked_flight_airline_iata[0] != '\0') {
        uint32_t logo_revision = 0;
        (void)draw_airline_logo(
            screen_root,
            remote->tracked_flight_airline_iata,
            CX,
            38,
            &logo_revision);
    }
    tracked_centered_label(
        screen_root,
        remote->tracked_flight_number,
        &lv_font_montserrat_18,
        lv_color_hex(0xf4f6f4),
        153,
        66,
        160);

    tracked_preflight_endpoint_text(
        screen_root,
        remote->tracked_flight_origin,
        remote->tracked_flight_origin_city,
        153);
    tracked_centered_label(
        screen_root,
        ">",
        &lv_font_montserrat_24,
        lv_color_hex(0x9aa49e),
        213,
        104,
        40);
    tracked_preflight_endpoint_text(
        screen_root,
        remote->tracked_flight_destination,
        remote->tracked_flight_destination_city,
        313);

    tracked_preflight_gate_sign = tracked_draw_static_gate_sign(
        screen_root,
        CX,
        348,
        true,
        remote->tracked_flight_origin_terminal,
        remote->tracked_flight_origin_gate);

    tracked_centered_label(
        screen_root,
        "DEPARTS IN",
        &lv_font_montserrat_12,
        lv_color_hex(0x8d9891),
        33,
        198,
        400);

    tracked_preflight_days_label = tracked_centered_label(
        screen_root,
        "",
        &lv_font_montserrat_48,
        lv_color_hex(radar_palette_active->text),
        33,
        226,
        400);
    tracked_preflight_days_units_label = tracked_centered_label(
        screen_root,
        "",
        &lv_font_montserrat_14,
        lv_color_hex(0x8d9891),
        33,
        283,
        400);
    tracked_preflight_countdown_label = tracked_centered_label(
        screen_root,
        "",
        &lv_font_montserrat_24,
        lv_color_hex(radar_palette_active->text),
        33,
        326,
        400);
    tracked_preflight_countdown_units_label = tracked_centered_label(
        screen_root,
        "HOURS  :  MINUTES",
        &lv_font_montserrat_12,
        lv_color_hex(0x7f8983),
        33,
        360,
        400);
    tracked_update_scheduled_countdown_labels(remote, now);
    tracked_preflight_countdown_minute_seen =
        now > 0 ? now / 60 : -1;
}

static void render_tracked_flight(void)
{
    app_remote_status_t remote;
    app_remote_get_status(&remote);
    tracked_reconcile_terminal_snapshot(
        &remote,
        (int64_t)time(NULL));
    if (!remote.tracked_flight_valid) {
        render_tracked_no_scheduled_flight();
        tracked_panels_entry_animation_pending = false;
        return;
    }
    const bool frame_view = tracked_flight_view == TRACKED_FLIGHT_FRAME;
    const int route_view_index = frame_view ? 1 : 0;
    size_t map_len = 0;
    uint32_t map_revision = 0;
    const uint8_t *map = app_remote_get_map_for_view(
        route_view_index,
        &map_len,
        &map_revision);
    map = prepare_padded_map_tiles(
        (uint8_t)(MAP_KIND_ROUTE + route_view_index),
        map_len,
        map,
        map_revision);

    draw_shell();
    if (map != NULL) {
        draw_pushed_map_tiles(map);
    } else {
        label(
            screen_root,
            "-",
            &lv_font_montserrat_28,
            lv_color_hex(0x4f5852),
            LV_ALIGN_CENTER,
            0,
            0);
    }
    if (tracked_should_show_countdown(&remote)) {
        tracked_draw_scheduled_countdown(&remote);
        tracked_panels_entry_animation_pending = false;
        return;
    }

    tracked_update_metric_trends(&remote);
    const lv_color_t route_color =
        frame_view
            ? lv_color_hex(0xf1f4f2)
            : lv_color_hex(radar_palette_active->main);
    const lv_color_t eta_color =
        remote.tracked_flight_delayed
            ? lv_color_hex(0xff686e)
            : lv_color_hex(0xf1f4f2);
    const lv_color_t altitude_color =
        tracked_trend_color(tracked_altitude_trend);
    const lv_color_t speed_color =
        tracked_trend_color(tracked_speed_trend);
    const float progress =
        fminf(1.0f, fmaxf(0.0f, remote.route_progress));
    tracked_prepare_time_rotation(&remote);
    const bool imperial = display_prefers_imperial();
    char distance_km[20] = "-";
    char distance_mi[20] = "-";
    char distance_from_km[20] = "-";
    char distance_from_mi[20] = "-";
    if (remote.tracked_flight_route_distance_km > 0) {
        const int distance_from =
            (int)lroundf(
                (float)remote
                    .tracked_flight_route_distance_km *
                progress);
        const int distance_left =
            (int)lroundf(
                (float)remote
                    .tracked_flight_route_distance_km *
                (1.0f - progress));
        snprintf(
            remote.tracked_flight_distance,
            sizeof(remote.tracked_flight_distance),
            "%d km",
            distance_left);
        snprintf(
            distance_km,
            sizeof(distance_km),
            "%d km",
            distance_left);
        snprintf(
            distance_mi,
            sizeof(distance_mi),
            "%d mi",
            (int)lroundf(
                (float)distance_left * 0.621371f));
        snprintf(
            distance_from_km,
            sizeof(distance_from_km),
            "%d km",
            distance_from);
        snprintf(
            distance_from_mi,
            sizeof(distance_from_mi),
            "%d mi",
            (int)lroundf(
                (float)distance_from * 0.621371f));
    } else {
        tracked_copy_or_dash(
            distance_km,
            sizeof(distance_km),
            remote.tracked_flight_distance);
        int fallback_distance_km = -1;
        if (sscanf(remote.tracked_flight_distance,
                   "%d",
                   &fallback_distance_km) == 1 &&
            fallback_distance_km >= 0) {
            format_display_distance_km(
                distance_mi,
                sizeof(distance_mi),
                fallback_distance_km,
                true);
        } else {
            tracked_copy_or_dash(
                distance_mi,
                sizeof(distance_mi),
                remote.tracked_flight_distance);
        }
    }
    const char *distance_preferred =
        imperial ? distance_mi : distance_km;
    const char *distance_alternate =
        imperial ? distance_km : distance_mi;
    const char *distance_from_preferred =
        imperial ? distance_from_mi : distance_from_km;
    tracked_prepare_route_geometry(&remote, frame_view);
    tracked_draw_route(
        progress,
        route_color,
        frame_view);
    app_aircraft_t route_aircraft = {
        .valid = true,
        .altitude_ft = remote.tracked_flight_altitude_m > 0
            ? (int)lroundf((float)remote.tracked_flight_altitude_m / 0.3048f)
            : 0,
        .speed_kt = remote.tracked_flight_speed_kph > 0
            ? (int)lroundf((float)remote.tracked_flight_speed_kph / 1.852f)
            : 0,
        .track_deg = remote.tracked_flight_heading_deg,
        .track_valid = remote.tracked_flight_heading_valid,
        .category = 4,
        .type_source = APP_AIRCRAFT_TYPE_SOURCE_FLIGHTAWARE,
    };
    snprintf(route_aircraft.callsign,
             sizeof(route_aircraft.callsign),
             "%.11s",
             remote.tracked_flight_callsign);
    snprintf(route_aircraft.typecode,
             sizeof(route_aircraft.typecode),
             "%.7s",
             remote.tracked_flight_aircraft_type);
    snprintf(route_aircraft.model,
             sizeof(route_aircraft.model),
             "%.15s",
             remote.tracked_flight_aircraft_type);
    snprintf(route_aircraft.airline_iata,
             sizeof(route_aircraft.airline_iata),
             "%.3s",
             remote.tracked_flight_airline_iata);

    int aircraft_x = CX;
    int aircraft_y = CY;
    int aircraft_heading = 0;
    tracked_aircraft_geometry(
        progress,
        frame_view,
        &aircraft_x,
        &aircraft_y,
        &aircraft_heading);
    app_aircraft_t map_aircraft = route_aircraft;
    map_aircraft.track_deg = aircraft_heading;
    map_aircraft.track_valid = true;
    tracked_aircraft_holder = draw_aircraft_map_icon(
        screen_root,
        &map_aircraft,
        aircraft_x,
        aircraft_y,
        frame_view
            ? lv_color_hex(0xf4f7f5)
            : lv_color_hex(radar_palette_active->aircraft));
    if (frame_view) {
        tracked_attach_aircraft_identity(
            tracked_aircraft_holder,
            &remote);
    }
    tracked_aircraft_frame_view = frame_view;
    tracked_aircraft_progress_base = progress;
    tracked_aircraft_progress_epoch_us =
        esp_timer_get_time();
    const int remaining_seconds =
        tracked_duration_seconds(
            remote.tracked_flight_remaining);
    tracked_aircraft_progress_per_second =
        remote.tracked_flight_speed_kph > 0 &&
                remaining_seconds > 0 &&
                progress < 1.0f
            ? (1.0f - progress) /
                (float)remaining_seconds
            : 0.0f;

    char altitude[20];
    char speed[20];
    format_display_altitude_m(
        altitude,
        sizeof(altitude),
        remote.tracked_flight_altitude_m,
        imperial);
    format_display_speed_kph(
        speed,
        sizeof(speed),
        remote.tracked_flight_speed_kph,
        imperial);

    if (!frame_view) {
        int origin_x = 0;
        int origin_y = 0;
        int origin_width = 0;
        lv_text_align_t origin_align =
            LV_TEXT_ALIGN_CENTER;
        tracked_endpoint_layout(
            false,
            progress,
            aircraft_x,
            aircraft_y,
            &origin_x,
            &origin_y,
            &origin_width,
            &origin_align);
        tracked_endpoint_text(
            screen_root,
            remote.tracked_flight_origin,
            remote.tracked_flight_origin_city,
            origin_x,
            origin_y,
            origin_width,
            origin_align);

        int destination_x = 0;
        int destination_y = 0;
        int destination_width = 0;
        lv_text_align_t destination_align =
            LV_TEXT_ALIGN_CENTER;
        tracked_endpoint_layout(
            true,
            progress,
            aircraft_x,
            aircraft_y,
            &destination_x,
            &destination_y,
            &destination_width,
            &destination_align);
        tracked_endpoint_text(
            screen_root,
            remote.tracked_flight_destination,
            remote.tracked_flight_destination_city,
            destination_x,
            destination_y,
            destination_width,
            destination_align);

        if (tracked_panels_visible) {
            tracked_draw_globe_panels(
                &remote,
                &route_aircraft,
                altitude,
                speed,
                distance_from_preferred,
                distance_preferred,
                altitude_color,
                speed_color);
        }
    } else {
        const int destination_label_y =
            tracked_frame_endpoint_label_y(
                true,
                progress,
                aircraft_y);
        const int origin_label_y =
            tracked_frame_endpoint_label_y(
                false,
                progress,
                aircraft_y);
        tracked_endpoint_text(
            screen_root,
            remote.tracked_flight_destination,
            remote.tracked_flight_destination_city,
            154,
            destination_label_y,
            158,
            LV_TEXT_ALIGN_CENTER);
        tracked_endpoint_text(
            screen_root,
            remote.tracked_flight_origin,
            remote.tracked_flight_origin_city,
            154,
            origin_label_y,
            158,
            LV_TEXT_ALIGN_CENTER);
        if (tracked_panels_visible) {
            tracked_draw_frame_panels(
                &remote,
                &route_aircraft,
                altitude,
                speed,
                distance_preferred,
                distance_alternate,
                eta_color,
                altitude_color,
                speed_color);
        }
    }
    tracked_panels_entry_animation_pending = false;
}

static void draw_weather_condition_icon(lv_obj_t *parent,
                                        int center_x,
                                        int center_y,
                                        int size,
                                        int weather_code)
{
    const lv_color_t sun = lv_color_hex(0xffb43c);
    const lv_color_t cloud = lv_color_hex(0xdce4df);
    const lv_color_t rain = lv_color_hex(0x62bfff);
    const bool clear = weather_code == 0 || weather_code == 1;
    const bool fog = weather_code == 45 || weather_code == 48;
    const bool snow = weather_code >= 71 && weather_code <= 86;
    const bool storm = weather_code >= 95;
    const bool wet =
        (weather_code >= 51 && weather_code <= 67) ||
        (weather_code >= 80 && weather_code <= 82);
    const int radius = size / 5;

    if (clear) {
        dot(parent, center_x, center_y, radius * 2, sun, LV_OPA_COVER);
        for (int ray = 0; ray < 8; ray++) {
            const float angle = (float)ray * 45.0f * DEG_TO_RAD;
            line(parent,
                 center_x + (int)lroundf(cosf(angle) * radius * 1.45f),
                 center_y + (int)lroundf(sinf(angle) * radius * 1.45f),
                 center_x + (int)lroundf(cosf(angle) * radius * 2.05f),
                 center_y + (int)lroundf(sinf(angle) * radius * 2.05f),
                 sun,
                 size >= 60 ? 3 : 2,
                 (lv_opa_t)230);
        }
        return;
    }

    if (fog) {
        for (int row = -1; row <= 1; row++) {
            line(parent,
                 center_x - size / 3,
                 center_y + row * size / 7,
                 center_x + size / 3,
                 center_y + row * size / 7,
                 cloud,
                 size >= 60 ? 3 : 2,
                 (lv_opa_t)(205 - abs(row) * 35));
        }
        return;
    }

    dot(parent,
        center_x - radius,
        center_y - radius / 3,
        radius * 2,
        cloud,
        (lv_opa_t)225);
    dot(parent,
        center_x + radius / 2,
        center_y - radius / 2,
        radius * 3,
        cloud,
        (lv_opa_t)235);
    lv_obj_t *base = card(
        parent,
        center_x - size / 3,
        center_y - radius / 3,
        size * 2 / 3,
        radius * 2,
        cloud,
        (lv_opa_t)235,
        cloud,
        LV_OPA_TRANSP,
        radius);
    lv_obj_set_style_border_width(base, 0, 0);

    if (wet || storm) {
        for (int drop = -1; drop <= 1; drop++) {
            line(parent,
                 center_x + drop * size / 5,
                 center_y + radius,
                 center_x + drop * size / 5 - 3,
                 center_y + radius * 2,
                 rain,
                 size >= 60 ? 3 : 2,
                 (lv_opa_t)230);
        }
    } else if (snow) {
        for (int flake = -1; flake <= 1; flake++) {
            dot(parent,
                center_x + flake * size / 5,
                center_y + radius * 2,
                size >= 60 ? 5 : 3,
                lv_color_hex(0xbfe7ff),
                LV_OPA_COVER);
        }
    }
    if (storm) {
        line(parent,
             center_x + 3,
             center_y + radius,
             center_x - 5,
             center_y + radius * 2,
             lv_color_hex(0xffbc38),
             4,
             LV_OPA_COVER);
        line(parent,
             center_x - 5,
             center_y + radius * 2,
             center_x + 5,
             center_y + radius * 2,
             lv_color_hex(0xffbc38),
             4,
             LV_OPA_COVER);
    }
}

static void draw_weather_city_icon(const char *location_key,
                                   int center_x,
                                   int center_y,
                                   int display_size)
{
    size_t high_resolution_len = 0;
    uint32_t high_resolution_revision = 0;
    const uint8_t *high_resolution =
        app_remote_get_weather_city_icon(
            location_key,
            &high_resolution_len,
            &high_resolution_revision);
    (void)high_resolution_revision;
    const lv_image_dsc_t *icon = NULL;
    if (high_resolution != NULL &&
        high_resolution_len ==
            APP_REMOTE_WEATHER_CITY_ICON_BYTES) {
        memset(
            &weather_city_icon_image,
            0,
            sizeof(weather_city_icon_image));
        weather_city_icon_image.header.magic =
            LV_IMAGE_HEADER_MAGIC;
        weather_city_icon_image.header.cf =
            LV_COLOR_FORMAT_RGB565A8;
        weather_city_icon_image.header.w =
            APP_REMOTE_WEATHER_CITY_ICON_SIZE;
        weather_city_icon_image.header.h =
            APP_REMOTE_WEATHER_CITY_ICON_SIZE;
        weather_city_icon_image.header.stride =
            APP_REMOTE_WEATHER_CITY_ICON_SIZE * 2;
        weather_city_icon_image.data_size =
            APP_REMOTE_WEATHER_CITY_ICON_BYTES;
        weather_city_icon_image.data = high_resolution;
        icon = &weather_city_icon_image;
    } else {
        icon = airspace_city_icon_for_key(location_key);
    }
    if (icon == NULL) {
        return;
    }
    const int scale =
        display_size * 256 / (int)icon->header.w;
    lv_obj_t *image = lv_image_create(screen_root);
    lv_image_set_src(image, icon);
    lv_image_set_pivot(image, icon->header.w / 2, icon->header.h / 2);
    lv_image_set_scale(image, scale);
    const int scaled_width = (int)icon->header.w * scale / 256;
    const int scaled_height = (int)icon->header.h * scale / 256;
    lv_obj_set_pos(
        image,
        center_x - scaled_width / 2,
        center_y - scaled_height / 2);
    lv_obj_set_style_image_opa(image, LV_OPA_COVER, 0);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
}

static char weather_wind_direction_text[4] = "N";

static const char *weather_cardinal_direction(int degrees)
{
    static const char *directions[] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW",
    };
    int normalized = degrees % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return directions[((normalized + 22) / 45) % 8];
}

static app_weather_units_t active_weather_units(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    return cfg.weather_units == APP_WEATHER_UNITS_FAHRENHEIT
        ? APP_WEATHER_UNITS_FAHRENHEIT
        : APP_WEATHER_UNITS_CELSIUS;
}

static int weather_display_temperature(int celsius)
{
    if (active_weather_units() == APP_WEATHER_UNITS_FAHRENHEIT) {
        return (int)lroundf((float)celsius * 1.8f + 32.0f);
    }
    return celsius;
}

/* Fahrenheit mode pairs temperature with kPa.  Keep one decimal as tenths. */
static int weather_display_pressure(int pressure_hpa)
{
    if (active_weather_units() == APP_WEATHER_UNITS_FAHRENHEIT) {
        return pressure_hpa;
    }
    return (int)lroundf((float)pressure_hpa * 0.7500617f);
}

static float weather_pressure_normalized(int pressure_hpa)
{
    /* 973..1053 hPa keeps normal sea-level pressure at the green midpoint. */
    return ((float)pressure_hpa - 973.0f) / 80.0f;
}

static void weather_temperature_animation(void *object, int32_t value)
{
    lv_label_set_text_fmt(
        (lv_obj_t *)object,
        active_weather_units() == APP_WEATHER_UNITS_FAHRENHEIT
            ? "%d°F"
            : "%d°C",
        (int)value);
}

static void weather_precipitation_animation(void *object, int32_t value)
{
    lv_label_set_text_fmt(
        (lv_obj_t *)object,
        "%d.%d mm",
        (int)(value / 10),
        abs((int)(value % 10)));
}

static void weather_humidity_animation(void *object, int32_t value)
{
    lv_label_set_text_fmt((lv_obj_t *)object, "%d%%", (int)value);
}

static void weather_wind_animation(void *object, int32_t value)
{
    lv_label_set_text_fmt(
        (lv_obj_t *)object,
        "%s %d kt",
        weather_wind_direction_text,
        (int)value);
}

static void weather_pressure_animation(void *object, int32_t value)
{
    if (active_weather_units() == APP_WEATHER_UNITS_FAHRENHEIT) {
        lv_label_set_text_fmt(
            (lv_obj_t *)object,
            "%d.%d kPa",
            (int)(value / 10),
            abs((int)(value % 10)));
    } else {
        lv_label_set_text_fmt((lv_obj_t *)object, "%d mmHg", (int)value);
    }
}

static void weather_aqi_animation(void *object, int32_t value)
{
    lv_label_set_text_fmt((lv_obj_t *)object, "%d AQI", (int)value);
}

static void weather_uv_animation(void *object, int32_t value)
{
    lv_label_set_text_fmt(
        (lv_obj_t *)object,
        "%d.%d",
        (int)(value / 10),
        abs((int)(value % 10)));
}

static void weather_marker_animation(void *object, int32_t x)
{
    lv_obj_set_x((lv_obj_t *)object, x);
}

static void start_weather_animation(lv_obj_t *object,
                                    lv_anim_exec_xcb_t callback,
                                    int32_t target)
{
    if (object == NULL || callback == NULL) {
        return;
    }
    callback(object, 0);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, callback);
    lv_anim_set_values(&animation, 0, target);
    lv_anim_set_duration(&animation, 1000);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static lv_obj_t *draw_weather_metric(int center_x,
                                     int top_y,
                                     const char *title,
                                     int target,
                                     lv_anim_exec_xcb_t animation_callback,
                                     bool valid)
{
    lv_obj_t *title_label = label_box(
        screen_root,
        title,
        &lv_font_montserrat_12,
        lv_color_hex(0x707a73),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        120);
    lv_obj_set_pos(title_label, center_x - 60, top_y);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *value_label = label_box(
        screen_root,
        valid ? "0" : "-",
        &lv_font_montserrat_16,
        lv_color_hex(0xe5ebe7),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        120);
    lv_obj_set_pos(value_label, center_x - 60, top_y + 22);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
    if (valid) {
        if (weather_entry_animation_pending) {
            start_weather_animation(
                value_label,
                animation_callback,
                target);
        } else {
            animation_callback(value_label, target);
        }
    }
    return value_label;
}

static lv_grad_dsc_t weather_divider_gradient;
static lv_grad_dsc_t weather_scale_gradient;
static lv_grad_dsc_t weather_pressure_gradient;
static bool weather_gradients_initialized;

static void ensure_weather_gradients(void)
{
    if (weather_gradients_initialized) {
        return;
    }

    const lv_color_t divider_colors[] = {
        lv_color_hex(0xffffff),
        lv_color_hex(0xffffff),
        lv_color_hex(0xffffff),
    };
    const lv_opa_t divider_opacity[] = {
        LV_OPA_TRANSP,
        (lv_opa_t)210,
        LV_OPA_TRANSP,
    };
    const uint8_t divider_stops[] = {0, 128, 255};
    lv_grad_init_stops(
        &weather_divider_gradient,
        divider_colors,
        divider_opacity,
        divider_stops,
        3);
    lv_grad_horizontal_init(&weather_divider_gradient);

    const lv_color_t scale_colors[] = {
        lv_color_hex(0x20c86a),
        lv_color_hex(0xd9cf4d),
        lv_color_hex(0xee8d35),
        lv_color_hex(0xe14d45),
    };
    const lv_opa_t scale_opacity[] = {
        (lv_opa_t)210,
        (lv_opa_t)220,
        (lv_opa_t)225,
        (lv_opa_t)215,
    };
    const uint8_t scale_stops[] = {0, 86, 171, 255};
    lv_grad_init_stops(
        &weather_scale_gradient,
        scale_colors,
        scale_opacity,
        scale_stops,
        4);
    lv_grad_horizontal_init(&weather_scale_gradient);

    const lv_color_t pressure_colors[] = {
        lv_color_hex(0x2f80ed),
        lv_color_hex(0x20c86a),
        lv_color_hex(0xe14d45),
    };
    const lv_opa_t pressure_opacity[] = {
        (lv_opa_t)220,
        (lv_opa_t)230,
        (lv_opa_t)220,
    };
    const uint8_t pressure_stops[] = {0, 128, 255};
    lv_grad_init_stops(
        &weather_pressure_gradient,
        pressure_colors,
        pressure_opacity,
        pressure_stops,
        3);
    lv_grad_horizontal_init(&weather_pressure_gradient);
    weather_gradients_initialized = true;
}

static lv_obj_t *draw_weather_gradient_bar(int x,
                                           int y,
                                           int width,
                                           int height,
                                           const lv_grad_dsc_t *gradient)
{
    lv_obj_t *bar = lv_obj_create(screen_root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, width, height);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad(bar, gradient, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    return bar;
}

static lv_obj_t *draw_weather_scale_metric(int x,
                                           int y,
                                           int width,
                                           const char *title,
                                           int target,
                                           lv_anim_exec_xcb_t animation_callback,
                                           float normalized,
                                           bool valid,
                                           lv_obj_t **marker_out,
                                           const lv_grad_dsc_t *gradient)
{
    if (normalized < 0.0f) {
        normalized = 0.0f;
    } else if (normalized > 1.0f) {
        normalized = 1.0f;
    }
    lv_obj_t *title_label = label_box(
        screen_root,
        title,
        &lv_font_montserrat_12,
        lv_color_hex(0x707a73),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        width);
    lv_obj_set_pos(title_label, x, y);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_t *value_label = label_box(
        screen_root,
        valid ? "0" : "-",
        &lv_font_montserrat_16,
        lv_color_hex(0xf0f3f1),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        width);
    lv_obj_set_pos(value_label, x, y + 18);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_LEFT, 0);
    if (valid) {
        if (weather_entry_animation_pending) {
            start_weather_animation(
                value_label,
                animation_callback,
                target);
        } else {
            animation_callback(value_label, target);
        }
    }

    const int bar_y = y + 45;
    ensure_weather_gradients();
    draw_weather_gradient_bar(
        x,
        bar_y,
        width,
        5,
        gradient != NULL ? gradient : &weather_scale_gradient);
    lv_obj_t *marker = lv_obj_create(screen_root);
    lv_obj_remove_style_all(marker);
    lv_obj_set_pos(marker, x, bar_y - 4);
    lv_obj_set_size(marker, 2, 13);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(marker, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(
        marker,
        valid ? LV_OPA_COVER : LV_OPA_TRANSP,
        0);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE);
    if (marker_out != NULL) {
        *marker_out = marker;
    }
    if (valid) {
        const int marker_x =
            x + (int)lroundf(normalized * (float)(width - 1));
        if (weather_entry_animation_pending) {
            lv_anim_t animation;
            lv_anim_init(&animation);
            lv_anim_set_var(&animation, marker);
            lv_anim_set_exec_cb(&animation, weather_marker_animation);
            lv_anim_set_values(&animation, x, marker_x);
            lv_anim_set_duration(&animation, 1000);
            lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
            lv_anim_start(&animation);
        } else {
            weather_marker_animation(marker, marker_x);
        }
    }
    return value_label;
}

static uint16_t weather_blend_rgb565(uint16_t background,
                                     uint16_t foreground,
                                     uint8_t alpha)
{
    const uint32_t inverse = 255U - alpha;
    const uint32_t bg_r = (background >> 11) & 0x1f;
    const uint32_t bg_g = (background >> 5) & 0x3f;
    const uint32_t bg_b = background & 0x1f;
    const uint32_t fg_r = (foreground >> 11) & 0x1f;
    const uint32_t fg_g = (foreground >> 5) & 0x3f;
    const uint32_t fg_b = foreground & 0x1f;
    const uint32_t r = (bg_r * inverse + fg_r * alpha + 127U) / 255U;
    const uint32_t g = (bg_g * inverse + fg_g * alpha + 127U) / 255U;
    const uint32_t b = (bg_b * inverse + fg_b * alpha + 127U) / 255U;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static const uint8_t *compose_weather_radar(uint8_t *base_map,
                                            const uint8_t *overlay,
                                            size_t overlay_len,
                                            uint32_t overlay_revision,
                                            uint32_t map_revision,
                                            int map_index,
                                            int frame_index)
{
    if (base_map == NULL ||
        overlay == NULL ||
        overlay_len != APP_WEATHER_RADAR_OVERLAY_BYTES) {
        return base_map;
    }
    if (weather_composite_overlay_revision_seen == overlay_revision &&
        weather_composite_map_revision_seen == map_revision &&
        weather_composite_map_index_seen == map_index &&
        weather_composite_frame_index_seen == frame_index) {
        return base_map;
    }

    const uint8_t *alpha_plane =
        overlay + APP_WEATHER_RADAR_OVERLAY_PIXELS * 2U;
    const int source_margin =
        (APP_WEATHER_RADAR_OVERLAY_SIZE -
         APP_WEATHER_RADAR_VIEW_SIZE) /
        2;
    const int motion_x =
        weather_data_snapshot != NULL
            ? (int)lroundf(
                weather_data_snapshot->weather_radar_motion_x *
                frame_index)
            : 0;
    const int motion_y =
        weather_data_snapshot != NULL
            ? (int)lroundf(
                weather_data_snapshot->weather_radar_motion_y *
                frame_index)
            : 0;
    const int opacity_percent =
        frame_index > 0
            ? (100 - (frame_index < 12 ? frame_index * 5 : 60))
            : 100;
    for (int y = 0; y < SCREEN_SIZE; y++) {
        uint8_t *destination =
            base_map + (size_t)y * MAP_TILE_STRIDE_BYTES;
        const int source_y = source_margin + y / 2 - motion_y;
        if (source_y < 0 ||
            source_y >= APP_WEATHER_RADAR_OVERLAY_SIZE) {
            continue;
        }
        for (int x = 0; x < SCREEN_SIZE; x++) {
            const int source_x = source_margin + x / 2 - motion_x;
            if (source_x < 0 ||
                source_x >= APP_WEATHER_RADAR_OVERLAY_SIZE) {
                continue;
            }
            const size_t pixel =
                (size_t)source_y * APP_WEATHER_RADAR_OVERLAY_SIZE +
                source_x;
            const uint16_t bg = (uint16_t)(
                destination[x * 2] |
                ((uint16_t)destination[x * 2 + 1] << 8));
            const uint16_t fg = (uint16_t)(
                overlay[pixel * 2] |
                ((uint16_t)overlay[pixel * 2 + 1] << 8));
            const uint16_t blended =
                weather_blend_rgb565(
                    bg,
                    fg,
                    (uint8_t)(
                        (uint16_t)alpha_plane[pixel] *
                        opacity_percent /
                        100U));
            destination[x * 2] = (uint8_t)(blended & 0xff);
            destination[x * 2 + 1] = (uint8_t)(blended >> 8);
        }
    }
    weather_composite_overlay_revision_seen = overlay_revision;
    weather_composite_map_revision_seen = map_revision;
    weather_composite_map_index_seen = map_index;
    weather_composite_frame_index_seen = frame_index;
    return base_map;
}

static void render_weather(void)
{
    sync_radar_display_config();
    app_network_status_t net;
    app_network_get_status(&net);
    app_data_status_t *data = ui_data_status();
    if (data == NULL) {
        draw_shell();
        label(
            screen_root,
            "-",
            &lv_font_montserrat_28,
            lv_color_hex(0x7b857e),
            LV_ALIGN_CENTER,
            0,
            0);
        return;
    }
    app_config_t cfg;
    app_config_get(&cfg);
    if (weather_selected_key[0] == '\0') {
        snprintf(
            weather_selected_key,
            sizeof(weather_selected_key),
            "%.7s",
            cfg.location_key);
    }
    const app_location_preset_t *weather_location =
        app_location_preset_find(
            weather_selected_key);
    const char *full_city_name =
        weather_location != NULL
            ? weather_location->label
            : (net.location_label[0] != '\0'
                ? net.location_label
                : cfg.location_label);
    const bool weather_values_valid =
        data->weather_valid &&
        data->weather_location_key[0] != '\0' &&
        strcasecmp(
            data->weather_location_key,
            weather_selected_key) == 0;

    size_t overlay_len = 0;
    uint32_t overlay_revision = 0;
    int overlay_frame_unix = 0;
    int overlay_frame_index = -1;
    int overlay_frame_count = data->weather_radar_frame_count;
    if (overlay_frame_count > APP_WEATHER_RADAR_FRAME_COUNT) {
        overlay_frame_count = APP_WEATHER_RADAR_FRAME_COUNT;
    }
    if (overlay_frame_count <= 0) {
        weather_radar_playing = false;
        weather_radar_playback_index = -1;
    } else {
        if (weather_radar_playback_index >= overlay_frame_count) {
            weather_radar_playback_index = overlay_frame_count - 1;
        }
        overlay_frame_index =
            weather_radar_playback_index >= 0
                ? weather_radar_playback_index
                : 0;
    }
    const bool overlay_matches =
        data->weather_radar_overlay_valid &&
        strcasecmp(
            data->weather_radar_overlay_location_key,
            weather_selected_key) == 0 &&
        fabsf(data->weather_radar_overlay_radius_miles - radar_range_miles) < 0.1f;
    const uint8_t *overlay = overlay_matches
        ? app_data_get_weather_radar_overlay_frame(
            overlay_frame_index,
            &overlay_len,
            &overlay_revision,
            &overlay_frame_unix)
        : NULL;

    if (weather_forecast_visible) {
        draw_shell();
        soft_blob(
            screen_root,
            CX,
            98,
            250,
            lv_color_hex(radar_palette_active->main),
            (lv_opa_t)18);
        label(
            screen_root,
            full_city_name,
            &lv_font_montserrat_20,
            lv_color_hex(0xf0f4f1),
            LV_ALIGN_TOP_MID,
            0,
            42);
        label(
            screen_root,
            "5-DAY FORECAST",
            &lv_font_montserrat_12,
            lv_color_hex(0x77817a),
            LV_ALIGN_TOP_MID,
            0,
            72);

        label(
            screen_root,
            "HIGH",
            &lv_font_montserrat_12,
            lv_color_hex(0x69726c),
            LV_ALIGN_TOP_LEFT,
            210,
            102);
        label(
            screen_root,
            "LOW",
            &lv_font_montserrat_12,
            lv_color_hex(0x69726c),
            LV_ALIGN_TOP_LEFT,
            278,
            102);
        label(
            screen_root,
            "RAIN",
            &lv_font_montserrat_12,
            lv_color_hex(0x69726c),
            LV_ALIGN_TOP_LEFT,
            350,
            102);

        for (int day = 0; day < APP_WEATHER_FORECAST_DAYS; day++) {
            const int y = 139 + day * 54;
            if (day > 0) {
                line(
                    screen_root,
                    46,
                    y - 14,
                    420,
                    y - 14,
                    lv_color_hex(0x343b36),
                    1,
                    (lv_opa_t)72);
            }
            const bool forecast_valid =
                weather_values_valid &&
                data->forecast_day[day][0] != '\0';
            weather_forecast_day_labels[day] = label(
                screen_root,
                forecast_valid
                    ? data->forecast_day[day]
                    : "-",
                &lv_font_montserrat_14,
                day == 0
                    ? lv_color_hex(radar_palette_active->text)
                    : lv_color_hex(0xdce1dd),
                LV_ALIGN_TOP_LEFT,
                48,
                y);
            if (forecast_valid) {
                draw_weather_condition_icon(
                    screen_root,
                    154,
                    y + 11,
                    34,
                    data->forecast_code[day]);
            }

            char high[12];
            char low[12];
            char precip[12];
            if (forecast_valid) {
                snprintf(
                    high,
                    sizeof(high),
                    "%d°",
                    weather_display_temperature(data->forecast_high[day]));
                snprintf(
                    low,
                    sizeof(low),
                    "%d°",
                    weather_display_temperature(data->forecast_low[day]));
                snprintf(
                    precip,
                    sizeof(precip),
                    "%d%%",
                    data->forecast_precip_percent[day]);
            } else {
                snprintf(high, sizeof(high), "-");
                snprintf(low, sizeof(low), "-");
                snprintf(precip, sizeof(precip), "-");
            }
            weather_forecast_high_labels[day] = label(
                screen_root,
                high,
                &lv_font_montserrat_16,
                lv_color_hex(0xf2f4f2),
                LV_ALIGN_TOP_LEFT,
                216,
                y - 1);
            weather_forecast_low_labels[day] = label(
                screen_root,
                low,
                &lv_font_montserrat_16,
                lv_color_hex(0x9ca69f),
                LV_ALIGN_TOP_LEFT,
                282,
                y - 1);
            weather_forecast_precip_labels[day] = label(
                screen_root,
                precip,
                &lv_font_montserrat_14,
                forecast_valid &&
                    data->forecast_precip_percent[day] > 35
                    ? lv_color_hex(0x68bdff)
                    : lv_color_hex(0x9ca69f),
                LV_ALIGN_TOP_LEFT,
                350,
                y);
        }
        return;
    }

    if (weather_radar_visible) {
        size_t map_len = 0;
        uint32_t map_revision = 0;
        int map_index = app_remote_radar_range_index_for_miles(radar_range_miles);
        const uint8_t *map =
            app_remote_get_radar_map_for_location(
            weather_selected_key,
            radar_range_miles,
            &map_len,
            &map_revision,
            &map_index
        );
        if (overlay != NULL) {
            const bool composite_changed =
                weather_composite_overlay_revision_seen != overlay_revision ||
                weather_composite_map_revision_seen != map_revision ||
                weather_composite_map_index_seen != map_index ||
                weather_composite_frame_index_seen != overlay_frame_index;
            if (composite_changed) {
                padded_map_revision_seen = UINT32_MAX;
            }
        } else if (
            weather_composite_overlay_revision_seen != UINT32_MAX) {
            padded_map_revision_seen = UINT32_MAX;
            weather_composite_overlay_revision_seen = UINT32_MAX;
            weather_composite_map_revision_seen = UINT32_MAX;
            weather_composite_map_index_seen = -1;
            weather_composite_frame_index_seen = -1;
        }
        map = prepare_padded_map_tiles(
            (uint8_t)(MAP_KIND_WEATHER_BASE + map_index),
            map_len,
            map,
            map_revision
        );

        draw_shell();
        const uint8_t *composite =
            compose_weather_radar(
                (uint8_t *)map,
                overlay,
                overlay_len,
                overlay_revision,
                map_revision,
                map_index,
                overlay_frame_index);
        if (composite != NULL) {
            draw_pushed_map_tiles(composite);
        }
        if (overlay != NULL) {
            weather_radar_loading = false;
        } else {
            label(
                screen_root,
                weather_radar_loading
                    ? "LOADING RADAR"
                    : "RADAR UNAVAILABLE",
                &lv_font_montserrat_14,
                weather_radar_loading
                    ? lv_color_hex(0xc7cfca)
                    : lv_color_hex(0xffb000),
                LV_ALIGN_CENTER,
                0,
                0);
        }

        lv_obj_t *temperature = lv_obj_create(screen_root);
        lv_obj_remove_style_all(temperature);
        lv_obj_set_pos(temperature, CX - 27, 19);
        lv_obj_set_size(temperature, 54, 54);
        lv_obj_set_style_radius(temperature, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(temperature, lv_color_hex(0x030504), 0);
        lv_obj_set_style_bg_opa(temperature, (lv_opa_t)168, 0);
        lv_obj_remove_flag(
            temperature,
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        char value[16];
        if (weather_values_valid) {
            snprintf(
                value,
                sizeof(value),
                "%d°",
                weather_display_temperature(data->weather_temperature));
        } else {
            snprintf(value, sizeof(value), "-");
        }
        weather_radar_temperature_label = label(
            temperature,
            value,
            &lv_font_montserrat_18,
            lv_color_hex(0xffffff),
            LV_ALIGN_CENTER,
            0,
            0);

        const int timeline_x1 = 151;
        const int timeline_x2 = 315;
        const int timeline_y = 84;
        line(
            screen_root,
            timeline_x1,
            timeline_y,
            timeline_x2,
            timeline_y,
            lv_color_hex(0x99a19c),
            2,
            (lv_opa_t)78);
        float timeline_progress =
            overlay != NULL ? 1.0f : 0.0f;
        if (overlay_frame_count > 1 && overlay_frame_index >= 0) {
            timeline_progress =
                (float)overlay_frame_index /
                (float)(overlay_frame_count - 1);
        }
        const int timeline_marker_x =
            timeline_x1 +
            (int)lroundf(
                timeline_progress *
                (float)(timeline_x2 - timeline_x1));
        weather_radar_progress_fill = lv_obj_create(screen_root);
        lv_obj_remove_style_all(weather_radar_progress_fill);
        lv_obj_set_pos(
            weather_radar_progress_fill,
            timeline_x1,
            timeline_y - 1);
        lv_obj_set_size(
            weather_radar_progress_fill,
            timeline_marker_x > timeline_x1
                ? timeline_marker_x - timeline_x1
                : 1,
            3);
        lv_obj_set_style_radius(
            weather_radar_progress_fill,
            LV_RADIUS_CIRCLE,
            0);
        lv_obj_set_style_bg_color(
            weather_radar_progress_fill,
            lv_color_hex(radar_palette_active->text),
            0);
        lv_obj_set_style_bg_opa(
            weather_radar_progress_fill,
            (lv_opa_t)210,
            0);
        weather_radar_progress_marker = dot(
            screen_root,
            timeline_marker_x,
            timeline_y,
            4,
            lv_color_hex(0xffffff),
            LV_OPA_COVER);

        char frame_time[20];
        const int minutes_after_now =
            overlay_frame_index > 0
                ? overlay_frame_index *
                    APP_WEATHER_RADAR_FORECAST_STEP_MINUTES
                : 0;
        if (overlay == NULL) {
            snprintf(frame_time, sizeof(frame_time), "-");
        } else if (minutes_after_now <= 0) {
            snprintf(frame_time, sizeof(frame_time), "NOW");
        } else {
            snprintf(
                frame_time,
                sizeof(frame_time),
                "+%d MIN",
                minutes_after_now);
        }
        weather_radar_time_label = label(
            screen_root,
            frame_time,
            &lv_font_montserrat_12,
            lv_color_hex(0xcbd2cd),
            LV_ALIGN_TOP_MID,
            0,
            91);

        if (overlay != NULL && overlay_frame_count > 1) {
            lv_obj_t *play_control = lv_obj_create(screen_root);
            lv_obj_remove_style_all(play_control);
            lv_obj_set_pos(play_control, CX - 27, CY - 27);
            lv_obj_set_size(play_control, 54, 54);
            lv_obj_set_style_radius(
                play_control,
                LV_RADIUS_CIRCLE,
                0);
            lv_obj_set_style_bg_color(
                play_control,
                lv_color_hex(0x020302),
                0);
            lv_obj_set_style_bg_opa(
                play_control,
                (lv_opa_t)116,
                0);
            lv_obj_remove_flag(
                play_control,
                LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            if (weather_radar_playing) {
                line(
                    screen_root,
                    CX - 7,
                    CY - 10,
                    CX - 7,
                    CY + 10,
                    lv_color_hex(0xffffff),
                    4,
                    LV_OPA_COVER);
                line(
                    screen_root,
                    CX + 7,
                    CY - 10,
                    CX + 7,
                    CY + 10,
                    lv_color_hex(0xffffff),
                    4,
                    LV_OPA_COVER);
            } else {
                const int play_points[][2] = {
                    {CX - 7, CY - 11},
                    {CX - 7, CY + 11},
                    {CX + 11, CY},
                    {CX - 7, CY - 11},
                };
                polyline(
                    screen_root,
                    play_points,
                    4,
                    lv_color_hex(0xffffff),
                    3,
                    LV_OPA_COVER);
            }
        }

        static const float weather_ranges[RADAR_RANGE_BUTTON_COUNT] = {
            10.0f,
            20.0f,
            50.0f,
        };
        static const int weather_range_centers[
            RADAR_RANGE_BUTTON_COUNT] = {
            150,
            233,
            316,
        };
        for (int i = 0; i < RADAR_RANGE_BUTTON_COUNT; i++) {
            const int button_size = 58;
            const int button_x =
                weather_range_centers[i] - button_size / 2;
            const int button_y = 385;
            const bool selected =
                fabsf(
                    weather_ranges[i] -
                    radar_range_miles) < 0.1f;
            weather_radar_range_buttons[i] =
                (radar_range_button_t) {
                    .valid = true,
                    .x = button_x,
                    .y = button_y,
                    .w = button_size,
                    .h = button_size,
                    .miles = weather_ranges[i],
                };
            lv_obj_t *button = lv_obj_create(screen_root);
            lv_obj_remove_style_all(button);
            lv_obj_set_pos(button, button_x, button_y);
            lv_obj_set_size(button, button_size, button_size);
            lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(
                button,
                selected
                    ? lv_color_hex(radar_palette_active->dark)
                    : lv_color_hex(0x040605),
                0);
            lv_obj_set_style_bg_opa(
                button,
                selected ? (lv_opa_t)220 : (lv_opa_t)170,
                0);
            lv_obj_set_style_border_width(
                button,
                selected ? 2 : 1,
                0);
            lv_obj_set_style_border_color(
                button,
                selected
                    ? lv_color_hex(radar_palette_active->text)
                    : lv_color_hex(0x7b857e),
                0);
            lv_obj_set_style_border_opa(
                button,
                selected ? LV_OPA_COVER : (lv_opa_t)86,
                0);
            lv_obj_remove_flag(
                button,
                LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            char range_text[8];
            snprintf(
                range_text,
                sizeof(range_text),
                "%.0f",
                (double)weather_ranges[i]);
            label(
                button,
                range_text,
                &lv_font_montserrat_16,
                selected
                    ? lv_color_hex(0xffffff)
                    : lv_color_hex(0xc2c9c4),
                LV_ALIGN_CENTER,
                0,
                0);
        }
        return;
    }

    draw_shell();

    draw_weather_city_icon(
        weather_selected_key,
        103,
        121,
        112);
    label(
        screen_root,
        full_city_name,
        &lv_font_montserrat_18,
        lv_color_hex(0xcbd2cd),
        LV_ALIGN_TOP_LEFT,
        188,
        49);

    weather_temperature_label = label_box(
        screen_root,
        weather_values_valid
            ? (active_weather_units() == APP_WEATHER_UNITS_FAHRENHEIT
                ? "0°F"
                : "0°C")
            : "-",
        &lv_font_montserrat_48,
        lv_color_hex(0xffffff),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        150);
    lv_obj_set_pos(weather_temperature_label, 184, 77);
    lv_obj_set_style_text_align(
        weather_temperature_label,
        LV_TEXT_ALIGN_LEFT,
        0);
    if (weather_values_valid) {
        if (weather_entry_animation_pending) {
            start_weather_animation(
                weather_temperature_label,
                weather_temperature_animation,
                weather_display_temperature(data->weather_temperature));
        } else {
            weather_temperature_animation(
                weather_temperature_label,
                weather_display_temperature(data->weather_temperature));
        }
        weather_summary_label = label_box(
            screen_root,
            data->weather_summary,
            &lv_font_montserrat_18,
            lv_color_hex(radar_palette_active->text),
            LV_ALIGN_TOP_LEFT,
            188,
            142,
            126);
        draw_weather_condition_icon(
            screen_root,
            360,
            138,
            88,
            data->weather_code);
    } else {
        weather_summary_label = label_box(
            screen_root,
            "-",
            &lv_font_montserrat_18,
            lv_color_hex(0x69726c),
            LV_ALIGN_TOP_LEFT,
            188,
            142,
            126);
    }

    ensure_weather_gradients();
    draw_weather_gradient_bar(
        44,
        187,
        378,
        3,
        &weather_divider_gradient);

    const int display_pressure =
        weather_display_pressure(data->weather_pressure_hpa);
    snprintf(
        weather_wind_direction_text,
        sizeof(weather_wind_direction_text),
        "%s",
        weather_cardinal_direction(data->weather_wind_deg));

    weather_precipitation_label = draw_weather_metric(
        139,
        201,
        "PRECIPITATION",
        data->weather_precipitation_tenths_mm,
        weather_precipitation_animation,
        weather_values_valid);
    weather_humidity_label = draw_weather_metric(
        327,
        201,
        "HUMIDITY",
        data->weather_humidity_percent,
        weather_humidity_animation,
        weather_values_valid);
    weather_wind_label = draw_weather_scale_metric(
        74,
        272,
        140,
        "WIND",
        data->weather_wind_kt,
        weather_wind_animation,
        (float)data->weather_wind_kt / 40.0f,
        weather_values_valid,
        &weather_scale_markers[0],
        &weather_scale_gradient);
    weather_pressure_label = draw_weather_scale_metric(
        252,
        272,
        140,
        "PRESSURE",
        display_pressure,
        weather_pressure_animation,
        weather_pressure_normalized(data->weather_pressure_hpa),
        weather_values_valid && data->weather_pressure_hpa > 0,
        &weather_scale_markers[1],
        &weather_pressure_gradient);
    weather_aqi_label = draw_weather_scale_metric(
        74,
        351,
        140,
        "AIR QUALITY",
        data->weather_aqi,
        weather_aqi_animation,
        data->weather_air_quality_valid
            ? (float)data->weather_aqi / 200.0f
            : 0.0f,
        weather_values_valid &&
            data->weather_air_quality_valid,
        &weather_scale_markers[2],
        &weather_scale_gradient);
    weather_uv_label = draw_weather_scale_metric(
        252,
        351,
        140,
        "UV INDEX",
        data->weather_uv_tenths,
        weather_uv_animation,
        data->weather_air_quality_valid
            ? (float)data->weather_uv_tenths / 110.0f
            : 0.0f,
        weather_values_valid &&
            data->weather_air_quality_valid,
        &weather_scale_markers[3],
        &weather_scale_gradient);
    if (weather_values_valid) {
        weather_entry_animation_pending = false;
    }
}

static const int space_orbit_radii[8] = {
    45, 68, 91, 114, 137, 160, 183, 206
};
static const int space_planet_sizes[8] = {
    5, 8, 9, 7, 15, 13, 10, 10
};
static const uint32_t space_planet_colors[8] = {
    0xaaa89f, 0xe1a65d, 0x3b91e8, 0xc05c38,
    0xd8a56c, 0xd6c78f, 0x70c7cf, 0x5279d2
};
static const char *space_planet_names[8] = {
    "MERCURY", "VENUS", "EARTH", "MARS",
    "JUPITER", "SATURN", "URANUS", "NEPTUNE"
};

/* Sidereal periods preserve the real ordering of angular velocities.  The
 * intro animation compresses their enormous range, so Neptune still moves
 * visibly while Mercury remains unmistakably fastest. */
static const float space_orbital_period_days[8] = {
    87.969f, 224.701f, 365.256f, 686.980f,
    4332.589f, 10759.22f, 30688.5f, 60182.0f,
};

/* A freshly powered ESP32 can report a small positive Unix time before SNTP
 * completes.  Treating that as valid placed the planets in January 1970;
 * Venus and Earth are about 167 degrees apart there, which looked like the
 * persistent opposite-side placement reported on the display. */
#define SPACE_EPHEMERIS_MIN_VALID_UNIX ((time_t)1704067200LL)

enum {
    SPACE_ASSET_SUN = 0,
    SPACE_ASSET_EARTH,
    SPACE_ASSET_MOON,
    SPACE_ASSET_ISS,
    SPACE_ASSET_WORLD,
    SPACE_ASSET_NASA,
    SPACE_ASSET_ROSCOSMOS,
    SPACE_ASSET_LAUNCH_SLS,
    SPACE_ASSET_LAUNCH_FALCON9,
    SPACE_ASSET_LAUNCH_SOYUZ,
    SPACE_ASSET_LAUNCH_STARSHIP,
    SPACE_ASSET_LAUNCH_SPACEX,
    SPACE_ASSET_LAUNCH_FLORIDA,
    SPACE_ASSET_LAUNCH_STARBASE,
    SPACE_ASSET_LAUNCH_BAIKONUR,
    SPACE_ASSET_ISS_FLAG_WEDGE,
};

static const char *const space_asset_names[] = {
    "sun",
    "earth",
    "moon",
    "iss",
    "world",
    "nasa",
    "roscosm",
    "launch_sls",
    "launch_falcon9",
    "launch_soyuz",
    "launch_starship",
    "launch_spacex",
    "launch_florida",
    "launch_starbase",
    "launch_baikonur",
    "iss_flags",
};

static void trim_space_asset_cache(
    const char *const *keep_names,
    size_t keep_count)
{
    for (size_t index = 0;
         index < sizeof(space_asset_names) /
             sizeof(space_asset_names[0]);
         index++) {
        bool keep = false;
        for (size_t keep_index = 0;
             keep_index < keep_count;
             keep_index++) {
            if (keep_names != NULL &&
                keep_names[keep_index] != NULL &&
                strcasecmp(
                    space_asset_names[index],
                    keep_names[keep_index]) == 0) {
                keep = true;
                break;
            }
        }
        if (!keep) {
            if (space_asset_images[index].data != NULL) {
                lv_image_cache_drop(
                    &space_asset_images[index]);
            }
            memset(
                &space_asset_images[index],
                0,
                sizeof(space_asset_images[index]));
            space_asset_image_revisions[index] = 0;
        }
    }
    app_remote_release_space_assets_except(
        keep_names,
        keep_count);
}

static const lv_image_dsc_t *space_asset_descriptor(
    int asset_index,
    const char *name)
{
    if (asset_index < SPACE_ASSET_SUN ||
        asset_index > SPACE_ASSET_ISS_FLAG_WEDGE) {
        return NULL;
    }
    size_t bytes = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    bool has_alpha = false;
    uint32_t revision = 0;
    const uint8_t *data =
        app_remote_get_space_asset(
            name,
            &bytes,
            &width,
            &height,
            &has_alpha,
            &revision);
    lv_image_dsc_t *descriptor =
        &space_asset_images[asset_index];
    if (data == NULL || bytes == 0) {
        if (descriptor->data != NULL) {
            lv_image_cache_drop(descriptor);
            memset(descriptor, 0, sizeof(*descriptor));
            space_asset_image_revisions[asset_index] = 0;
        }
        ESP_LOGW(
            TAG,
            "Artwork unavailable: %s free_psram=%uKB largest=%uKB",
            name != NULL ? name : "?",
            (unsigned)(
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) /
                1024U),
            (unsigned)(
                heap_caps_get_largest_free_block(
                    MALLOC_CAP_SPIRAM) /
                1024U));
        if (space_asset_retry_attempts > 0 &&
            space_asset_retry_after_us == 0) {
            space_asset_retry_after_us =
                esp_timer_get_time() + 1500000LL;
        }
        return NULL;
    }
    if (descriptor->data == data &&
        space_asset_image_revisions[asset_index] == revision &&
        descriptor->header.magic == LV_IMAGE_HEADER_MAGIC) {
        return descriptor;
    }
    if (descriptor->data != NULL) {
        lv_image_cache_drop(descriptor);
    }
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor->header.cf =
        has_alpha
            ? LV_COLOR_FORMAT_RGB565A8
            : LV_COLOR_FORMAT_RGB565;
    descriptor->header.w = width;
    descriptor->header.h = height;
    descriptor->header.stride = width * 2;
    descriptor->data_size = bytes;
    descriptor->data = data;
    space_asset_image_revisions[asset_index] = revision;
    return descriptor;
}

static lv_obj_t *draw_space_asset_on(
    lv_obj_t *parent,
    int asset_index,
    const char *name,
    int center_x,
    int center_y,
    uint32_t scale)
{
    const lv_image_dsc_t *descriptor =
        space_asset_descriptor(asset_index, name);
    if (descriptor == NULL) {
        return NULL;
    }
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, descriptor);
    lv_image_set_scale(image, scale);
    lv_image_set_antialias(image, true);
    /*
     * LVGL applies image scaling around the descriptor's center.
     * Position the unscaled image box around the requested center so
     * the transformed artwork remains aligned with its glow/marker.
     */
    const int width = (int)descriptor->header.w;
    const int height = (int)descriptor->header.h;
    lv_obj_set_pos(
        image,
        center_x - width / 2,
        center_y - height / 2);
    lv_obj_remove_flag(
        image,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return image;
}

static lv_obj_t *draw_space_asset(
    int asset_index,
    const char *name,
    int center_x,
    int center_y,
    uint32_t scale)
{
    return draw_space_asset_on(
        screen_root,
        asset_index,
        name,
        center_x,
        center_y,
        scale);
}

static lv_obj_t *space_symbol_ring(lv_obj_t *parent,
                                   int center_x,
                                   int center_y,
                                   int diameter,
                                   lv_color_t color,
                                   int width,
                                   lv_opa_t opacity)
{
    lv_obj_t *circle = lv_obj_create(parent);
    lv_obj_remove_style_all(circle);
    lv_obj_set_pos(
        circle,
        center_x - diameter / 2,
        center_y - diameter / 2);
    lv_obj_set_size(circle, diameter, diameter);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(circle, width, 0);
    lv_obj_set_style_border_color(circle, color, 0);
    lv_obj_set_style_border_opa(circle, opacity, 0);
    lv_obj_remove_flag(
        circle,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return circle;
}

static lv_obj_t *draw_space_planet_symbol(int planet_index)
{
    const int size = 30;
    const int center = size / 2;
    const lv_color_t color =
        lv_color_hex(space_planet_colors[planet_index]);
    lv_obj_t *symbol = lv_obj_create(screen_root);
    lv_obj_remove_style_all(symbol);
    lv_obj_set_size(symbol, size, size);
    lv_obj_set_style_bg_color(symbol, lv_color_hex(0x010302), 0);
    lv_obj_set_style_bg_opa(symbol, (lv_opa_t)190, 0);
    lv_obj_set_style_radius(symbol, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_color(symbol, color, 0);
    lv_obj_set_style_shadow_width(symbol, 7, 0);
    lv_obj_set_style_shadow_opa(symbol, LV_OPA_20, 0);
    lv_obj_remove_flag(
        symbol,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    switch (planet_index) {
    case 0: /* Mercury */
        space_symbol_ring(symbol, center, 11, 11, color, 2, LV_OPA_COVER);
        line(symbol, center, 17, center, 27, color, 2, LV_OPA_COVER);
        line(symbol, center - 5, 23, center + 5, 23, color, 2, LV_OPA_COVER);
        line(symbol, center - 5, 3, center, 7, color, 2, LV_OPA_COVER);
        line(symbol, center, 7, center + 5, 3, color, 2, LV_OPA_COVER);
        break;
    case 1: /* Venus */
        space_symbol_ring(symbol, center, 10, 13, color, 2, LV_OPA_COVER);
        line(symbol, center, 17, center, 27, color, 2, LV_OPA_COVER);
        line(symbol, center - 5, 23, center + 5, 23, color, 2, LV_OPA_COVER);
        break;
    case 2: /* Earth */
        space_symbol_ring(symbol, center, center, 20, color, 2, LV_OPA_COVER);
        line(symbol, center, 5, center, 23, color, 2, LV_OPA_COVER);
        line(symbol, 5, center, 23, center, color, 2, LV_OPA_COVER);
        break;
    case 3: /* Mars */
        space_symbol_ring(symbol, 11, 17, 13, color, 2, LV_OPA_COVER);
        line(symbol, 16, 12, 25, 3, color, 2, LV_OPA_COVER);
        line(symbol, 19, 3, 25, 3, color, 2, LV_OPA_COVER);
        line(symbol, 25, 3, 25, 9, color, 2, LV_OPA_COVER);
        break;
    case 4: { /* Jupiter */
        const int points[][2] = {
            {6, 5}, {14, 5}, {10, 10}, {6, 14},
            {8, 17}, {20, 17}
        };
        polyline(symbol, points, 6, color, 2, LV_OPA_COVER);
        line(symbol, 18, 4, 18, 27, color, 2, LV_OPA_COVER);
        line(symbol, 13, 22, 23, 22, color, 2, LV_OPA_COVER);
        break;
    }
    case 5: { /* Saturn */
        line(symbol, 11, 3, 11, 27, color, 2, LV_OPA_COVER);
        line(symbol, 5, 8, 17, 8, color, 2, LV_OPA_COVER);
        const int points[][2] = {
            {11, 12}, {18, 12}, {21, 16},
            {20, 21}, {15, 26}
        };
        polyline(symbol, points, 5, color, 2, LV_OPA_COVER);
        break;
    }
    case 6: /* Uranus */
        space_symbol_ring(symbol, center, 18, 8, color, 2, LV_OPA_COVER);
        line(symbol, center, 3, center, 14, color, 2, LV_OPA_COVER);
        line(symbol, 5, 4, 5, 18, color, 2, LV_OPA_COVER);
        line(symbol, 25, 4, 25, 18, color, 2, LV_OPA_COVER);
        line(symbol, 5, 10, 25, 10, color, 2, LV_OPA_COVER);
        break;
    case 7: /* Neptune */
        line(symbol, center, 5, center, 27, color, 2, LV_OPA_COVER);
        line(symbol, 9, 25, 21, 25, color, 2, LV_OPA_COVER);
        line(symbol, 5, 5, 5, 12, color, 2, LV_OPA_COVER);
        line(symbol, 25, 5, 25, 12, color, 2, LV_OPA_COVER);
        line(symbol, 5, 5, 10, 10, color, 2, LV_OPA_COVER);
        line(symbol, 25, 5, 20, 10, color, 2, LV_OPA_COVER);
        line(symbol, 10, 10, center, 5, color, 2, LV_OPA_COVER);
        line(symbol, 20, 10, center, 5, color, 2, LV_OPA_COVER);
        break;
    default:
        break;
    }
    return symbol;
}

typedef struct {
    double base[6];
    double rate[6];
} space_orbital_elements_t;

/*
 * JPL approximate heliocentric orbital elements, valid 1800–2050.
 * Columns are a, e, I, mean longitude, longitude of perihelion,
 * and longitude of ascending node; rates are per Julian century.
 */
static const space_orbital_elements_t space_orbits[8] = {
    {{0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593},
     {0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081}},
    {{0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255},
     {0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418}},
    {{1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0},
     {0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0}},
    {{1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891},
     {0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343}},
    {{5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909},
     {-0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106}},
    {{9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448},
     {-0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794}},
    {{19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503},
     {-0.00196176, -0.00004397, -0.00242939, 428.48202785, 0.40805281, 0.04240589}},
    {{30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574},
     {0.00026291, 0.00005105, 0.00035372, 218.45945325, -0.32241464, -0.00508664}},
};

static double normalize_radians(double value)
{
    value = fmod(value, 2.0 * M_PI);
    return value < 0.0 ? value + 2.0 * M_PI : value;
}

static float space_planet_angle_for_time(int planet_index, time_t unix_time)
{
    const double julian_date =
        2440587.5 + (double)unix_time / 86400.0;
    const double centuries =
        (julian_date - 2451545.0) / 36525.0;
    double value[6];
    for (int element = 0; element < 6; element++) {
        value[element] =
            space_orbits[planet_index].base[element] +
            space_orbits[planet_index].rate[element] * centuries;
    }
    const double eccentricity = value[1];
    const double inclination = value[2] * DEG_TO_RAD;
    const double mean_anomaly =
        normalize_radians((value[3] - value[4]) * DEG_TO_RAD);
    double eccentric_anomaly = mean_anomaly;
    for (int iteration = 0; iteration < 8; iteration++) {
        eccentric_anomaly -=
            (eccentric_anomaly -
             eccentricity * sin(eccentric_anomaly) -
             mean_anomaly) /
            (1.0 - eccentricity * cos(eccentric_anomaly));
    }

    const double orbital_x =
        value[0] * (cos(eccentric_anomaly) - eccentricity);
    const double orbital_y =
        value[0] * sqrt(1.0 - eccentricity * eccentricity) *
        sin(eccentric_anomaly);
    const double argument_perihelion =
        (value[4] - value[5]) * DEG_TO_RAD;
    const double ascending_node = value[5] * DEG_TO_RAD;
    const double x =
        (cos(argument_perihelion) * cos(ascending_node) -
         sin(argument_perihelion) * sin(ascending_node) * cos(inclination)) *
            orbital_x +
        (-sin(argument_perihelion) * cos(ascending_node) -
         cos(argument_perihelion) * sin(ascending_node) * cos(inclination)) *
            orbital_y;
    const double y =
        (cos(argument_perihelion) * sin(ascending_node) +
         sin(argument_perihelion) * cos(ascending_node) * cos(inclination)) *
            orbital_x +
        (-sin(argument_perihelion) * sin(ascending_node) +
         cos(argument_perihelion) * cos(ascending_node) * cos(inclination)) *
            orbital_y;
    return (float)normalize_radians(atan2(y, x));
}

static bool space_ephemeris_time_is_valid(time_t unix_time)
{
    /* ESP-IDF's RTC+high-resolution system-time mode keeps this clock moving
     * through light sleep.  After a true power loss the S3 has no populated
     * external calendar RTC, so wait for SNTP instead of inventing a date. */
    return unix_time >= SPACE_EPHEMERIS_MIN_VALID_UNIX;
}

static void update_space_planet_targets(void)
{
    const time_t clock_time = time(NULL);
    const bool clock_valid =
        space_ephemeris_time_is_valid(clock_time);
    if (!clock_valid) {
        space_orbit_hour_seen = -1;
        space_planet_targets_valid = false;
        ESP_LOGW(TAG, "Solar ephemeris waiting for RTC or SNTP time");
        return;
    }

    space_orbit_hour_seen = (int64_t)clock_time / 3600;
    for (int index = 0; index < 8; index++) {
        space_planet_target_angles[index] =
            space_planet_angle_for_time(index, clock_time);
    }
    space_planet_targets_valid = true;

    float venus_earth_separation = fabsf(
        space_planet_target_angles[2] -
        space_planet_target_angles[1]);
    if (venus_earth_separation > (float)M_PI) {
        venus_earth_separation =
            2.0f * (float)M_PI - venus_earth_separation;
    }
    ESP_LOGI(
        TAG,
        "Solar ephemeris clock unix=%lld Venus=%.2f Earth=%.2f separation=%.2f deg",
        (long long)clock_time,
        (double)(space_planet_target_angles[1] / DEG_TO_RAD),
        (double)(space_planet_target_angles[2] / DEG_TO_RAD),
        (double)(venus_earth_separation / DEG_TO_RAD));
}

typedef struct {
    int x;
    int y;
    int width;
    int height;
} space_annotation_rect_t;

static int space_annotation_overlap_area(
    const space_annotation_rect_t *first,
    const space_annotation_rect_t *second,
    int clearance)
{
    const int left = first->x > second->x - clearance
        ? first->x
        : second->x - clearance;
    const int top = first->y > second->y - clearance
        ? first->y
        : second->y - clearance;
    const int right = first->x + first->width <
            second->x + second->width + clearance
        ? first->x + first->width
        : second->x + second->width + clearance;
    const int bottom = first->y + first->height <
            second->y + second->height + clearance
        ? first->y + first->height
        : second->y + second->height + clearance;
    if (right <= left || bottom <= top) {
        return 0;
    }
    return (right - left) * (bottom - top);
}

static space_annotation_rect_t space_planet_annotation_rect(
    int planet_x,
    int planet_y,
    int width,
    int height,
    int candidate)
{
    const int gap = 10;
    space_annotation_rect_t rect = {
        .x = planet_x + gap,
        .y = planet_y - height / 2,
        .width = width,
        .height = height,
    };
    switch (candidate) {
    case 1: /* left */
        rect.x = planet_x - width - gap;
        break;
    case 2: /* above */
        rect.x = planet_x - width / 2;
        rect.y = planet_y - height - gap;
        break;
    case 3: /* below */
        rect.x = planet_x - width / 2;
        rect.y = planet_y + gap;
        break;
    case 4: /* upper right */
        rect.x = planet_x + gap;
        rect.y = planet_y - height - gap;
        break;
    case 5: /* lower right */
        rect.x = planet_x + gap;
        rect.y = planet_y + gap;
        break;
    case 6: /* upper left */
        rect.x = planet_x - width - gap;
        rect.y = planet_y - height - gap;
        break;
    case 7: /* lower left */
        rect.x = planet_x - width - gap;
        rect.y = planet_y + gap;
        break;
    default: /* right */
        break;
    }

    const int margin = 6;
    if (rect.x < margin) {
        rect.x = margin;
    } else if (rect.x + width > SCREEN_SIZE - margin) {
        rect.x = SCREEN_SIZE - margin - width;
    }
    if (rect.y < margin) {
        rect.y = margin;
    } else if (rect.y + height > SCREEN_SIZE - margin) {
        rect.y = SCREEN_SIZE - margin - height;
    }

    /* The AMOLED is circular.  A square bounds check still permits the
     * outer corner of a label to disappear behind the bezel, particularly
     * around 1, 5, 7 and 11 o'clock.  Constrain the complete annotation
     * rectangle, rather than its anchor, to the usable circle. */
    const float half_width = (float)width * 0.5f;
    const float half_height = (float)height * 0.5f;
    const float center_x = (float)rect.x + half_width;
    const float center_y = (float)rect.y + half_height;
    const float delta_x = center_x - (float)CX;
    const float delta_y = center_y - (float)CY;
    const float absolute_x = fabsf(delta_x);
    const float absolute_y = fabsf(delta_y);
    const float safe_radius = 221.0f;
    const float far_x = absolute_x + half_width;
    const float far_y = absolute_y + half_height;
    if (far_x * far_x + far_y * far_y >
        safe_radius * safe_radius) {
        const float a =
            delta_x * delta_x + delta_y * delta_y;
        const float b =
            2.0f *
            (absolute_x * half_width +
             absolute_y * half_height);
        const float c =
            half_width * half_width +
            half_height * half_height -
            safe_radius * safe_radius;
        float scale = 0.0f;
        if (a > 0.001f) {
            const float discriminant =
                fmaxf(0.0f, b * b - 4.0f * a * c);
            scale =
                (-b + sqrtf(discriminant)) /
                (2.0f * a);
            scale = fminf(1.0f, fmaxf(0.0f, scale));
        }
        rect.x = (int)lroundf(
            (float)CX + delta_x * scale - half_width);
        rect.y = (int)lroundf(
            (float)CY + delta_y * scale - half_height);
    }
    return rect;
}

static int space_planet_label_width(int planet_index)
{
    lv_point_t text_size = {0};
    lv_text_get_size(
        &text_size,
        space_planet_names[planet_index],
        &lv_font_montserrat_12,
        1,
        0,
        LV_COORD_MAX,
        LV_TEXT_FLAG_NONE);
    /* One pixel of breathing room on each side prevents the final glyph
     * from being clipped by integer rounding. */
    return text_size.x + 2;
}

static void layout_space_planet_annotations(
    const int planet_x[8],
    const int planet_y[8])
{
    if (space_planet_annotation_mode == 0) {
        return;
    }

    space_annotation_rect_t placed[8];
    int placed_count = 0;
    const bool symbols_only =
        space_planet_annotation_mode == 2;
    const int annotation_height =
        symbols_only ? 30 : 24;
    const space_annotation_rect_t sun_keepout = {
        .x = CX - 35,
        .y = CY - 35,
        .width = 70,
        .height = 70,
    };

    /* Inner planets have the least available room, so reserve their labels
     * first. Outer planets can then use the edge-facing alternatives. */
    for (int index = 0; index < 8; index++) {
        const int label_width =
            space_planet_label_width(index);
        const int annotation_width =
            symbols_only ? 30 : label_width;
        space_annotation_rect_t best = {0};
        long best_score = 0x7fffffffL;

        for (int candidate = 0; candidate < 8; candidate++) {
            const space_annotation_rect_t trial =
                space_planet_annotation_rect(
                    planet_x[index],
                    planet_y[index],
                    annotation_width,
                    annotation_height,
                    candidate);
            long score = candidate * 20L;

            for (int prior = 0; prior < placed_count; prior++) {
                score +=
                    (long)space_annotation_overlap_area(
                        &trial,
                        &placed[prior],
                        5) *
                    10000L;
            }

            score +=
                (long)space_annotation_overlap_area(
                    &trial,
                    &sun_keepout,
                    4) *
                3000L;

            for (int planet = 0; planet < 8; planet++) {
                const int marker_clearance =
                    space_planet_sizes[planet] / 2 + 5;
                const space_annotation_rect_t marker = {
                    .x = planet_x[planet] - marker_clearance,
                    .y = planet_y[planet] - marker_clearance,
                    .width = marker_clearance * 2,
                    .height = marker_clearance * 2,
                };
                score +=
                    (long)space_annotation_overlap_area(
                        &trial,
                        &marker,
                        2) *
                    1500L;
            }

            /* Prefer the side facing away from the Sun when several
             * collision-free choices remain. */
            const float outward_x =
                (float)(planet_x[index] - CX);
            const float outward_y =
                (float)(planet_y[index] - CY);
            const float annotation_x =
                (float)(trial.x + trial.width / 2 -
                        planet_x[index]);
            const float annotation_y =
                (float)(trial.y + trial.height / 2 -
                        planet_y[index]);
            if (outward_x * annotation_x +
                    outward_y * annotation_y <
                0.0f) {
                score += 180L;
            }

            if (score < best_score) {
                best_score = score;
                best = trial;
            }
        }

        placed[placed_count++] = best;
        if (symbols_only) {
            if (space_planet_symbols[index] == NULL) {
                space_planet_symbols[index] =
                    draw_space_planet_symbol(index);
            }
            lv_obj_set_pos(
                space_planet_symbols[index],
                best.x,
                best.y);
            continue;
        }
        if (space_planet_labels[index] == NULL) {
            space_planet_labels[index] = label_box(
                screen_root,
                space_planet_names[index],
                &lv_font_montserrat_12,
                lv_color_hex(space_planet_colors[index]),
                LV_ALIGN_TOP_LEFT,
                0,
                0,
                label_width);
        }
        lv_obj_set_pos(
            space_planet_labels[index],
            best.x,
            best.y + 4);
    }
}

static void update_solar_planets(void)
{
    if (!space_planet_targets_valid) {
        if (space_earth_glow != NULL) {
            lv_obj_add_flag(space_earth_glow, LV_OBJ_FLAG_HIDDEN);
        }
        for (int index = 0; index < 8; index++) {
            if (space_planets[index] != NULL) {
                lv_obj_add_flag(
                    space_planets[index],
                    LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }

    if (space_earth_glow != NULL) {
        lv_obj_remove_flag(space_earth_glow, LV_OBJ_FLAG_HIDDEN);
    }
    const int64_t now_us = esp_timer_get_time();
    if (space_solar_intro_start_us == 0) {
        space_solar_intro_start_us = now_us;
    }
    float progress = space_solar_intro_active
        ? (float)(now_us - space_solar_intro_start_us) / 3400000.0f
        : 1.0f;
    if (progress >= 1.0f) {
        progress = 1.0f;
        space_solar_intro_active = false;
    } else if (progress < 0.0f) {
        progress = 0.0f;
    }
    const float eased =
        progress * progress * progress *
        (progress * (progress * 6.0f - 15.0f) + 10.0f);
    int planet_x[8] = {0};
    int planet_y[8] = {0};
    for (int index = 0; index < 8; index++) {
        if (space_planets[index] == NULL) {
            continue;
        }
        lv_obj_remove_flag(space_planets[index], LV_OBJ_FLAG_HIDDEN);
        const float real_speed_ratio =
            space_orbital_period_days[2] /
            space_orbital_period_days[index];
        const float intro_turns =
            1.25f * powf(real_speed_ratio, 0.36f);
        const float start_angle =
            space_planet_target_angles[index] -
            intro_turns * 2.0f * (float)M_PI;
        const float angle =
            start_angle +
            (space_planet_target_angles[index] - start_angle) * eased;
        const int x =
            CX +
            (int)lroundf(
                cosf(angle) *
                space_orbit_radii[index]);
        const int y =
            CY +
            (int)lroundf(
                -sinf(angle) *
                space_orbit_radii[index]);
        planet_x[index] = x;
        planet_y[index] = y;
        lv_obj_set_pos(
            space_planets[index],
            x - space_planet_sizes[index] / 2,
            y - space_planet_sizes[index] / 2);
        if (index == 2 &&
            space_earth_glow != NULL) {
            lv_obj_set_pos(
                space_earth_glow,
                x - 11,
                y - 11);
        }
    }
    layout_space_planet_annotations(planet_x, planet_y);
}

static void update_solar_earth_glow(void)
{
    if (space_earth_glow == NULL) {
        return;
    }
    const float seconds =
        (float)(esp_timer_get_time() % 2800000) /
        1000000.0f;
    const float pulse =
        0.5f +
        0.5f *
            sinf(
                seconds *
                (2.0f * (float)M_PI / 2.8f));
    lv_obj_set_style_bg_opa(
        space_earth_glow,
        (lv_opa_t)(
            18 +
            lroundf(pulse * 18.0f)),
        0);
    lv_obj_set_style_shadow_opa(
        space_earth_glow,
        (lv_opa_t)(
            32 +
            lroundf(pulse * 34.0f)),
        0);
}

static void draw_space_stars(void)
{
    static const int stars[][3] = {
        {49, 92, 2}, {91, 55, 3}, {139, 103, 2}, {189, 44, 2},
        {271, 55, 2}, {328, 91, 3}, {403, 70, 2}, {426, 151, 2},
        {33, 186, 2}, {55, 286, 3}, {109, 358, 2}, {164, 393, 2},
        {296, 394, 3}, {360, 353, 2}, {421, 296, 2}, {439, 222, 3}
    };
    for (size_t index = 0; index < sizeof(stars) / sizeof(stars[0]); index++) {
        dot(
            screen_root,
            stars[index][0],
            stars[index][1],
            stars[index][2],
            lv_color_hex(0xd8e6ff),
            index % 3 == 0 ? LV_OPA_80 : LV_OPA_50);
    }
}

static void render_solar_system_base(
    lv_obj_t *screen)
{
    draw_shell_on(screen);
    draw_space_stars();

    for (int index = 7; index >= 0; index--) {
        ring(
            screen_root,
            space_orbit_radii[index] * 2,
            lv_color_hex(
                index < 4 ? 0x2b3532 : 0x202825),
            1,
            index < 4
                ? (lv_opa_t)88
                : (lv_opa_t)62);
    }

    dot(screen_root, CX, CY, 58, lv_color_hex(0xff8d22), LV_OPA_20);
    dot(screen_root, CX, CY, 38, lv_color_hex(0xffc04c), LV_OPA_50);
    space_symbol_ring(
        screen_root,
        CX,
        CY,
        26,
        lv_color_hex(0xffe19b),
        2,
        LV_OPA_COVER);
    dot(
        screen_root,
        CX,
        CY,
        6,
        lv_color_hex(0xfff5c7),
        LV_OPA_COVER);
}

static bool prepare_solar_system_snapshot(void)
{
    if (space_solar_snapshot_buffer != NULL &&
        space_solar_snapshot_image.data ==
            space_solar_snapshot_buffer) {
        return true;
    }
    if (space_solar_snapshot_buffer == NULL) {
        space_solar_snapshot_buffer =
            heap_caps_aligned_alloc(
                128,
                SPACE_SOLAR_SNAPSHOT_BYTES,
                MALLOC_CAP_SPIRAM |
                    MALLOC_CAP_8BIT);
        if (space_solar_snapshot_buffer == NULL) {
            ESP_LOGW(
                TAG,
                "No PSRAM for solar snapshot");
            return false;
        }
    }

    lv_obj_t *stage = lv_obj_create(NULL);
    if (stage == NULL) {
        return false;
    }
    lv_obj_set_size(
        stage,
        SCREEN_SIZE,
        SCREEN_SIZE);

    const int64_t started_us =
        esp_timer_get_time();
    render_solar_system_base(stage);
    memset(
        &space_solar_snapshot_image,
        0,
        sizeof(space_solar_snapshot_image));
    const lv_result_t result =
        lv_snapshot_take_to_buf(
            stage,
            LV_COLOR_FORMAT_RGB565,
            &space_solar_snapshot_image,
            space_solar_snapshot_buffer,
            SPACE_SOLAR_SNAPSHOT_BYTES);
    lv_obj_delete(stage);
    if (result != LV_RESULT_OK) {
        space_solar_snapshot_image.data = NULL;
        ESP_LOGW(
            TAG,
            "Solar snapshot failed");
        return false;
    }
    ESP_LOGI(
        TAG,
        "Solar snapshot ready in %lld ms",
        (long long)(
            (esp_timer_get_time() -
             started_us) /
            1000));
    return true;
}

static void show_solar_system_base(void)
{
    draw_shell();
    trim_space_asset_cache(NULL, 0);
    lv_obj_t *image =
        lv_image_create(screen_root);
    lv_image_set_src(
        image,
        &space_solar_snapshot_image);
    lv_obj_set_pos(image, 0, 0);
    lv_obj_remove_flag(
        image,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
}

static void render_solar_system(void)
{
    if (prepare_solar_system_snapshot()) {
        show_solar_system_base();
    } else {
        render_solar_system_base(
            lv_screen_active());
        trim_space_asset_cache(NULL, 0);
    }
    update_space_planet_targets();

    space_earth_glow =
        dot(
            screen_root,
            CX,
            CY,
            22,
            lv_color_hex(0x3b91e8),
            (lv_opa_t)24);
    lv_obj_set_style_shadow_color(
        space_earth_glow,
        lv_color_hex(0x43b8ff),
        0);
    lv_obj_set_style_shadow_width(
        space_earth_glow,
        13,
        0);
    lv_obj_set_style_shadow_opa(
        space_earth_glow,
        (lv_opa_t)48,
        0);

    for (int index = 0; index < 8; index++) {
        space_planets[index] =
            dot(
                screen_root,
                CX,
                CY,
                space_planet_sizes[index],
                lv_color_hex(space_planet_colors[index]),
                LV_OPA_COVER);
    }
    update_solar_planets();
}

#define ISS_MAP_CENTER_X 233.0f
#define ISS_MAP_CENTER_Y 205.0f
#define ISS_MAP_SCALE_X 78.0f
#define ISS_MAP_SCALE_Y 87.0f
#define ISS_MAP_LEFT 8
#define ISS_MAP_TOP 58
#define ISS_MAP_RIGHT (SCREEN_SIZE - 9)
#define ISS_MAP_BOTTOM 352
#define ISS_MARKER_SIZE 64
#define ISS_SOLAR_OVERLAY_PIXELS \
    ((size_t)SCREEN_SIZE * (size_t)SCREEN_SIZE)

static float natural_earth_x_factor(float latitude_rad)
{
    const float latitude_squared =
        latitude_rad * latitude_rad;
    const float latitude_fourth =
        latitude_squared * latitude_squared;
    return
        0.8707f -
        0.131979f * latitude_squared +
        latitude_fourth *
            (-0.013791f +
             latitude_fourth *
                 (0.003971f * latitude_squared -
                  0.001529f * latitude_fourth));
}

static float natural_earth_y(float latitude_rad)
{
    const float latitude_squared =
        latitude_rad * latitude_rad;
    const float latitude_fourth =
        latitude_squared * latitude_squared;
    return
        latitude_rad *
        (1.007226f +
         latitude_squared *
             (0.015085f +
              latitude_fourth *
                  (-0.044475f +
                   0.028874f * latitude_squared -
                   0.005916f * latitude_fourth)));
}

static bool natural_earth_latitude_for_y(
    int y,
    float *latitude_rad)
{
    const float target =
        (ISS_MAP_CENTER_Y - (float)y) /
        ISS_MAP_SCALE_Y;
    const float pole_y =
        natural_earth_y((float)M_PI * 0.5f);
    if (target < -pole_y || target > pole_y) {
        return false;
    }
    float lower = -(float)M_PI * 0.5f;
    float upper = (float)M_PI * 0.5f;
    for (int iteration = 0;
         iteration < 16;
         iteration++) {
        const float middle = (lower + upper) * 0.5f;
        if (natural_earth_y(middle) < target) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    *latitude_rad = (lower + upper) * 0.5f;
    return true;
}

static void space_world_point_float(float latitude,
                                    float longitude,
                                    float *x,
                                    float *y)
{
    while (longitude > 180.0f) {
        longitude -= 360.0f;
    }
    while (longitude < -180.0f) {
        longitude += 360.0f;
    }
    if (latitude > 90.0f) {
        latitude = 90.0f;
    } else if (latitude < -90.0f) {
        latitude = -90.0f;
    }

    const float latitude_rad = latitude * DEG_TO_RAD;
    *x =
        ISS_MAP_CENTER_X +
        ISS_MAP_SCALE_X *
            longitude *
            DEG_TO_RAD *
            natural_earth_x_factor(latitude_rad);
    *y =
        ISS_MAP_CENTER_Y -
        ISS_MAP_SCALE_Y *
            natural_earth_y(latitude_rad);
}

static void space_world_point(float latitude,
                              float longitude,
                              int *x,
                              int *y)
{
    float precise_x = 0.0f;
    float precise_y = 0.0f;
    space_world_point_float(
        latitude,
        longitude,
        &precise_x,
        &precise_y);
    *x = (int)lroundf(precise_x);
    *y = (int)lroundf(precise_y);
}

static void calculate_solar_position(
    time_t when,
    float *declination_rad,
    float *subsolar_longitude)
{
    if (when <= 0) {
        when = 1785300000;
    }
    struct tm utc;
    gmtime_r(&when, &utc);
    const float utc_hours =
        (float)utc.tm_hour +
        (float)utc.tm_min / 60.0f +
        (float)utc.tm_sec / 3600.0f;
    /*
     * NOAA's fractional-year solar model includes both axial tilt and
     * the equation of time.  That keeps the terminator seasonal and
     * places its two branches symmetrically around the true subsolar
     * meridian instead of an approximate clock-time meridian.
     */
    const float fractional_year =
        2.0f *
        (float)M_PI /
        365.0f *
        ((float)utc.tm_yday +
         (utc_hours - 12.0f) / 24.0f);
    *declination_rad =
        0.006918f -
        0.399912f * cosf(fractional_year) +
        0.070257f * sinf(fractional_year) -
        0.006758f * cosf(2.0f * fractional_year) +
        0.000907f * sinf(2.0f * fractional_year) -
        0.002697f * cosf(3.0f * fractional_year) +
        0.001480f * sinf(3.0f * fractional_year);
    const float equation_of_time_minutes =
        229.18f *
        (0.000075f +
         0.001868f * cosf(fractional_year) -
         0.032077f * sinf(fractional_year) -
         0.014615f * cosf(2.0f * fractional_year) -
         0.040849f * sinf(2.0f * fractional_year));
    *subsolar_longitude =
        180.0f -
        (utc_hours * 60.0f +
         equation_of_time_minutes) /
            4.0f;
    while (*subsolar_longitude > 180.0f) {
        *subsolar_longitude -= 360.0f;
    }
    while (*subsolar_longitude < -180.0f) {
        *subsolar_longitude += 360.0f;
    }
}

static bool ensure_space_solar_overlay_buffers(void)
{
    if (space_iss_night_buffer == NULL) {
        space_iss_night_buffer =
            (uint8_t *)heap_caps_malloc(
                ISS_SOLAR_OVERLAY_PIXELS,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (space_iss_terminator_buffer == NULL) {
        space_iss_terminator_buffer =
            (uint8_t *)heap_caps_malloc(
                ISS_SOLAR_OVERLAY_PIXELS,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (space_iss_night_buffer == NULL ||
        space_iss_terminator_buffer == NULL) {
        return false;
    }
    return true;
}

static bool ensure_eclipse_motion_buffer(void)
{
    if (space_eclipse_motion_buffer == NULL) {
        space_eclipse_motion_buffer =
            (uint8_t *)heap_caps_malloc(
                ISS_SOLAR_OVERLAY_PIXELS,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return space_eclipse_motion_buffer != NULL;
}

static void render_space_solar_overlay_mask(
    uint8_t *buffer,
    time_t when,
    bool terminator_only)
{
    if (buffer == NULL) {
        return;
    }

    memset(
        buffer,
        0,
        ISS_SOLAR_OVERLAY_PIXELS);

    float declination_rad = 0.0f;
    float subsolar_longitude = 0.0f;
    calculate_solar_position(
        when,
        &declination_rad,
        &subsolar_longitude);

    const float subsolar_longitude_rad =
        subsolar_longitude * DEG_TO_RAD;
    const float declination_sine =
        sinf(declination_rad);
    const float declination_cosine =
        cosf(declination_rad);
    for (int y = 0; y < SCREEN_SIZE; y++) {
        if (y < ISS_MAP_TOP || y > ISS_MAP_BOTTOM) {
            continue;
        }
        float latitude_rad;
        if (!natural_earth_latitude_for_y(
                y,
                &latitude_rad)) {
            continue;
        }
        const float x_factor =
            natural_earth_x_factor(latitude_rad);
        const float half_width =
            (float)M_PI *
            x_factor *
            ISS_MAP_SCALE_X;
        int start_x =
            (int)ceilf(ISS_MAP_CENTER_X - half_width);
        int end_x =
            (int)floorf(ISS_MAP_CENTER_X + half_width);
        if (start_x < 0) {
            start_x = 0;
        }
        if (end_x >= SCREEN_SIZE) {
            end_x = SCREEN_SIZE - 1;
        }
        if (start_x < ISS_MAP_LEFT) {
            start_x = ISS_MAP_LEFT;
        }
        if (end_x > ISS_MAP_RIGHT) {
            end_x = ISS_MAP_RIGHT;
        }
        const float longitude_step =
            1.0f /
            (ISS_MAP_SCALE_X * x_factor);
        float hour_angle =
            ((float)start_x - ISS_MAP_CENTER_X) /
                (ISS_MAP_SCALE_X * x_factor) -
            subsolar_longitude_rad;
        float hour_cosine = cosf(hour_angle);
        float hour_sine = sinf(hour_angle);
        const float step_cosine =
            cosf(longitude_step);
        const float step_sine =
            sinf(longitude_step);
        const float latitude_term =
            sinf(latitude_rad) *
            declination_sine;
        const float hour_term =
            cosf(latitude_rad) *
            declination_cosine;

        for (int x = start_x; x <= end_x; x++) {
            const float solar_cosine =
                latitude_term +
                hour_term * hour_cosine;
            float night =
                (0.025f - solar_cosine) / 0.05f;
            if (night < 0.0f) {
                night = 0.0f;
            } else if (night > 1.0f) {
                night = 1.0f;
            }
            night =
                night * night *
                (3.0f - 2.0f * night);

            float terminator =
                1.0f -
                fabsf(solar_cosine) / 0.020f;
            if (terminator < 0.0f) {
                terminator = 0.0f;
            } else if (terminator > 1.0f) {
                terminator = 1.0f;
            }
            terminator =
                terminator * terminator *
                (3.0f - 2.0f * terminator);

            const size_t pixel =
                (size_t)y *
                    (size_t)SCREEN_SIZE +
                (size_t)x;
            buffer[pixel] = terminator_only
                ? (uint8_t)lroundf(118.0f * terminator)
                : (uint8_t)lroundf(148.0f * night);

            const float next_hour_cosine =
                hour_cosine * step_cosine -
                hour_sine * step_sine;
            hour_sine =
                hour_sine * step_cosine +
                hour_cosine * step_sine;
            hour_cosine = next_hour_cosine;
        }
    }
}

static void prepare_space_solar_overlay_descriptor(
    lv_image_dsc_t *descriptor,
    uint8_t *buffer)
{
    if (descriptor->data != NULL) {
        lv_image_cache_drop(descriptor);
    }
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor->header.cf = LV_COLOR_FORMAT_A8;
    descriptor->header.w = SCREEN_SIZE;
    descriptor->header.h = SCREEN_SIZE;
    descriptor->header.stride = SCREEN_SIZE;
    descriptor->data_size = ISS_SOLAR_OVERLAY_PIXELS;
    descriptor->data = buffer;
}

static lv_obj_t *create_space_solar_overlay(
    lv_obj_t *map,
    lv_image_dsc_t *descriptor,
    uint32_t color,
    lv_opa_t opacity)
{
    lv_obj_t *image = lv_image_create(map);
    lv_image_set_src(image, descriptor);
    lv_obj_set_pos(image, 0, 0);
    lv_obj_set_style_image_recolor(
        image,
        lv_color_hex(color),
        0);
    lv_obj_set_style_image_recolor_opa(
        image,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(image, opacity, 0);
    lv_obj_remove_flag(
        image,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    return image;
}

static void draw_eclipse_motion_overlay(lv_obj_t *map)
{
    if (!ensure_eclipse_motion_buffer()) {
        return;
    }
    memset(
        space_eclipse_motion_buffer,
        0,
        ISS_SOLAR_OVERLAY_PIXELS);
    prepare_space_solar_overlay_descriptor(
        &space_eclipse_motion_image,
        space_eclipse_motion_buffer);
    space_eclipse_motion_image_object =
        create_space_solar_overlay(
            map,
            &space_eclipse_motion_image,
            0xdbe4e6,
            LV_OPA_COVER);
    space_eclipse_motion_dirty_count = 0;
    if (!space_eclipse_terminator_active) {
        lv_obj_add_flag(
            space_eclipse_motion_image_object,
            LV_OBJ_FLAG_HIDDEN);
    }
}

static void draw_iss_day_night(lv_obj_t *map)
{
    if (!ensure_space_solar_overlay_buffers()) {
        return;
    }

    const time_t now = time(NULL);
    render_space_solar_overlay_mask(
        space_iss_night_buffer,
        now,
        false);
    render_space_solar_overlay_mask(
        space_iss_terminator_buffer,
        now,
        true);

    prepare_space_solar_overlay_descriptor(
        &space_iss_night_image,
        space_iss_night_buffer);
    prepare_space_solar_overlay_descriptor(
        &space_iss_terminator_image,
        space_iss_terminator_buffer);
    space_iss_night_image_object = create_space_solar_overlay(
        map,
        &space_iss_night_image,
        0x000000,
        LV_OPA_COVER);
    space_iss_terminator_image_object = create_space_solar_overlay(
        map,
        &space_iss_terminator_image,
        0x8e999c,
        LV_OPA_COVER);
}

static void draw_eclipse_day_night(
    lv_obj_t *map,
    time_t start,
    time_t end)
{
    if (!ensure_space_solar_overlay_buffers()) {
        return;
    }

    render_space_solar_overlay_mask(
        space_iss_night_buffer,
        start,
        false);
    /* The second PSRAM buffer is scratch storage for the next exact shadow
     * mask.  Keeping one visible image lets us update only the pixels swept by
     * the moving terminator instead of cross-fading two full-screen images. */
    render_space_solar_overlay_mask(
        space_iss_terminator_buffer,
        start,
        false);
    prepare_space_solar_overlay_descriptor(
        &space_iss_night_image,
        space_iss_night_buffer);

    const lv_opa_t start_opacity =
        space_eclipse_terminator_active
            ? LV_OPA_COVER
            : LV_OPA_TRANSP;
    space_iss_night_image_object = create_space_solar_overlay(
        map,
        &space_iss_night_image,
        0x000000,
        start_opacity);
    space_iss_terminator_image_object = NULL;
    if (!space_eclipse_terminator_active) {
        lv_obj_add_flag(
            space_iss_night_image_object,
            LV_OBJ_FLAG_HIDDEN);
    }
    space_eclipse_shade_step =
        space_eclipse_terminator_active
            ? 0
            : -1;

    (void)end;
}

static void update_eclipse_day_night_mask(
    time_t eclipse_time,
    float progress)
{
    if (space_iss_night_image_object == NULL ||
        space_iss_night_buffer == NULL ||
        space_iss_terminator_buffer == NULL) {
        return;
    }
    if (progress < 0.0f) {
        progress = 0.0f;
    } else if (progress > 1.0f) {
        progress = 1.0f;
    }
    const int step = (int)lroundf(
        progress * (float)ECLIPSE_SHADE_STEPS);
    if (step == space_eclipse_shade_step) {
        return;
    }
    space_eclipse_shade_step = step;

    render_space_solar_overlay_mask(
        space_iss_terminator_buffer,
        eclipse_time,
        false);

    lv_area_t dirty[ECLIPSE_SHADE_DIRTY_ROWS][2];
    bool dirty_valid[ECLIPSE_SHADE_DIRTY_ROWS][2] = {{false}};
    for (int y = 0; y < SCREEN_SIZE; y++) {
        const int row =
            y * ECLIPSE_SHADE_DIRTY_ROWS / SCREEN_SIZE;
        for (int x = 0; x < SCREEN_SIZE; x++) {
            const size_t pixel =
                (size_t)y * (size_t)SCREEN_SIZE + (size_t)x;
            const uint8_t next =
                space_iss_terminator_buffer[pixel];
            if (space_iss_night_buffer[pixel] == next) {
                continue;
            }
            space_iss_night_buffer[pixel] = next;
            const int side = x < CX ? 0 : 1;
            if (!dirty_valid[row][side]) {
                dirty[row][side] = (lv_area_t){
                    .x1 = x,
                    .y1 = y,
                    .x2 = x,
                    .y2 = y,
                };
                dirty_valid[row][side] = true;
            } else {
                if (x < dirty[row][side].x1) {
                    dirty[row][side].x1 = x;
                }
                if (x > dirty[row][side].x2) {
                    dirty[row][side].x2 = x;
                }
                if (y < dirty[row][side].y1) {
                    dirty[row][side].y1 = y;
                }
                if (y > dirty[row][side].y2) {
                    dirty[row][side].y2 = y;
                }
            }
        }
    }

    if (lv_obj_has_flag(
            space_iss_night_image_object,
            LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(
            space_iss_night_image_object,
            LV_OBJ_FLAG_HIDDEN);
    }
    for (int row = 0; row < ECLIPSE_SHADE_DIRTY_ROWS; row++) {
        for (int side = 0; side < 2; side++) {
            if (!dirty_valid[row][side]) {
                continue;
            }
            dirty[row][side].x1 = eclipse_clamp_screen(
                dirty[row][side].x1 - 1);
            dirty[row][side].y1 = eclipse_clamp_screen(
                dirty[row][side].y1 - 1);
            dirty[row][side].x2 = eclipse_clamp_screen(
                dirty[row][side].x2 + 1);
            dirty[row][side].y2 = eclipse_clamp_screen(
                dirty[row][side].y2 + 1);
            lv_obj_invalidate_area(
                space_iss_night_image_object,
                &dirty[row][side]);
        }
    }
}

static void interpolate_iss_position(const app_data_status_t *data,
                                     float *latitude,
                                     float *longitude)
{
    *latitude = data->iss_latitude;
    *longitude = data->iss_longitude;
    if (data->iss_track_count < 2) {
        return;
    }

    const int now = (int)time(NULL);
    int left = 0;
    for (int index = 0; index < data->iss_track_count - 1; index++) {
        left = index;
        if (now <= data->iss_track_timestamp[index + 1]) {
            break;
        }
    }
    const int right =
        left + 1 < data->iss_track_count ? left + 1 : left;
    if (right == left) {
        *latitude = data->iss_track_latitude[left];
        *longitude = data->iss_track_longitude[left];
        return;
    }

    const int duration =
        data->iss_track_timestamp[right] - data->iss_track_timestamp[left];
    float progress = duration > 0
        ? (float)(now - data->iss_track_timestamp[left]) / (float)duration
        : 0.0f;
    if (progress < 0.0f) {
        progress = 0.0f;
    } else if (progress > 1.0f) {
        progress = 1.0f;
    }
    float longitude_delta =
        data->iss_track_longitude[right] - data->iss_track_longitude[left];
    if (longitude_delta > 180.0f) {
        longitude_delta -= 360.0f;
    } else if (longitude_delta < -180.0f) {
        longitude_delta += 360.0f;
    }
    *latitude =
        data->iss_track_latitude[left] +
        (data->iss_track_latitude[right] - data->iss_track_latitude[left]) * progress;
    *longitude = data->iss_track_longitude[left] + longitude_delta * progress;
}

static void update_iss_marker_from_data(
    const app_data_status_t *data)
{
    if (space_iss_marker == NULL ||
        data == NULL ||
        !data->iss_valid) {
        return;
    }
    float latitude;
    float longitude;
    interpolate_iss_position(data, &latitude, &longitude);
    int x;
    int y;
    space_world_point(
        latitude,
        longitude,
        &x,
        &y);
    lv_obj_set_pos(
        space_iss_marker,
        x - ISS_MARKER_SIZE / 2,
        y - ISS_MARKER_SIZE / 2);
}

static void update_iss_marker(void)
{
    update_iss_marker_from_data(ui_data_status());
}

static void update_space_city_marker_pulse(void)
{
    if (!space_city_marker_visible ||
        space_city_marker == NULL ||
        space_city_marker_glow == NULL) {
        return;
    }
    const float phase =
        (float)(esp_timer_get_time() % 2600000LL) /
        2600000.0f * 2.0f * (float)M_PI;
    const float wave =
        0.5f + 0.5f * sinf(phase);
    const int size =
        22 + (int)lroundf(12.0f * wave);
    lv_obj_set_size(space_city_marker_glow, size, size);
    lv_obj_set_pos(
        space_city_marker_glow,
        space_city_marker_x - size / 2,
        space_city_marker_y - size / 2);
    lv_obj_set_style_bg_opa(
        space_city_marker_glow,
        (lv_opa_t)(28 + 52 * wave),
        0);
    lv_obj_set_style_shadow_opa(
        space_city_marker_glow,
        (lv_opa_t)(60 + 70 * wave),
        0);
    lv_obj_set_style_bg_opa(
        space_city_marker,
        (lv_opa_t)(190 + 65 * wave),
        0);
}

#define ISS_TRACK_SPLINE_SAMPLES 16
#define ISS_TRACK_MAX_SEGMENTS \
    ((APP_ISS_TRACK_POINTS - 1) * \
     ISS_TRACK_SPLINE_SAMPLES)
#define ISS_TRACK_FADE_STEPS 24
#define ISS_TRACK_MAX_GROUPS \
    (ISS_TRACK_FADE_STEPS + \
     APP_ISS_TRACK_POINTS)
#define ISS_TRACK_MAX_POINTS \
    (ISS_TRACK_MAX_SEGMENTS + \
     ISS_TRACK_MAX_GROUPS)

typedef struct {
    int first_point;
    int point_count;
    lv_opa_t opacity;
} iss_track_group_t;

typedef struct {
    lv_color_t color;
    int point_count;
    int group_count;
    lv_point_precise_t
        points[ISS_TRACK_MAX_POINTS];
    iss_track_group_t
        groups[ISS_TRACK_MAX_GROUPS];
} iss_track_draw_data_t;

static void iss_track_draw_event_cb(
    lv_event_t *event)
{
    iss_track_draw_data_t *data =
        (iss_track_draw_data_t *)
            lv_event_get_user_data(event);
    if (data == NULL) {
        return;
    }
    const lv_event_code_t code =
        lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        heap_caps_free(data);
        return;
    }
    if (code != LV_EVENT_DRAW_MAIN) {
        return;
    }

    lv_layer_t *layer =
        lv_event_get_layer(event);
    lv_draw_line_dsc_t descriptor;
    lv_draw_line_dsc_init(&descriptor);
    descriptor.base.layer = layer;
    descriptor.color = data->color;
    descriptor.width = 2;
    descriptor.round_start = false;
    descriptor.round_end = false;
    for (int index = 0;
         index < data->group_count;
         index++) {
        const iss_track_group_t *group =
            &data->groups[index];
        descriptor.opa =
            group->opacity;
        descriptor.points =
            &data->points[
                group->first_point];
        descriptor.point_cnt =
            group->point_count;
        lv_draw_line(
            layer,
            &descriptor);
    }
}

static float iss_track_spline_coordinate(
    float p0,
    float p1,
    float p2,
    float p3,
    float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return
        0.5f *
        (2.0f * p1 +
         (-p0 + p2) * t +
         (2.0f * p0 -
          5.0f * p1 +
          4.0f * p2 -
          p3) *
             t2 +
         (-p0 +
          3.0f * p1 -
          3.0f * p2 +
          p3) *
             t3);
}

static void append_iss_track_segment(
    iss_track_draw_data_t *data,
    int x1,
    int y1,
    int x2,
    int y2,
    float age_progress,
    bool force_new_group)
{
    if (data == NULL) {
        return;
    }
    age_progress =
        fmaxf(
            0.0f,
            fminf(
                1.0f,
                age_progress));
    const float smooth_fade =
        age_progress *
        age_progress *
        (3.0f -
         2.0f * age_progress);
    const int fade_step =
        (int)lroundf(
            smooth_fade *
            ISS_TRACK_FADE_STEPS);
    const lv_opa_t opacity =
        (lv_opa_t)(
            fade_step * 220 /
            ISS_TRACK_FADE_STEPS);
    if (opacity < 4) {
        return;
    }

    iss_track_group_t *group =
        data->group_count > 0
            ? &data->groups[
                  data->group_count - 1]
            : NULL;
    if (force_new_group ||
        group == NULL ||
        group->opacity != opacity) {
        if (data->group_count >=
                ISS_TRACK_MAX_GROUPS ||
            data->point_count + 2 >
                ISS_TRACK_MAX_POINTS) {
            return;
        }
        group =
            &data->groups[
                data->group_count++];
        *group =
            (iss_track_group_t) {
                .first_point =
                    data->point_count,
                .point_count = 0,
                .opacity = opacity,
            };
        data->points[
            data->point_count++] =
            (lv_point_precise_t) {
                .x = x1,
                .y = y1,
            };
        group->point_count++;
    }
    if (data->point_count >=
        ISS_TRACK_MAX_POINTS) {
        return;
    }
    data->points[
        data->point_count++] =
        (lv_point_precise_t) {
            .x = x2,
            .y = y2,
        };
    group->point_count++;
}

static void draw_iss_track(
    lv_obj_t *map,
    const app_data_status_t *data)
{
    if (data->iss_track_count < 2) {
        return;
    }

    int track_x[APP_ISS_TRACK_POINTS];
    int track_y[APP_ISS_TRACK_POINTS];
    int track_time[APP_ISS_TRACK_POINTS];
    const int now = (int)time(NULL);
    int track_count = 0;
    for (int index = 0;
         index < data->iss_track_count;
         index++) {
        if (data->iss_track_timestamp[index] > now) {
            break;
        }
        space_world_point(
            data->iss_track_latitude[index],
            data->iss_track_longitude[index],
            &track_x[track_count],
            &track_y[track_count]);
        track_time[track_count] =
            data->iss_track_timestamp[index];
        track_count++;
    }
    if (track_count < APP_ISS_TRACK_POINTS &&
        track_count > 0 &&
        data->iss_track_timestamp[
            track_count - 1] < now) {
        float latitude;
        float longitude;
        interpolate_iss_position(
            data,
            &latitude,
            &longitude);
        space_world_point(
            latitude,
            longitude,
            &track_x[track_count],
            &track_y[track_count]);
        track_time[track_count] = now;
        track_count++;
    }
    if (track_count < 2) {
        return;
    }

    iss_track_draw_data_t *draw_data =
        heap_caps_calloc(
            1,
            sizeof(*draw_data),
            MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT);
    if (draw_data == NULL) {
        draw_data =
            heap_caps_calloc(
                1,
                sizeof(*draw_data),
                MALLOC_CAP_8BIT);
    }
    if (draw_data == NULL) {
        return;
    }
    draw_data->color =
        lv_color_hex(
            radar_palette_active->main);

    const int oldest_time =
        track_time[0];
    const float history_duration =
        (float)(
            now > oldest_time
                ? now - oldest_time
                : 1);
    int run_start = 0;
    while (run_start < track_count - 1) {
        int run_last = run_start;
        while (
            run_last + 1 < track_count &&
            abs(
                track_x[run_last + 1] -
                track_x[run_last]) <
                SCREEN_SIZE / 2) {
            run_last++;
        }
        for (int segment = run_start;
             segment < run_last;
             segment++) {
            const int p0 =
                segment > run_start
                    ? segment - 1
                    : segment;
            const int p1 = segment;
            const int p2 = segment + 1;
            const int p3 =
                segment + 2 <= run_last
                    ? segment + 2
                    : segment + 1;
            int previous_x = track_x[p1];
            int previous_y = track_y[p1];
            for (int sample = 1;
                 sample <=
                 ISS_TRACK_SPLINE_SAMPLES;
                 sample++) {
                const float t =
                    (float)sample /
                    ISS_TRACK_SPLINE_SAMPLES;
                const int next_x =
                    (int)lroundf(
                        iss_track_spline_coordinate(
                            track_x[p0],
                            track_x[p1],
                            track_x[p2],
                            track_x[p3],
                            t));
                const int next_y =
                    (int)lroundf(
                        iss_track_spline_coordinate(
                            track_y[p0],
                            track_y[p1],
                            track_y[p2],
                            track_y[p3],
                            t));
                const float segment_time =
                    track_time[p1] +
                    (track_time[p2] -
                     track_time[p1]) *
                        ((float)sample - 0.5f) /
                        ISS_TRACK_SPLINE_SAMPLES;
                append_iss_track_segment(
                    draw_data,
                    previous_x,
                    previous_y,
                    next_x,
                    next_y,
                    (segment_time -
                     oldest_time) /
                        history_duration,
                    segment == run_start &&
                        sample == 1);
                previous_x = next_x;
                previous_y = next_y;
            }
        }
        run_start = run_last + 1;
    }

    if (draw_data->group_count == 0) {
        heap_caps_free(draw_data);
        return;
    }
    lv_obj_t *track =
        lv_obj_create(map);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(
        track,
        SCREEN_SIZE,
        SCREEN_SIZE);
    lv_obj_set_pos(track, 0, 0);
    lv_obj_remove_flag(
        track,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(
        track,
        iss_track_draw_event_cb,
        LV_EVENT_ALL,
        draw_data);
}

typedef struct {
    float latitude;
    float longitude;
} eclipse_geo_point_t;

typedef struct {
    int64_t display_until_unix;
    int64_t path_start_unix;
    int64_t path_end_unix;
    const char *date_label;
    const eclipse_geo_point_t *points;
    size_t point_count;
} eclipse_definition_t;

/*
 * Central-line samples from Fred Espenak's NASA GSFC eclipse path tables.
 * The source tables use WGS 84 coordinates at two-minute intervals; this
 * compact selection is spline-interpolated for the 466 px display.
 * Eclipse Predictions by Fred Espenak, NASA's GSFC.
 * https://eclipse.gsfc.nasa.gov/SEpath/SEpath2001/SE2026Aug12Tpath.html
 * https://eclipse.gsfc.nasa.gov/SEpath/SEpath2001/SE2027Aug02Tpath.html
 * https://eclipse.gsfc.nasa.gov/SEpath/SEpath2001/SE2028Jul22Tpath.html
 */
static const eclipse_geo_point_t eclipse_2026_aug_12[] = {
    {75.078f, 113.452f},
    {82.275f, 112.487f},
    {87.278f, 81.525f},
    {86.835f, -1.638f},
    {82.495f, -24.272f},
    {78.483f, -27.540f},
    {74.837f, -27.905f},
    {71.450f, -27.362f},
    {68.247f, -26.410f},
    {65.172f, -25.205f},
    {62.183f, -23.793f},
    {59.245f, -22.170f},
    {56.322f, -20.287f},
    {53.372f, -18.057f},
    {50.333f, -15.317f},
    {47.102f, -11.715f},
    {43.372f, -6.188f},
    {39.408f, 2.950f},
    {38.680f, 5.415f},
};

static const eclipse_geo_point_t eclipse_2027_aug_02[] = {
    {27.962f, -44.477f},
    {32.117f, -30.090f},
    {34.688f, -17.003f},
    {35.743f, -1.823f},
    {35.477f, 7.555f},
    {33.952f, 14.090f},
    {31.907f, 20.543f},
    {29.585f, 26.097f},
    {27.293f, 30.342f},
    {24.775f, 34.257f},
    {22.043f, 37.958f},
    {19.095f, 41.572f},
    {16.460f, 44.607f},
    {13.643f, 47.765f},
    {10.610f, 51.172f},
    {7.300f, 55.013f},
    {3.602f, 59.615f},
    {-0.728f, 65.695f},
};

static const eclipse_geo_point_t eclipse_2028_jul_22[] = {
    {-18.385f, 75.630f},
    {-12.688f, 93.535f},
    {-10.713f, 107.153f},
    {-11.372f, 115.505f},
    {-12.218f, 118.942f},
    {-13.342f, 122.110f},
    {-14.718f, 125.110f},
    {-16.345f, 128.030f},
    {-18.227f, 130.952f},
    {-20.383f, 133.960f},
    {-22.857f, 137.172f},
    {-25.712f, 140.740f},
    {-29.077f, 144.932f},
    {-33.203f, 150.270f},
    {-37.480f, 156.265f},
    {-44.133f, 167.150f},
    {-50.498f, -179.495f},
};

static const eclipse_definition_t eclipse_definitions[] = {
    {
        .display_until_unix = 1786564800LL,
        .path_start_unix = 1786554120LL,
        .path_end_unix = 1786559520LL,
        .date_label = "12 AUG 2026",
        .points = eclipse_2026_aug_12,
        .point_count = sizeof(eclipse_2026_aug_12) /
            sizeof(eclipse_2026_aug_12[0]),
    },
    {
        .display_until_unix = 1817211600LL,
        .path_start_unix = 1817195160LL,
        .path_end_unix = 1817207280LL,
        .date_label = "02 AUG 2027",
        .points = eclipse_2027_aug_02,
        .point_count = sizeof(eclipse_2027_aug_02) /
            sizeof(eclipse_2027_aug_02[0]),
    },
    {
        .display_until_unix = 1847883600LL,
        .path_start_unix = 1847842440LL,
        .path_end_unix = 1847852280LL,
        .date_label = "22 JUL 2028",
        .points = eclipse_2028_jul_22,
        .point_count = sizeof(eclipse_2028_jul_22) /
            sizeof(eclipse_2028_jul_22[0]),
    },
};

static const eclipse_definition_t *next_total_eclipse(void)
{
    const time_t now = time(NULL);
    if (!space_ephemeris_time_is_valid(now)) {
        return NULL;
    }
    for (size_t index = 0;
         index < sizeof(eclipse_definitions) /
             sizeof(eclipse_definitions[0]);
         index++) {
        if ((int64_t)now <=
            eclipse_definitions[index].display_until_unix) {
            return &eclipse_definitions[index];
        }
    }
    return &eclipse_definitions[
        sizeof(eclipse_definitions) /
            sizeof(eclipse_definitions[0]) - 1];
}

static void hide_eclipse_terminator_objects(void)
{
    if (space_eclipse_time_label != NULL) {
        lv_obj_add_flag(
            space_eclipse_time_label,
            LV_OBJ_FLAG_HIDDEN);
    }
    if (space_eclipse_motion_image_object != NULL) {
        lv_obj_add_flag(
            space_eclipse_motion_image_object,
            LV_OBJ_FLAG_HIDDEN);
    }
    if (space_iss_night_image_object != NULL) {
        lv_obj_add_flag(
            space_iss_night_image_object,
            LV_OBJ_FLAG_HIDDEN);
    }
    if (space_iss_terminator_image_object != NULL) {
        lv_obj_add_flag(
            space_iss_terminator_image_object,
            LV_OBJ_FLAG_HIDDEN);
    }
    space_eclipse_motion_dirty_count = 0;
}

static void stop_eclipse_terminator_animation(void)
{
    space_eclipse_terminator_active = false;
    space_eclipse_terminator_started_us = 0;
    if (space_eclipse_terminator_timer != NULL) {
        lv_timer_pause(space_eclipse_terminator_timer);
    }
    hide_eclipse_terminator_objects();
}

static int eclipse_clamp_screen(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value >= SCREEN_SIZE) {
        return SCREEN_SIZE - 1;
    }
    return value;
}

static void eclipse_append_motion_dirty_area(
    float left,
    float top,
    float right,
    float bottom)
{
    if (space_eclipse_motion_dirty_count >=
        ECLIPSE_TERMINATOR_DIRTY_AREAS) {
        return;
    }
    const int margin = 2;
    lv_area_t *area =
        &space_eclipse_motion_dirty[
            space_eclipse_motion_dirty_count++];
    area->x1 = eclipse_clamp_screen(
        (int)floorf(left) - margin);
    area->y1 = eclipse_clamp_screen(
        (int)floorf(top) - margin);
    area->x2 = eclipse_clamp_screen(
        (int)ceilf(right) + margin);
    area->y2 = eclipse_clamp_screen(
        (int)ceilf(bottom) + margin);
}

static void rasterize_eclipse_terminator_segment(
    const eclipse_float_point_t *start,
    const eclipse_float_point_t *end,
    float envelope)
{
    if (space_eclipse_motion_buffer == NULL ||
        start == NULL || end == NULL) {
        return;
    }
    const float dx = end->x - start->x;
    const float dy = end->y - start->y;
    const float length_squared = dx * dx + dy * dy;
    if (length_squared < 0.001f) {
        return;
    }
    int left = (int)floorf(fminf(start->x, end->x) - 1.2f);
    int right = (int)ceilf(fmaxf(start->x, end->x) + 1.2f);
    int top = (int)floorf(fminf(start->y, end->y) - 1.2f);
    int bottom = (int)ceilf(fmaxf(start->y, end->y) + 1.2f);
    if (left < ISS_MAP_LEFT) {
        left = ISS_MAP_LEFT;
    }
    if (right > ISS_MAP_RIGHT) {
        right = ISS_MAP_RIGHT;
    }
    if (top < ISS_MAP_TOP) {
        top = ISS_MAP_TOP;
    }
    if (bottom > ISS_MAP_BOTTOM) {
        bottom = ISS_MAP_BOTTOM;
    }
    if (left > right || top > bottom) {
        return;
    }

    for (int y = top; y <= bottom; y++) {
        for (int x = left; x <= right; x++) {
            const float pixel_x = (float)x + 0.5f;
            const float pixel_y = (float)y + 0.5f;
            float position =
                ((pixel_x - start->x) * dx +
                 (pixel_y - start->y) * dy) /
                length_squared;
            if (position < 0.0f) {
                position = 0.0f;
            } else if (position > 1.0f) {
                position = 1.0f;
            }
            const float nearest_x = start->x + position * dx;
            const float nearest_y = start->y + position * dy;
            const float distance_x = pixel_x - nearest_x;
            const float distance_y = pixel_y - nearest_y;
            const float distance = sqrtf(
                distance_x * distance_x +
                distance_y * distance_y);
            float coverage =
                1.0f - (distance - 0.20f) / 0.78f;
            if (coverage <= 0.0f) {
                continue;
            }
            if (coverage > 1.0f) {
                coverage = 1.0f;
            }
            coverage = coverage * coverage *
                (3.0f - 2.0f * coverage);
            const uint8_t alpha = (uint8_t)lroundf(
                178.0f * envelope * coverage);
            const size_t pixel =
                (size_t)y * (size_t)SCREEN_SIZE +
                (size_t)x;
            if (alpha > space_eclipse_motion_buffer[pixel]) {
                space_eclipse_motion_buffer[pixel] = alpha;
            }
        }
    }
}

static void render_eclipse_terminator_motion(
    time_t eclipse_time,
    float envelope)
{
    if (space_eclipse_motion_image_object == NULL ||
        space_eclipse_motion_buffer == NULL) {
        return;
    }

    lv_area_t previous_dirty[ECLIPSE_TERMINATOR_DIRTY_AREAS];
    const int previous_dirty_count =
        space_eclipse_motion_dirty_count;
    if (previous_dirty_count > 0) {
        memcpy(
            previous_dirty,
            space_eclipse_motion_dirty,
            (size_t)previous_dirty_count * sizeof(previous_dirty[0]));
    }
    memset(
        space_eclipse_motion_buffer,
        0,
        ISS_SOLAR_OVERLAY_PIXELS);
    space_eclipse_motion_dirty_count = 0;

    float declination_rad = 0.0f;
    float subsolar_longitude = 0.0f;
    calculate_solar_position(
        eclipse_time,
        &declination_rad,
        &subsolar_longitude);
    const float tangent_declination = tanf(declination_rad);
    int group_count = 0;
    for (int branch = 0; branch < 2; branch++) {
        int current_group = -1;
        float previous_x = 0.0f;
        bool previous_valid = false;
        for (int sample = 0;
             sample < ECLIPSE_TERMINATOR_SAMPLE_COUNT;
             sample++) {
            const float latitude =
                -88.0f +
                176.0f * (float)sample /
                    (float)(ECLIPSE_TERMINATOR_SAMPLE_COUNT - 1);
            const float latitude_rad = latitude * DEG_TO_RAD;
            const float hour_cosine =
                -tanf(latitude_rad) * tangent_declination;
            if (hour_cosine < -1.0f || hour_cosine > 1.0f) {
                previous_valid = false;
                current_group = -1;
                continue;
            }
            const float hour_angle =
                acosf(hour_cosine) / DEG_TO_RAD;
            float longitude =
                subsolar_longitude +
                (branch == 0 ? hour_angle : -hour_angle);
            while (longitude > 180.0f) {
                longitude -= 360.0f;
            }
            while (longitude < -180.0f) {
                longitude += 360.0f;
            }
            float x = 0.0f;
            float y = 0.0f;
            space_world_point_float(latitude, longitude, &x, &y);
            const bool wrapped =
                previous_valid &&
                fabsf(x - previous_x) >= (float)SCREEN_SIZE * 0.5f;
            if (!previous_valid || wrapped || current_group < 0) {
                if (group_count >= ECLIPSE_TERMINATOR_MAX_GROUPS) {
                    current_group = -1;
                    previous_valid = false;
                    continue;
                }
                current_group = group_count++;
                space_eclipse_terminator_groups[
                    current_group].point_count = 0;
            }
            eclipse_terminator_group_t *group =
                &space_eclipse_terminator_groups[current_group];
            if (group->point_count >=
                ECLIPSE_TERMINATOR_POINTS_PER_GROUP) {
                previous_valid = false;
                current_group = -1;
                continue;
            }
            group->points[group->point_count++] =
                (eclipse_float_point_t) {.x = x, .y = y};
            previous_x = x;
            previous_valid = true;
        }
    }

    for (int group_index = 0;
         group_index < group_count;
         group_index++) {
        eclipse_terminator_group_t *group =
            &space_eclipse_terminator_groups[group_index];
        if (group->point_count < 2) {
            continue;
        }
        for (int point = 0;
             point < group->point_count - 1;
             point++) {
            rasterize_eclipse_terminator_segment(
                &group->points[point],
                &group->points[point + 1],
                envelope);
        }
        /* Invalidate each continuous branch as one coherent region.  The
         * panel's six-row transfer buffer otherwise exposes the individual
         * curve chunks while they are being sent, which looks like the line
         * is being assembled piece by piece. */
        {
            float left = group->points[0].x;
            float right = left;
            float top = group->points[0].y;
            float bottom = top;
            for (int point = 1;
                 point < group->point_count;
                 point++) {
                left = fminf(left, group->points[point].x);
                right = fmaxf(right, group->points[point].x);
                top = fminf(top, group->points[point].y);
                bottom = fmaxf(bottom, group->points[point].y);
            }
            eclipse_append_motion_dirty_area(
                left,
                top,
                right,
                bottom);
        }
    }

    if (lv_obj_has_flag(
            space_eclipse_motion_image_object,
            LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(
            space_eclipse_motion_image_object,
            LV_OBJ_FLAG_HIDDEN);
    }
    for (int index = 0; index < previous_dirty_count; index++) {
        lv_obj_invalidate_area(
            space_eclipse_motion_image_object,
            &previous_dirty[index]);
    }
    for (int index = 0;
         index < space_eclipse_motion_dirty_count;
         index++) {
        lv_obj_invalidate_area(
            space_eclipse_motion_image_object,
            &space_eclipse_motion_dirty[index]);
    }
}

static void update_eclipse_terminator_animation(void)
{
    if (!space_eclipse_terminator_active ||
        current_screen != SCREEN_SPACE ||
        !space_iss_visible ||
        !space_eclipse_visible ||
        screen_root == NULL) {
        return;
    }

    const eclipse_definition_t *eclipse = next_total_eclipse();
    if (eclipse == NULL ||
        eclipse->path_end_unix <= eclipse->path_start_unix) {
        stop_eclipse_terminator_animation();
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if (space_eclipse_terminator_started_us <= 0) {
        space_eclipse_terminator_started_us = now_us;
    }
    float progress =
        (float)(now_us - space_eclipse_terminator_started_us) /
        (float)ECLIPSE_TERMINATOR_ANIMATION_US;
    if (progress >= 1.0f) {
        update_total_eclipse_path_progress(1.0f, true);
        stop_eclipse_terminator_animation();
        return;
    }
    if (progress < 0.0f) {
        progress = 0.0f;
    }

    /* A linear timeline keeps the compressed celestial motion at a constant
     * apparent velocity.  The shadow is retained in one A8 mask and only the
     * narrow regions crossed since its previous step are transferred. */
    const time_t eclipse_time = (time_t)llround(
        (double)eclipse->path_start_unix +
        (double)(eclipse->path_end_unix -
                 eclipse->path_start_unix) *
            progress);
    update_eclipse_day_night_mask(
        eclipse_time,
        progress);
    update_total_eclipse_path_progress(progress, false);

    float envelope = 1.0f;
    if (progress < 0.08f) {
        const float fade = progress / 0.08f;
        envelope = fade * fade * (3.0f - 2.0f * fade);
    } else if (progress > 0.86f) {
        const float fade =
            (1.0f - progress) / 0.14f;
        envelope = fade * fade * (3.0f - 2.0f * fade);
    }
    if (envelope < 0.0f) {
        envelope = 0.0f;
    } else if (envelope > 1.0f) {
        envelope = 1.0f;
    }

    render_eclipse_terminator_motion(
        eclipse_time,
        envelope);

    if (space_eclipse_time_label == NULL) {
        space_eclipse_time_label = lv_label_create(screen_root);
        lv_obj_set_pos(space_eclipse_time_label, 138, 21);
        lv_obj_set_size(space_eclipse_time_label, 190, 30);
        lv_obj_set_style_text_font(
            space_eclipse_time_label,
            &lv_font_montserrat_18,
            0);
        lv_obj_set_style_text_color(
            space_eclipse_time_label,
            lv_color_hex(0xffffff),
            0);
        lv_obj_set_style_text_align(
            space_eclipse_time_label,
            LV_TEXT_ALIGN_CENTER,
            0);
        lv_obj_set_style_text_letter_space(
            space_eclipse_time_label,
            2,
            0);
        lv_obj_remove_flag(
            space_eclipse_time_label,
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(space_eclipse_time_label);
    }
    struct tm utc;
    gmtime_r(&eclipse_time, &utc);
    char time_text[24];
    snprintf(
        time_text,
        sizeof(time_text),
        "%02d:%02d UTC",
        utc.tm_hour,
        utc.tm_min);
    lv_label_set_text(space_eclipse_time_label, time_text);
    lv_obj_set_style_text_opa(
        space_eclipse_time_label,
        (lv_opa_t)lroundf(255.0f * envelope),
        0);
    lv_obj_remove_flag(
        space_eclipse_time_label,
        LV_OBJ_FLAG_HIDDEN);
}

static void eclipse_terminator_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_eclipse_terminator_animation();
}

static void start_eclipse_terminator_animation(void)
{
    space_eclipse_terminator_active = true;
    space_eclipse_terminator_started_us = esp_timer_get_time();
    space_eclipse_shade_step = -1;
    if (space_eclipse_terminator_timer == NULL) {
        space_eclipse_terminator_timer = lv_timer_create(
            eclipse_terminator_timer_cb,
            ECLIPSE_TERMINATOR_FRAME_MS,
            NULL);
    } else {
        lv_timer_set_period(
            space_eclipse_terminator_timer,
            ECLIPSE_TERMINATOR_FRAME_MS);
        lv_timer_reset(space_eclipse_terminator_timer);
        lv_timer_resume(space_eclipse_terminator_timer);
    }
    if (space_eclipse_time_label != NULL) {
        /* A long-button replay happens without rebuilding the screen.  The
         * previous run deliberately hid the retained mask at completion, so
         * restore its exact first frame before resuming partial updates. */
        const eclipse_definition_t *eclipse =
            next_total_eclipse();
        if (eclipse != NULL &&
            space_iss_night_image_object != NULL &&
            space_iss_night_buffer != NULL &&
            space_iss_terminator_buffer != NULL) {
            render_space_solar_overlay_mask(
                space_iss_night_buffer,
                (time_t)eclipse->path_start_unix,
                false);
            memcpy(
                space_iss_terminator_buffer,
                space_iss_night_buffer,
                ISS_SOLAR_OVERLAY_PIXELS);
            lv_obj_remove_flag(
                space_iss_night_image_object,
                LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_image_opa(
                space_iss_night_image_object,
                LV_OPA_COVER,
                0);
            lv_obj_invalidate(space_iss_night_image_object);
            space_eclipse_shade_step = 0;
        }
        update_eclipse_terminator_animation();
    }
}

#define ECLIPSE_SPLINE_SAMPLES 16
#define ECLIPSE_PATH_MAX_POINTS 512
#define ECLIPSE_PATH_MAX_GROUPS 4

typedef struct {
    int first_point;
    int point_count;
} eclipse_path_group_t;

struct eclipse_path_image {
    lv_image_dsc_t image;
    uint8_t *mask;
    uint8_t *final_alpha;
    uint8_t *path_progress;
    lv_obj_t *object;
    size_t pixel_count;
    int x;
    int y;
    int width;
    int height;
    int last_reveal;
};

static void eclipse_path_draw_event_cb(lv_event_t *event)
{
    eclipse_path_image_t *data =
        (eclipse_path_image_t *)
            lv_event_get_user_data(event);
    if (data != NULL &&
        lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (space_eclipse_path_asset == data) {
            space_eclipse_path_asset = NULL;
        }
        heap_caps_free(data->mask);
        heap_caps_free(data->final_alpha);
        heap_caps_free(data->path_progress);
        heap_caps_free(data);
    }
}

static void rasterize_eclipse_path_segment(
    uint8_t *mask,
    int mask_width,
    int mask_height,
    int mask_x,
    int mask_y,
    const lv_point_precise_t *start,
    const lv_point_precise_t *end,
    uint8_t *path_progress,
    float start_progress,
    float end_progress)
{
    const float start_x = (float)start->x;
    const float start_y = (float)start->y;
    const float dx = (float)end->x - start_x;
    const float dy = (float)end->y - start_y;
    const float length_squared = dx * dx + dy * dy;
    if (length_squared < 0.01f) {
        return;
    }

    int left = (int)floorf(fminf(start_x, (float)end->x) - 5.0f);
    int right = (int)ceilf(fmaxf(start_x, (float)end->x) + 5.0f);
    int top = (int)floorf(fminf(start_y, (float)end->y) - 5.0f);
    int bottom = (int)ceilf(fmaxf(start_y, (float)end->y) + 5.0f);
    left = left < mask_x ? mask_x : left;
    right = right >= mask_x + mask_width
        ? mask_x + mask_width - 1
        : right;
    top = top < mask_y ? mask_y : top;
    bottom = bottom >= mask_y + mask_height
        ? mask_y + mask_height - 1
        : bottom;

    for (int y = top; y <= bottom; y++) {
        for (int x = left; x <= right; x++) {
            const float pixel_x = (float)x + 0.5f;
            const float pixel_y = (float)y + 0.5f;
            float position =
                ((pixel_x - start_x) * dx +
                 (pixel_y - start_y) * dy) /
                length_squared;
            if (position < 0.0f) {
                position = 0.0f;
            } else if (position > 1.0f) {
                position = 1.0f;
            }
            const float nearest_x = start_x + position * dx;
            const float nearest_y = start_y + position * dy;
            const float distance_x = pixel_x - nearest_x;
            const float distance_y = pixel_y - nearest_y;
            const float distance = sqrtf(
                distance_x * distance_x +
                distance_y * distance_y);

            const size_t index =
                (size_t)(y - mask_y) * (size_t)mask_width +
                (size_t)(x - mask_x);
            /* Store a quantized distance field first.  Turning the minimum
             * distance to the complete spline into opacity in a second pass
             * avoids both accumulated alpha and the bright circular end-cap
             * left by every individual tessellation segment. */
            const uint8_t quantized_distance =
                (uint8_t)fminf(254.0f, lroundf(distance * 32.0f));
            if (quantized_distance < mask[index]) {
                mask[index] = quantized_distance;
                path_progress[index] =
                    (uint8_t)lroundf(
                        fminf(
                            255.0f,
                            fmaxf(
                                0.0f,
                                (start_progress +
                                 (end_progress - start_progress) *
                                     position) *
                                    255.0f)));
            }
        }
    }
}

static void update_total_eclipse_path_progress(
    float progress,
    bool complete)
{
    eclipse_path_image_t *asset =
        space_eclipse_path_asset;
    if (asset == NULL || asset->mask == NULL ||
        asset->final_alpha == NULL ||
        asset->path_progress == NULL ||
        asset->object == NULL) {
        return;
    }
    if (progress < 0.0f) {
        progress = 0.0f;
    } else if (progress > 1.0f) {
        progress = 1.0f;
    }
    const float reveal = progress * 255.0f;
    const int reveal_step =
        complete ? UINT8_MAX : (uint8_t)lroundf(reveal);
    if (!complete && reveal_step == asset->last_reveal) {
        return;
    }
    asset->last_reveal = reveal_step;
    int dirty_left = asset->width;
    int dirty_right = -1;
    int dirty_top = asset->height;
    int dirty_bottom = -1;
    for (size_t index = 0;
         index < asset->pixel_count;
         index++) {
        const uint8_t final_alpha =
            asset->final_alpha[index];
        uint8_t next_alpha = 0;
        if (final_alpha == 0) {
            next_alpha = 0;
        } else if (complete) {
            next_alpha = final_alpha;
        } else {
            /* A narrow antialiased reveal front travels with the simulated
             * eclipse time.  All earlier pixels retain their final band and
             * edge opacity, so the route grows as one continuous trace rather
             * than as overlapping line segments. */
            const float coverage =
                reveal - (float)asset->path_progress[index] + 1.25f;
            if (coverage <= 0.0f) {
                next_alpha = 0;
            } else if (coverage >= 2.5f) {
                next_alpha = final_alpha;
            } else {
                float blend = coverage / 2.5f;
                blend = blend * blend * (3.0f - 2.0f * blend);
                next_alpha =
                    (uint8_t)lroundf(
                        (float)final_alpha * blend);
            }
        }
        if (asset->mask[index] == next_alpha) {
            continue;
        }
        asset->mask[index] = next_alpha;
        const int local_x = (int)(index % (size_t)asset->width);
        const int local_y = (int)(index / (size_t)asset->width);
        if (local_x < dirty_left) {
            dirty_left = local_x;
        }
        if (local_x > dirty_right) {
            dirty_right = local_x;
        }
        if (local_y < dirty_top) {
            dirty_top = local_y;
        }
        if (local_y > dirty_bottom) {
            dirty_bottom = local_y;
        }
    }
    if (dirty_right >= dirty_left && dirty_bottom >= dirty_top) {
        lv_area_t dirty = {
            .x1 = eclipse_clamp_screen(
                asset->x + dirty_left - 1),
            .y1 = eclipse_clamp_screen(
                asset->y + dirty_top - 1),
            .x2 = eclipse_clamp_screen(
                asset->x + dirty_right + 1),
            .y2 = eclipse_clamp_screen(
                asset->y + dirty_bottom + 1),
        };
        lv_obj_invalidate_area(asset->object, &dirty);
    }
}

static void draw_total_eclipse_path(
    lv_obj_t *map,
    const eclipse_definition_t *eclipse)
{
    if (eclipse == NULL || eclipse->point_count < 2) {
        return;
    }
    int path_x[32];
    int path_y[32];
    const int point_count =
        (int)fminf((float)eclipse->point_count, 32.0f);
    for (int index = 0; index < point_count; index++) {
        space_world_point(
            eclipse->points[index].latitude,
            eclipse->points[index].longitude,
            &path_x[index],
            &path_y[index]);
    }

    lv_point_precise_t *points =
        heap_caps_calloc(
            ECLIPSE_PATH_MAX_POINTS,
            sizeof(*points),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (points == NULL) {
        points = heap_caps_calloc(
            ECLIPSE_PATH_MAX_POINTS,
            sizeof(*points),
            MALLOC_CAP_8BIT);
    }
    if (points == NULL) {
        return;
    }

    eclipse_path_group_t groups[ECLIPSE_PATH_MAX_GROUPS] = {0};
    int spline_point_count = 0;
    int group_count = 0;
    int run_start = 0;
    while (run_start < point_count - 1 &&
           group_count < ECLIPSE_PATH_MAX_GROUPS) {
        int run_end = run_start;
        while (run_end + 1 < point_count &&
               abs(path_x[run_end + 1] - path_x[run_end]) <
                   SCREEN_SIZE / 2) {
            run_end++;
        }
        if (run_end > run_start) {
            eclipse_path_group_t *group =
                &groups[group_count++];
            group->first_point = spline_point_count;
            points[spline_point_count++] =
                (lv_point_precise_t) {
                    .x = path_x[run_start],
                    .y = path_y[run_start],
                };
            group->point_count = 1;
            for (int segment = run_start;
                 segment < run_end;
                 segment++) {
                const int p0 = segment > run_start
                    ? segment - 1
                    : segment;
                const int p1 = segment;
                const int p2 = segment + 1;
                const int p3 = segment + 2 <= run_end
                    ? segment + 2
                    : segment + 1;
                for (int sample = 1;
                     sample <= ECLIPSE_SPLINE_SAMPLES &&
                     spline_point_count <
                         ECLIPSE_PATH_MAX_POINTS;
                     sample++) {
                    const float t =
                        (float)sample /
                        ECLIPSE_SPLINE_SAMPLES;
                    points[spline_point_count++] =
                        (lv_point_precise_t) {
                            .x = (int)lroundf(
                                iss_track_spline_coordinate(
                                    path_x[p0], path_x[p1],
                                    path_x[p2], path_x[p3], t)),
                            .y = (int)lroundf(
                                iss_track_spline_coordinate(
                                    path_y[p0], path_y[p1],
                                    path_y[p2], path_y[p3], t)),
                        };
                    group->point_count++;
                }
            }
        }
        run_start = run_end + 1;
    }

    if (group_count == 0 || spline_point_count < 2) {
        heap_caps_free(points);
        return;
    }

    int left = points[0].x;
    int right = points[0].x;
    int top = points[0].y;
    int bottom = points[0].y;
    for (int index = 1; index < spline_point_count; index++) {
        left = points[index].x < left ? points[index].x : left;
        right = points[index].x > right ? points[index].x : right;
        top = points[index].y < top ? points[index].y : top;
        bottom = points[index].y > bottom ? points[index].y : bottom;
    }
    left = left > 5 ? left - 5 : 0;
    right = right < SCREEN_SIZE - 6 ? right + 5 : SCREEN_SIZE - 1;
    top = top > 5 ? top - 5 : 0;
    bottom = bottom < SCREEN_SIZE - 6 ? bottom + 5 : SCREEN_SIZE - 1;
    const int mask_width = right - left + 1;
    const int mask_height = bottom - top + 1;

    eclipse_path_image_t *asset = heap_caps_calloc(
        1,
        sizeof(*asset),
        MALLOC_CAP_8BIT);
    if (asset != NULL) {
        asset->mask = heap_caps_calloc(
            (size_t)mask_width * (size_t)mask_height,
            1,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        asset->final_alpha = heap_caps_calloc(
            (size_t)mask_width * (size_t)mask_height,
            1,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        asset->path_progress = heap_caps_calloc(
            (size_t)mask_width * (size_t)mask_height,
            1,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (asset == NULL || asset->mask == NULL ||
        asset->final_alpha == NULL ||
        asset->path_progress == NULL) {
        if (asset != NULL) {
            heap_caps_free(asset->mask);
            heap_caps_free(asset->final_alpha);
            heap_caps_free(asset->path_progress);
            heap_caps_free(asset);
        }
        heap_caps_free(points);
        return;
    }
    memset(
        asset->mask,
        0xff,
        (size_t)mask_width * (size_t)mask_height);
    asset->pixel_count =
        (size_t)mask_width * (size_t)mask_height;
    asset->x = left;
    asset->y = top;
    asset->width = mask_width;
    asset->height = mask_height;
    asset->last_reveal = UINT8_MAX;

    for (int group_index = 0;
         group_index < group_count;
         group_index++) {
        const eclipse_path_group_t *group = &groups[group_index];
        const int last =
            group->first_point + group->point_count - 1;
        for (int index = group->first_point;
             index < last;
             index++) {
            rasterize_eclipse_path_segment(
                asset->mask,
                mask_width,
                mask_height,
                left,
                top,
                &points[index],
                &points[index + 1],
                asset->path_progress,
                (float)index /
                    (float)(spline_point_count - 1),
                (float)(index + 1) /
                    (float)(spline_point_count - 1));
        }
    }
    heap_caps_free(points);

    const size_t mask_size =
        (size_t)mask_width * (size_t)mask_height;
    for (size_t index = 0; index < mask_size; index++) {
        const uint8_t quantized_distance = asset->mask[index];
        if (quantized_distance == 0xff) {
            asset->mask[index] = 0;
            continue;
        }
        const float distance =
            (float)quantized_distance / 32.0f;
        /* A single translucent totality band.  The former opaque rails made
         * tessellation boundaries and diagonal stair-steps conspicuous on
         * the AMOLED.  Keep the requested 30% interior and spend one pixel
         * only on a smooth antialiased falloff. */
        float coverage =
            1.0f - (distance - 2.65f) / 1.05f;
        if (coverage < 0.0f) {
            coverage = 0.0f;
        } else if (coverage > 1.0f) {
            coverage = 1.0f;
        }
        coverage = coverage * coverage *
            (3.0f - 2.0f * coverage);
        const float alpha = 77.0f * coverage;
        asset->final_alpha[index] =
            (uint8_t)lroundf(alpha);
        asset->mask[index] =
            asset->final_alpha[index];
    }

    memset(&asset->image, 0, sizeof(asset->image));
    asset->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    asset->image.header.cf = LV_COLOR_FORMAT_A8;
    asset->image.header.w = mask_width;
    asset->image.header.h = mask_height;
    asset->image.header.stride = mask_width;
    asset->image.data_size =
        (size_t)mask_width * (size_t)mask_height;
    asset->image.data = asset->mask;

    lv_obj_t *path = lv_image_create(map);
    asset->object = path;
    space_eclipse_path_asset = asset;
    lv_image_set_src(path, &asset->image);
    lv_obj_set_pos(path, left, top);
    lv_obj_set_style_image_recolor(
        path,
        lv_color_hex(0xffe34d),
        0);
    lv_obj_set_style_image_recolor_opa(path, LV_OPA_COVER, 0);
    lv_obj_remove_flag(
        path,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(
        path,
        eclipse_path_draw_event_cb,
        LV_EVENT_DELETE,
        asset);
    if (space_eclipse_terminator_active) {
        update_total_eclipse_path_progress(0.0f, false);
    }
}

static void draw_space_greenwich_meridian(lv_obj_t *map)
{
    app_config_t cfg;
    app_config_get(&cfg);
    const uint32_t color = cfg.accent_color == APP_ACCENT_RED
        ? 0x20bd45
        : 0xd52b3b;
    line(
        map,
        (int)ISS_MAP_CENTER_X,
        ISS_MAP_TOP,
        (int)ISS_MAP_CENTER_X,
        ISS_MAP_BOTTOM,
        lv_color_hex(color),
        2,
        (lv_opa_t)225);
}

static void draw_space_selected_city_marker(lv_obj_t *map)
{
    if (!space_city_marker_visible) {
        return;
    }
    app_config_t cfg;
    app_config_get(&cfg);
    const app_location_preset_t *preset =
        app_location_preset_for_config(&cfg);
    const char *latitude_text = cfg.location_lat;
    const char *longitude_text = cfg.location_lon;
    if (preset != NULL) {
        latitude_text = preset->lat;
        longitude_text = preset->lon;
    }
    space_world_point(
        strtof(latitude_text, NULL),
        strtof(longitude_text, NULL),
        &space_city_marker_x,
        &space_city_marker_y);

    space_city_marker_glow = dot(
        map,
        space_city_marker_x,
        space_city_marker_y,
        26,
        lv_color_hex(0xffc34b),
        (lv_opa_t)65);
    lv_obj_set_style_shadow_color(
        space_city_marker_glow,
        lv_color_hex(0xffb52f),
        0);
    lv_obj_set_style_shadow_width(
        space_city_marker_glow,
        28,
        0);
    lv_obj_set_style_shadow_opa(
        space_city_marker_glow,
        (lv_opa_t)100,
        0);
    space_city_marker = dot(
        map,
        space_city_marker_x,
        space_city_marker_y,
        10,
        lv_color_hex(0xffffff),
        LV_OPA_COVER);
    lv_obj_set_style_shadow_color(
        space_city_marker,
        lv_color_hex(0xffd558),
        0);
    lv_obj_set_style_shadow_width(
        space_city_marker,
        14,
        0);
    lv_obj_set_style_shadow_opa(
        space_city_marker,
        (lv_opa_t)190,
        0);

    lv_obj_t *city = label(
        map,
        cfg.location_key,
        &lv_font_montserrat_12,
        lv_color_hex(0xffffff),
        LV_ALIGN_TOP_LEFT,
        space_city_marker_x + 8,
        space_city_marker_y - 13);
    lv_obj_set_style_text_letter_space(city, 2, 0);
}

static lv_obj_t *draw_iss_marker(lv_obj_t *map)
{
    lv_obj_t *marker = lv_obj_create(map);
    lv_obj_remove_style_all(marker);
    lv_obj_set_size(
        marker,
        ISS_MARKER_SIZE,
        ISS_MARKER_SIZE);
    lv_obj_set_style_bg_opa(marker, LV_OPA_TRANSP, 0);
    space_symbol_ring(
        marker,
        ISS_MARKER_SIZE / 2,
        ISS_MARKER_SIZE / 2,
        48,
        lv_color_hex(radar_palette_active->main),
        2,
        (lv_opa_t)235);
    const lv_image_dsc_t *descriptor =
        space_asset_descriptor(
            SPACE_ASSET_ISS,
            "iss");
    if (descriptor != NULL) {
        lv_obj_t *image = lv_image_create(marker);
        lv_image_set_src(image, descriptor);
        lv_image_set_scale(image, 112);
        lv_image_set_antialias(image, true);
        lv_obj_set_pos(
            image,
            ISS_MARKER_SIZE / 2 -
                (int)descriptor->header.w / 2,
            ISS_MARKER_SIZE / 2 -
                (int)descriptor->header.h / 2);
        lv_obj_set_style_image_recolor(
            image,
            lv_color_hex(0xffffff),
            0);
        lv_obj_set_style_image_recolor_opa(
            image,
            LV_OPA_COVER,
            0);
    }
    lv_obj_remove_flag(
        marker,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return marker;
}

static void draw_iss_metric(
    lv_obj_t *parent,
    const lv_image_dsc_t *icon,
    int center_x,
    int top_y,
    const char *value,
    const char *unit)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, icon);
    lv_obj_set_pos(
        image,
        center_x - (int)icon->header.w / 2,
        top_y);
    lv_image_set_antialias(image, true);
    lv_obj_set_style_image_recolor(
        image,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(
        image,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(
        image,
        (lv_opa_t)235,
        0);

    lv_obj_t *value_label = lv_label_create(parent);
    lv_label_set_text(value_label, value);
    lv_obj_set_pos(
        value_label,
        center_x - 44,
        top_y + 47);
    lv_obj_set_width(value_label, 88);
    lv_obj_set_style_text_font(
        value_label,
        &lv_font_montserrat_16,
        0);
    lv_obj_set_style_text_color(
        value_label,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_text_align(
        value_label,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_set_style_text_letter_space(
        value_label,
        1,
        0);

    lv_obj_t *unit_label = lv_label_create(parent);
    lv_label_set_text(unit_label, unit);
    lv_obj_set_pos(
        unit_label,
        center_x - 36,
        top_y + 67);
    lv_obj_set_width(unit_label, 72);
    lv_obj_set_style_text_font(
        unit_label,
        &lv_font_montserrat_12,
        0);
    lv_obj_set_style_text_color(
        unit_label,
        lv_color_hex(0xb9c0c2),
        0);
    lv_obj_set_style_text_align(
        unit_label,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_set_style_text_letter_space(
        unit_label,
        1,
        0);
}

static void space_iss_flags_opacity_animation(
    void *object,
    int32_t opacity)
{
    lv_obj_set_style_image_opa(
        (lv_obj_t *)object,
        (lv_opa_t)opacity,
        0);
}

static void start_space_iss_flags_fade_in(
    lv_obj_t *image,
    lv_opa_t initial_opacity,
    uint32_t remaining_ms)
{
    if (image == NULL) {
        return;
    }
    lv_obj_set_style_image_opa(
        image,
        initial_opacity,
        0);
    if (remaining_ms == 0) {
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, image);
    lv_anim_set_exec_cb(
        &animation,
        space_iss_flags_opacity_animation);
    lv_anim_set_values(
        &animation,
        initial_opacity,
        ISS_FLAGS_FINAL_OPACITY);
    lv_anim_set_duration(&animation, remaining_ms);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_start(&animation);
}

static void reset_space_map_overlay_references(void)
{
    space_iss_marker = NULL;
    space_city_marker = NULL;
    space_city_marker_glow = NULL;
    space_eclipse_time_label = NULL;
    space_eclipse_path_asset = NULL;
    space_iss_night_image_object = NULL;
    space_iss_terminator_image_object = NULL;
    space_eclipse_motion_image_object = NULL;
    space_eclipse_motion_dirty_count = 0;
    space_eclipse_shade_step = -1;
    for (int index = 0;
         index < ECLIPSE_TERMINATOR_MAX_GROUPS;
         index++) {
        space_eclipse_terminator_groups[index].point_count = 0;
    }
}

static void render_space_map_overlay(lv_obj_t *map)
{
    if (map == NULL) {
        return;
    }
    app_config_t cfg;
    app_config_get(&cfg);
    static const char *const iss_assets[] = {
        "world",
        "iss",
        "nasa",
        "roscosm",
        "iss_flags",
    };
    static const char *const iss_assets_without_flags[] = {
        "world",
        "iss",
        "nasa",
        "roscosm",
    };
    static const char *const eclipse_assets[] = {
        "world",
    };
    if (space_eclipse_visible) {
        trim_space_asset_cache(
            eclipse_assets,
            sizeof(eclipse_assets) /
                sizeof(eclipse_assets[0]));
    } else {
        trim_space_asset_cache(
            cfg.iss_flags_enabled
                ? iss_assets
                : iss_assets_without_flags,
            cfg.iss_flags_enabled
                ? sizeof(iss_assets) / sizeof(iss_assets[0])
                : sizeof(iss_assets_without_flags) /
                    sizeof(iss_assets_without_flags[0]));
    }
    const eclipse_definition_t *eclipse = NULL;
    if (space_eclipse_visible) {
        eclipse = next_total_eclipse();
        if (eclipse != NULL) {
            draw_eclipse_day_night(
                map,
                (time_t)eclipse->path_start_unix,
                (time_t)eclipse->path_end_unix);
        }
    } else {
        draw_iss_day_night(map);
    }
    draw_space_greenwich_meridian(map);

    const app_data_status_t *data = ui_data_status();
    if (space_eclipse_visible) {
        draw_total_eclipse_path(map, eclipse);
        draw_eclipse_motion_overlay(map);
    } else {
        if (data != NULL && data->iss_valid) {
            draw_iss_track(map, data);
            space_iss_marker = draw_iss_marker(map);
            update_iss_marker_from_data(data);
        } else {
            label(
                map,
                "ACQUIRING ISS",
                &lv_font_montserrat_14,
                lv_color_hex(0x8b999d),
                LV_ALIGN_CENTER,
                0,
                0);
        }
    }

    char speed[32];
    char altitude[32];
    if (data != NULL && data->iss_valid) {
        snprintf(
            speed,
            sizeof(speed),
            "%.0f",
            (double)data->iss_velocity_kph);
        snprintf(
            altitude,
            sizeof(altitude),
            "%.0f",
            (double)data->iss_altitude_km);
    } else {
        snprintf(speed, sizeof(speed), "-");
        snprintf(altitude, sizeof(altitude), "-");
    }

    lv_obj_t *footer = lv_obj_create(map);
    lv_obj_remove_style_all(footer);
    lv_obj_set_pos(footer, 0, 338);
    lv_obj_set_size(footer, SCREEN_SIZE, 128);
    lv_obj_set_style_bg_color(
        footer,
        lv_color_hex(0x000000),
        0);
    lv_obj_set_style_bg_opa(
        footer,
        LV_OPA_TRANSP,
        0);
    lv_obj_set_style_bg_grad_color(
        footer,
        lv_color_hex(0x000000),
        0);
    lv_obj_set_style_bg_grad_opa(
        footer,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_bg_grad_dir(
        footer,
        LV_GRAD_DIR_VER,
        0);
    lv_obj_remove_flag(
        footer,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    if (space_eclipse_visible) {
        label(
            map,
            "NEXT TOTAL SOLAR ECLIPSE",
            &lv_font_montserrat_12,
            lv_color_hex(0x8b9294),
            LV_ALIGN_TOP_MID,
            0,
            365);
        label(
            map,
            eclipse != NULL
                ? eclipse->date_label
                : "-",
            &lv_font_montserrat_24,
            lv_color_hex(0xffffff),
            LV_ALIGN_TOP_MID,
            0,
            389);
    } else {
        if (cfg.iss_flags_enabled) {
            lv_obj_t *flags = draw_space_asset_on(
                map,
                SPACE_ASSET_ISS_FLAG_WEDGE,
                "iss_flags",
                CX,
                399,
                LV_SCALE_NONE);
            if (flags != NULL &&
                space_iss_flags_fade_started_us > 0) {
                const uint32_t fade_duration_ms = 720;
                const int64_t elapsed_us =
                    esp_timer_get_time() -
                    space_iss_flags_fade_started_us;
                const uint32_t elapsed_ms =
                    elapsed_us > 0
                        ? (uint32_t)(elapsed_us / 1000)
                        : 0;
                if (elapsed_ms < fade_duration_ms) {
                    const lv_opa_t opacity =
                        (lv_opa_t)(
                            ((uint64_t)elapsed_ms *
                             ISS_FLAGS_FINAL_OPACITY) /
                            fade_duration_ms);
                    start_space_iss_flags_fade_in(
                        flags,
                        opacity,
                        fade_duration_ms - elapsed_ms);
                } else {
                    space_iss_flags_fade_started_us = 0;
                    lv_obj_set_style_image_opa(
                        flags,
                        ISS_FLAGS_FINAL_OPACITY,
                        0);
                }
            } else if (flags != NULL) {
                lv_obj_set_style_image_opa(
                    flags,
                    ISS_FLAGS_FINAL_OPACITY,
                    0);
            }
        } else {
            space_iss_flags_fade_started_us = 0;
        }
        const int metric_top = cfg.iss_flags_enabled ? 340 : 352;
        draw_iss_metric(
            map,
            &airspace_detail_metric_speed,
            190,
            metric_top,
            speed,
            "KM/H");
        draw_iss_metric(
            map,
            &airspace_detail_metric_altitude,
            276,
            metric_top,
            altitude,
            "KM");

        draw_space_asset_on(
            map,
            SPACE_ASSET_NASA,
            "nasa",
            181,
            50,
            LV_SCALE_NONE);
        draw_space_asset_on(
            map,
            SPACE_ASSET_ROSCOSMOS,
            "roscosm",
            285,
            50,
            LV_SCALE_NONE);
    }
    draw_space_selected_city_marker(map);
    if (space_eclipse_visible &&
        space_eclipse_terminator_active) {
        update_eclipse_terminator_animation();
    }
}

static void render_iss_globe(void)
{
    draw_shell();
    lv_obj_t *world_image = draw_space_asset(
        SPACE_ASSET_WORLD,
        "world",
        CX,
        CY,
        LV_SCALE_NONE);
    if (world_image == NULL) {
        card(
            screen_root,
            0,
            0,
            SCREEN_SIZE,
            SCREEN_SIZE,
            lv_color_hex(0x000000),
            LV_OPA_COVER,
            lv_color_hex(0x000000),
            LV_OPA_TRANSP,
            0);
    }

    space_map_overlay_layer = lv_obj_create(screen_root);
    if (space_map_overlay_layer == NULL) {
        return;
    }
    lv_obj_remove_style_all(space_map_overlay_layer);
    lv_obj_set_pos(space_map_overlay_layer, 0, 0);
    lv_obj_set_size(
        space_map_overlay_layer,
        SCREEN_SIZE,
        SCREEN_SIZE);
    lv_obj_remove_flag(
        space_map_overlay_layer,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    reset_space_map_overlay_references();
    render_space_map_overlay(space_map_overlay_layer);
}

static void space_map_overlay_opacity_animation(
    void *object,
    int32_t opacity)
{
    lv_obj_t *overlay = (lv_obj_t *)object;
    if (overlay != NULL && lv_obj_is_valid(overlay)) {
        lv_obj_set_style_opa(
            overlay,
            (lv_opa_t)opacity,
            0);
    }
}

static void space_map_overlay_fade_in_completed(
    lv_anim_t *animation)
{
    (void)animation;
    space_map_overlay_transition_active = false;
}

static void space_map_overlay_fade_out_completed(
    lv_anim_t *animation)
{
    (void)animation;
    if (current_screen != SCREEN_SPACE ||
        !space_iss_visible ||
        space_map_overlay_layer == NULL ||
        !lv_obj_is_valid(space_map_overlay_layer)) {
        space_map_overlay_transition_active = false;
        return;
    }

    lv_obj_clean(space_map_overlay_layer);
    reset_space_map_overlay_references();
    render_space_map_overlay(space_map_overlay_layer);
    lv_obj_set_style_opa(
        space_map_overlay_layer,
        LV_OPA_TRANSP,
        0);
    render_requested = false;

    lv_anim_t fade_in;
    lv_anim_init(&fade_in);
    lv_anim_set_var(&fade_in, space_map_overlay_layer);
    lv_anim_set_exec_cb(
        &fade_in,
        space_map_overlay_opacity_animation);
    lv_anim_set_values(
        &fade_in,
        LV_OPA_TRANSP,
        LV_OPA_COVER);
    lv_anim_set_duration(
        &fade_in,
        SPACE_MAP_OVERLAY_FADE_MS);
    lv_anim_set_path_cb(
        &fade_in,
        lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(
        &fade_in,
        space_map_overlay_fade_in_completed);
    lv_anim_start(&fade_in);
}

static bool begin_space_map_overlay_transition(void)
{
    if (space_map_overlay_transition_active ||
        current_screen != SCREEN_SPACE ||
        space_map_overlay_layer == NULL ||
        !lv_obj_is_valid(space_map_overlay_layer)) {
        return false;
    }

    space_map_overlay_transition_active = true;
    lv_anim_delete(
        space_map_overlay_layer,
        space_map_overlay_opacity_animation);
    lv_anim_t fade_out;
    lv_anim_init(&fade_out);
    lv_anim_set_var(&fade_out, space_map_overlay_layer);
    lv_anim_set_exec_cb(
        &fade_out,
        space_map_overlay_opacity_animation);
    lv_anim_set_values(
        &fade_out,
        LV_OPA_COVER,
        LV_OPA_TRANSP);
    lv_anim_set_duration(
        &fade_out,
        SPACE_MAP_OVERLAY_FADE_MS);
    lv_anim_set_path_cb(
        &fade_out,
        lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(
        &fade_out,
        space_map_overlay_fade_out_completed);
    lv_anim_start(&fade_out);
    return true;
}

static bool refresh_space_map_overlay(void)
{
    if (current_screen != SCREEN_SPACE ||
        !space_iss_visible ||
        space_map_overlay_transition_active ||
        space_map_overlay_layer == NULL ||
        !lv_obj_is_valid(space_map_overlay_layer)) {
        return false;
    }
    lv_obj_clean(space_map_overlay_layer);
    reset_space_map_overlay_references();
    render_space_map_overlay(space_map_overlay_layer);
    render_requested = false;
    return true;
}

#define SPACE_MOON_PHASE_SIZE 56
#define SPACE_MOON_PHASE_PIXELS \
    (SPACE_MOON_PHASE_SIZE * SPACE_MOON_PHASE_SIZE)
#define SPACE_MOON_PHASE_BYTES \
    (SPACE_MOON_PHASE_PIXELS * 3)
#define SPACE_MOON_STRIP_COUNT 7
#define SPACE_MOON_STRIP_SIZE 34
#define SPACE_MOON_STRIP_PIXELS \
    (SPACE_MOON_STRIP_SIZE * SPACE_MOON_STRIP_SIZE)
#define SPACE_MOON_STRIP_IMAGE_BYTES \
    (SPACE_MOON_STRIP_PIXELS * 3)
#define SPACE_MOON_STRIP_BYTES \
    (SPACE_MOON_STRIP_COUNT * SPACE_MOON_STRIP_IMAGE_BYTES)

typedef struct {
    bool valid;
    float phase;
    int illuminated_percent;
    int distance_km;
    const char *name;
} space_moon_state_t;

static space_moon_state_t current_space_moon_state(void)
{
    static const char *phase_names[] = {
        "NEW MOON",
        "WAXING CRESCENT",
        "FIRST QUARTER",
        "WAXING GIBBOUS",
        "FULL MOON",
        "WANING GIBBOUS",
        "LAST QUARTER",
        "WANING CRESCENT",
    };
    const time_t now = time(NULL);
    if (!space_ephemeris_time_is_valid(now)) {
        return (space_moon_state_t) {
            .valid = false,
            .name = "WAITING FOR TIME",
        };
    }
    const double julian_date =
        2440587.5 + (double)now / 86400.0;
    const double synodic_month = 29.53058867;
    double moon_age =
        fmod(julian_date - 2451550.1, synodic_month);
    if (moon_age < 0.0) {
        moon_age += synodic_month;
    }
    const double phase = moon_age / synodic_month;
    const double illuminated =
        (1.0 - cos(phase * 2.0 * M_PI)) * 0.5;
    double mean_anomaly =
        fmod(
            134.963 +
                13.064993 *
                    (julian_date - 2451545.0),
            360.0);
    if (mean_anomaly < 0.0) {
        mean_anomaly += 360.0;
    }
    const int phase_index =
        ((int)floor(phase * 8.0 + 0.5)) % 8;
    return (space_moon_state_t) {
        .valid = true,
        .phase = (float)phase,
        .illuminated_percent =
            (int)lround(illuminated * 100.0),
        .distance_km =
            (int)lround(
                385000.56 -
                20905.0 *
                    cos(mean_anomaly * DEG_TO_RAD)),
        .name = phase_names[phase_index],
    };
}

static void render_space_moon_phase_pixels(
    uint8_t *buffer,
    int size,
    float phase,
    bool orient_toward_light,
    float light_angle)
{
    if (buffer == NULL || size <= 0) {
        return;
    }
    const float center =
        ((float)size - 1.0f) * 0.5f;
    const float radius = center - 0.8f;
    const float theta = phase * 2.0f * (float)M_PI;
    float sun_x = sinf(theta);
    float sun_y = 0.0f;
    const float sun_z = -cosf(theta);
    if (orient_toward_light) {
        const float projected_light =
            fabsf(sinf(theta));
        sun_x =
            cosf(light_angle) * projected_light;
        sun_y =
            sinf(light_angle) * projected_light;
    }
    const size_t pixels = (size_t)size * (size_t)size;
    uint8_t *alpha = buffer + pixels * 2U;
    size_t moon_bytes = 0;
    uint16_t moon_width = 0;
    uint16_t moon_height = 0;
    bool moon_has_alpha = false;
    const uint8_t *moon_texture =
        app_remote_get_space_asset(
            "moon",
            &moon_bytes,
            &moon_width,
            &moon_height,
            &moon_has_alpha,
            NULL);
    const size_t moon_pixels =
        (size_t)moon_width *
        (size_t)moon_height;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            const float normalized_x =
                ((float)x - center) / radius;
            const float normalized_y =
                ((float)y - center) / radius;
            const float radial_squared =
                normalized_x * normalized_x +
                normalized_y * normalized_y;
            const size_t pixel =
                (size_t)y * (size_t)size +
                (size_t)x;
            if (radial_squared > 1.0f) {
                buffer[pixel * 2] = 0;
                buffer[pixel * 2 + 1] = 0;
                alpha[pixel] = 0;
                continue;
            }

            const float surface_z =
                sqrtf(1.0f - radial_squared);
            const float light =
                normalized_x * sun_x +
                normalized_y * sun_y +
                surface_z * sun_z;
            float light_mix = (light + 0.035f) / 0.07f;
            if (light_mix < 0.0f) {
                light_mix = 0.0f;
            } else if (light_mix > 1.0f) {
                light_mix = 1.0f;
            }
            uint8_t source_red = 195;
            uint8_t source_green = 192;
            uint8_t source_blue = 185;
            uint8_t source_alpha = 255;
            if (moon_texture != NULL &&
                moon_has_alpha &&
                moon_width > 0 &&
                moon_height > 0 &&
                moon_bytes >= moon_pixels * 3U) {
                int source_x =
                    (int)lroundf(
                        (normalized_x * 0.5f + 0.5f) *
                        (float)(moon_width - 1));
                int source_y =
                    (int)lroundf(
                        (normalized_y * 0.5f + 0.5f) *
                        (float)(moon_height - 1));
                if (source_x < 0) {
                    source_x = 0;
                } else if (source_x >= moon_width) {
                    source_x = moon_width - 1;
                }
                if (source_y < 0) {
                    source_y = 0;
                } else if (source_y >= moon_height) {
                    source_y = moon_height - 1;
                }
                const size_t source_pixel =
                    (size_t)source_y *
                        (size_t)moon_width +
                    (size_t)source_x;
                const uint16_t source_rgb565 =
                    (uint16_t)moon_texture[
                        source_pixel * 2] |
                    ((uint16_t)moon_texture[
                         source_pixel * 2 + 1]
                     << 8);
                source_red =
                    (uint8_t)(
                        ((source_rgb565 >> 11) & 0x1f) *
                        255 /
                        31);
                source_green =
                    (uint8_t)(
                        ((source_rgb565 >> 5) & 0x3f) *
                        255 /
                        63);
                source_blue =
                    (uint8_t)(
                        (source_rgb565 & 0x1f) *
                        255 /
                        31);
                source_alpha =
                    moon_texture[
                        moon_pixels * 2U +
                        source_pixel];
            }
            const float luminance =
                0.025f +
                light_mix *
                    (0.72f +
                     surface_z * 0.25f);
            const uint8_t red =
                (uint8_t)lroundf(
                    (float)source_red *
                    luminance);
            const uint8_t green =
                (uint8_t)lroundf(
                    (float)source_green *
                    luminance);
            const uint8_t blue =
                (uint8_t)lroundf(
                    (float)source_blue *
                    luminance);
            const uint16_t rgb565 =
                (uint16_t)(
                    ((uint16_t)(red >> 3) << 11) |
                    ((uint16_t)(green >> 2) << 5) |
                    (uint16_t)(blue >> 3));
            buffer[pixel * 2] =
                (uint8_t)(rgb565 & 0xff);
            buffer[pixel * 2 + 1] =
                (uint8_t)(rgb565 >> 8);
            const float edge =
                (1.0f - sqrtf(radial_squared)) * radius;
            const float edge_alpha =
                edge >= 1.0f
                    ? 1.0f
                    : fmaxf(0.0f, edge);
            alpha[pixel] =
                (uint8_t)lroundf(
                    (float)source_alpha *
                    edge_alpha);
        }
    }
}

static const lv_image_dsc_t *render_space_moon_phase(
    float phase,
    float light_angle)
{
    if (space_moon_phase_buffer == NULL) {
        space_moon_phase_buffer =
            (uint8_t *)heap_caps_malloc(
                SPACE_MOON_PHASE_BYTES,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (space_moon_phase_buffer == NULL) {
        return NULL;
    }
    render_space_moon_phase_pixels(
        space_moon_phase_buffer,
        SPACE_MOON_PHASE_SIZE,
        phase,
        true,
        light_angle);
    memset(
        &space_moon_phase_image,
        0,
        sizeof(space_moon_phase_image));
    space_moon_phase_image.header.magic =
        LV_IMAGE_HEADER_MAGIC;
    space_moon_phase_image.header.cf =
        LV_COLOR_FORMAT_RGB565A8;
    space_moon_phase_image.header.w =
        SPACE_MOON_PHASE_SIZE;
    space_moon_phase_image.header.h =
        SPACE_MOON_PHASE_SIZE;
    space_moon_phase_image.header.stride =
        SPACE_MOON_PHASE_SIZE * 2;
    space_moon_phase_image.data_size =
        SPACE_MOON_PHASE_BYTES;
    space_moon_phase_image.data =
        space_moon_phase_buffer;
    return &space_moon_phase_image;
}

static bool render_space_moon_strip(void)
{
    if (space_moon_strip_buffer == NULL) {
        space_moon_strip_buffer =
            (uint8_t *)heap_caps_malloc(
                SPACE_MOON_STRIP_BYTES,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (space_moon_strip_buffer == NULL) {
        return false;
    }
    for (int index = 0;
         index < SPACE_MOON_STRIP_COUNT;
         index++) {
        uint8_t *buffer =
            space_moon_strip_buffer +
            (size_t)index *
                SPACE_MOON_STRIP_IMAGE_BYTES;
        render_space_moon_phase_pixels(
            buffer,
            SPACE_MOON_STRIP_SIZE,
            (float)(index + 1) / 8.0f,
            false,
            0.0f);
        lv_image_dsc_t *descriptor =
            &space_moon_strip_images[index];
        memset(descriptor, 0, sizeof(*descriptor));
        descriptor->header.magic =
            LV_IMAGE_HEADER_MAGIC;
        descriptor->header.cf =
            LV_COLOR_FORMAT_RGB565A8;
        descriptor->header.w =
            SPACE_MOON_STRIP_SIZE;
        descriptor->header.h =
            SPACE_MOON_STRIP_SIZE;
        descriptor->header.stride =
            SPACE_MOON_STRIP_SIZE * 2;
        descriptor->data_size =
            SPACE_MOON_STRIP_IMAGE_BYTES;
        descriptor->data = buffer;
    }
    return true;
}

static void render_earth_moon(void)
{
    draw_shell();
    static const char *const moon_assets[] = {
        "sun",
        "earth",
        "moon",
    };
    trim_space_asset_cache(
        moon_assets,
        sizeof(moon_assets) / sizeof(moon_assets[0]));
    draw_space_stars();
    const space_moon_state_t moon =
        current_space_moon_state();
    const time_t now = time(NULL);
    const float earth_angle = moon.valid
        ? space_planet_angle_for_time(2, now)
        : 0.0f;
    const float earth_progress =
        earth_angle /
        (2.0f * (float)M_PI);
    const int sun_y = 5;
    const int earth_path_center_y = 10;
    const int earth_path_radius = 194;
    const float earth_path_start_degrees = 141.0f;
    const float earth_path_end_degrees = 39.0f;

    lv_obj_t *earth_path = lv_arc_create(screen_root);
    lv_obj_set_size(
        earth_path,
        earth_path_radius * 2,
        earth_path_radius * 2);
    lv_obj_set_pos(
        earth_path,
        CX - earth_path_radius,
        earth_path_center_y - earth_path_radius);
    lv_arc_set_bg_angles(earth_path, 39, 141);
    lv_arc_set_angles(earth_path, 39, 141);
    lv_obj_remove_style(
        earth_path,
        NULL,
        LV_PART_KNOB);
    lv_obj_set_style_arc_width(
        earth_path,
        2,
        LV_PART_MAIN);
    lv_obj_set_style_arc_color(
        earth_path,
        lv_color_hex(0x747b79),
        LV_PART_MAIN);
    lv_obj_set_style_arc_opa(
        earth_path,
        (lv_opa_t)185,
        LV_PART_MAIN);
    lv_obj_set_style_arc_width(
        earth_path,
        2,
        LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(
        earth_path,
        lv_color_hex(0x747b79),
        LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(
        earth_path,
        (lv_opa_t)185,
        LV_PART_INDICATOR);
    lv_obj_remove_flag(
        earth_path,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sun_glow = soft_blob(
        screen_root,
        CX,
        sun_y,
        80,
        lv_color_hex(0xff7c18),
        (lv_opa_t)34);
    lv_obj_set_style_shadow_color(
        sun_glow,
        lv_color_hex(0xff8b22),
        0);
    lv_obj_set_style_shadow_width(
        sun_glow,
        13,
        0);
    lv_obj_set_style_shadow_opa(
        sun_glow,
        (lv_opa_t)80,
        0);
    if (draw_space_asset(
            SPACE_ASSET_SUN,
            "sun",
            CX,
            sun_y,
            168) == NULL) {
        dot(
            screen_root,
            CX,
            sun_y,
            52,
            lv_color_hex(0xffa225),
            LV_OPA_COVER);
    }

    const float earth_path_degrees =
        earth_path_start_degrees +
        (earth_path_end_degrees -
         earth_path_start_degrees) *
            earth_progress;
    const float earth_path_radians =
        earth_path_degrees * DEG_TO_RAD;
    const int earth_x =
        (int)lroundf(
            (float)CX +
            cosf(earth_path_radians) *
                (float)earth_path_radius);
    const int earth_y =
        (int)lroundf(
            (float)earth_path_center_y +
            sinf(earth_path_radians) *
                (float)earth_path_radius);
    lv_obj_t *earth_glow = soft_blob(
        screen_root,
        earth_x,
        earth_y,
        70,
        lv_color_hex(0x3da5df),
        (lv_opa_t)42);
    lv_obj_set_style_shadow_color(
        earth_glow,
        lv_color_hex(0x58bfff),
        0);
    lv_obj_set_style_shadow_width(
        earth_glow,
        8,
        0);
    lv_obj_set_style_shadow_opa(
        earth_glow,
        (lv_opa_t)100,
        0);
    if (draw_space_asset(
            SPACE_ASSET_EARTH,
            "earth",
            earth_x,
            earth_y,
            205) == NULL) {
        dot(
            screen_root,
            earth_x,
            earth_y,
            64,
            lv_color_hex(0x247aa5),
            LV_OPA_COVER);
    }

    const int moon_orbit_diameter = 120;
    space_symbol_ring(
        screen_root,
        earth_x,
        earth_y,
        moon_orbit_diameter,
        lv_color_hex(0x9aa5a4),
        1,
        LV_OPA_60);
    const float sun_direction =
        atan2f(
            (float)sun_y - (float)earth_y,
            (float)CX - (float)earth_x);
    const float moon_angle =
        sun_direction +
        moon.phase *
            2.0f *
            (float)M_PI;
    const int moon_x =
        earth_x +
        (int)lroundf(
            cosf(moon_angle) *
            moon_orbit_diameter *
            0.5f);
    const int moon_y =
        earth_y +
        (int)lroundf(
            sinf(moon_angle) *
            moon_orbit_diameter *
            0.5f);
    dot(
        screen_root,
        moon_x,
        moon_y,
        34,
        lv_color_hex(0xe4e1d8),
        LV_OPA_10);
    const float moon_light_angle =
        atan2f(
            (float)sun_y - (float)moon_y,
            (float)CX - (float)moon_x);
    const lv_image_dsc_t *phase =
        render_space_moon_phase(
            moon.phase,
            moon_light_angle);
    if (phase != NULL) {
        lv_obj_t *image = lv_image_create(screen_root);
        lv_image_set_src(image, phase);
        lv_image_set_scale(image, 137);
        lv_image_set_antialias(image, true);
        lv_obj_set_pos(
            image,
            moon_x - SPACE_MOON_PHASE_SIZE / 2,
            moon_y - SPACE_MOON_PHASE_SIZE / 2);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    }

    ensure_weather_gradients();
    draw_weather_gradient_bar(
        54,
        276,
        358,
        1,
        &weather_divider_gradient);

    const int current_phase_sector =
        ((int)floorf(
             moon.phase * 8.0f + 0.5f)) %
        8;
    const int current_phase_index =
        current_phase_sector > 0
            ? current_phase_sector - 1
            : -1;
    if (render_space_moon_strip()) {
        for (int index = 0;
             index < SPACE_MOON_STRIP_COUNT;
             index++) {
            const int center_x =
                CX +
                (index -
                 SPACE_MOON_STRIP_COUNT / 2) *
                    52;
            if (index == current_phase_index) {
                soft_blob(
                    screen_root,
                    center_x,
                    326,
                    50,
                    lv_color_hex(
                        radar_palette_active->main),
                    LV_OPA_20);
                space_symbol_ring(
                    screen_root,
                    center_x,
                    326,
                    44,
                    lv_color_hex(
                        radar_palette_active->main),
                    2,
                    (lv_opa_t)215);
            }
            lv_obj_t *phase_image =
                lv_image_create(screen_root);
            lv_image_set_src(
                phase_image,
                &space_moon_strip_images[index]);
            lv_obj_set_pos(
                phase_image,
                center_x -
                    SPACE_MOON_STRIP_SIZE / 2,
                326 -
                    SPACE_MOON_STRIP_SIZE / 2);
            lv_obj_remove_flag(
                phase_image,
                LV_OBJ_FLAG_CLICKABLE |
                    LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    label(
        screen_root,
        moon.name,
        &lv_font_montserrat_18,
        lv_color_hex(0xe9eeeb),
        LV_ALIGN_BOTTOM_MID,
        0,
        -50);
    char details[48];
    snprintf(
        details,
        sizeof(details),
        "%d%% ILLUMINATED  •  %d KM",
        moon.illuminated_percent,
        moon.distance_km);
    label(
        screen_root,
        details,
        &lv_font_montserrat_12,
        lv_color_hex(0x788580),
        LV_ALIGN_BOTTOM_MID,
        0,
        -27);
}

static void render_space(void)
{
    if (space_moon_visible) {
        render_earth_moon();
    } else if (space_iss_visible) {
        render_iss_globe();
    } else {
        render_solar_system();
    }
}

static bool launch_has_precise_net(
    const app_remote_status_t *remote)
{
    if (remote == NULL ||
        remote->launch_net_unix <= 0) {
        return false;
    }
    const char *precision = remote->launch_precision;
    return strcasecmp(precision, "Second") == 0 ||
           strcasecmp(precision, "Minute") == 0 ||
           strcasecmp(precision, "Hour") == 0;
}

static void launch_format_target_date(
    const app_remote_status_t *remote,
    char *buffer,
    size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    if (remote == NULL ||
        remote->launch_net_unix <= 0) {
        snprintf(buffer, buffer_size, "-");
        return;
    }
    time_t net = (time_t)remote->launch_net_unix;
    struct tm utc = {0};
    gmtime_r(&net, &utc);
    static const char *months[] = {
        "JAN", "FEB", "MAR", "APR",
        "MAY", "JUN", "JUL", "AUG",
        "SEP", "OCT", "NOV", "DEC",
    };
    const char *month =
        utc.tm_mon >= 0 && utc.tm_mon < 12
            ? months[utc.tm_mon]
            : "---";
    if (launch_has_precise_net(remote)) {
        snprintf(
            buffer,
            buffer_size,
            "%02d %s  %02d:%02d UTC",
            utc.tm_mday,
            month,
            utc.tm_hour,
            utc.tm_min);
    } else {
        snprintf(
            buffer,
            buffer_size,
            "EXPECTED  %s %04d",
            month,
            utc.tm_year + 1900);
    }
}

static void launch_format_countdown(
    const app_remote_status_t *remote,
    time_t now,
    char *buffer,
    size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    if (!launch_has_precise_net(remote)) {
        launch_format_target_date(
            remote,
            buffer,
            buffer_size);
        return;
    }
    int64_t seconds =
        remote->launch_net_unix - (int64_t)now;
    const bool elapsed = seconds < 0;
    if (elapsed) {
        seconds = -seconds;
    }
    const int days = (int)(seconds / 86400);
    const int hours =
        (int)((seconds % 86400) / 3600);
    const int minutes =
        (int)((seconds % 3600) / 60);
    const int secs = (int)(seconds % 60);
    if (days > 0) {
        snprintf(
            buffer,
            buffer_size,
            "T%c %03dD %02dH",
            elapsed ? '+' : '-',
            days,
            hours);
    } else {
        snprintf(
            buffer,
            buffer_size,
            "T%c %02d:%02d:%02d",
            elapsed ? '+' : '-',
            hours,
            minutes,
            secs);
    }
}

static lv_color_t launch_status_color(
    const app_remote_status_t *remote)
{
    if (remote == NULL) {
        return lv_color_hex(0x8b9590);
    }
    const char *status =
        remote->launch_status_abbrev[0] != '\0'
            ? remote->launch_status_abbrev
            : remote->launch_status;
    if (strcasecmp(status, "GO") == 0 ||
        strcasecmp(status, "SUCCESS") == 0) {
        return lv_color_hex(0x36d66b);
    }
    if (strcasecmp(status, "HOLD") == 0 ||
        strcasecmp(status, "FAILURE") == 0 ||
        strcasecmp(status, "FAIL") == 0) {
        return lv_color_hex(0xff5b56);
    }
    return lv_color_hex(0xf0b846);
}

static void launch_draw_centered_text(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    lv_color_t color,
    int y,
    int width)
{
    lv_obj_t *value = label_box(
        parent,
        text != NULL && text[0] != '\0'
            ? text
            : "-",
        font,
        color,
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        width);
    lv_obj_set_pos(value, CX - width / 2, y);
    lv_obj_set_height(
        value,
        lv_font_get_line_height(font) + 2);
    lv_obj_set_style_text_align(
        value,
        LV_TEXT_ALIGN_CENTER,
        0);
}

static bool launch_text_contains(
    const char *text,
    const char *needle)
{
    if (text == NULL || needle == NULL ||
        needle[0] == '\0') {
        return false;
    }
    const size_t needle_length = strlen(needle);
    for (const char *cursor = text;
         *cursor != '\0';
         cursor++) {
        if (strncasecmp(
                cursor,
                needle,
                needle_length) == 0) {
            return true;
        }
    }
    return false;
}

static void launch_pad_alias(
    const app_remote_status_t *remote,
    char *buffer,
    size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    const char *pad =
        remote != NULL ? remote->launch_pad : NULL;
    if (pad == NULL || pad[0] == '\0') {
        snprintf(buffer, buffer_size, "-");
    } else if (launch_text_contains(pad, "39A")) {
        snprintf(buffer, buffer_size, "39A");
    } else if (launch_text_contains(pad, "39B")) {
        snprintf(buffer, buffer_size, "39B");
    } else if (launch_text_contains(pad, "SLC-40") ||
               launch_text_contains(pad, "COMPLEX 40")) {
        snprintf(buffer, buffer_size, "S40");
    } else if (launch_text_contains(pad, "31/6")) {
        snprintf(buffer, buffer_size, "31/6");
    } else if (launch_text_contains(pad, "200/39")) {
        snprintf(buffer, buffer_size, "200/39");
    } else if (launch_text_contains(pad, "ORBITAL") ||
               (remote != NULL &&
                strcasecmp(remote->launch_site, "starbase") == 0)) {
        snprintf(buffer, buffer_size, "OLM");
    } else if (strlen(pad) <= 7) {
        snprintf(buffer, buffer_size, "%s", pad);
    } else {
        size_t output = 0;
        bool at_word_start = true;
        for (size_t index = 0;
             pad[index] != '\0' &&
             output + 1 < buffer_size &&
             output < 4;
             index++) {
            const unsigned char character =
                (unsigned char)pad[index];
            if (isalnum(character) && at_word_start) {
                buffer[output++] =
                    (char)toupper(character);
                at_word_start = false;
            } else if (!isalnum(character)) {
                at_word_start = true;
            }
        }
        if (output == 0) {
            snprintf(buffer, buffer_size, "PAD");
        } else {
            buffer[output] = '\0';
        }
    }
}

static bool launch_split_countdown(
    const char *formatted,
    char prefix[3],
    const char **value)
{
    if (prefix == NULL || value == NULL) {
        return false;
    }
    prefix[0] = '\0';
    *value = formatted != NULL ? formatted : "-";
    if (formatted == NULL ||
        formatted[0] != 'T' ||
        (formatted[1] != '-' &&
         formatted[1] != '+')) {
        return false;
    }
    prefix[0] = 'T';
    prefix[1] = formatted[1];
    prefix[2] = '\0';
    *value = formatted + 2;
    while (**value == ' ') {
        (*value)++;
    }
    return true;
}

static void launch_set_countdown_text(
    const char *formatted)
{
    char prefix[3];
    const char *value = NULL;
    const bool precise = launch_split_countdown(
        formatted,
        prefix,
        &value);
    if (launch_countdown_prefix_label != NULL) {
        lv_label_set_text(
            launch_countdown_prefix_label,
            precise ? prefix : "");
    }
    if (launch_countdown_label != NULL) {
        lv_label_set_text(
            launch_countdown_label,
            precise ? value : formatted);
    }
}

static void launch_draw_countdown(
    lv_obj_t *parent,
    int y,
    const char *formatted)
{
    char prefix[3];
    const char *value = NULL;
    const bool precise = launch_split_countdown(
        formatted,
        prefix,
        &value);
    if (!precise) {
        launch_countdown_label = label_box(
            parent,
            formatted != NULL ? formatted : "-",
            &nasalization_expected_22,
            lv_color_hex(0xf4f6f5),
            LV_ALIGN_TOP_LEFT,
            0,
            0,
            386);
        lv_obj_set_pos(
            launch_countdown_label,
            40,
            y + 17);
        lv_obj_set_height(
            launch_countdown_label,
            lv_font_get_line_height(
                &nasalization_expected_22) + 2);
        lv_label_set_long_mode(
            launch_countdown_label,
            LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(
            launch_countdown_label,
            LV_TEXT_ALIGN_CENTER,
            0);
        return;
    }

    launch_countdown_prefix_label = label_box(
        parent,
        prefix,
        &nasalization_prefix_34,
        lv_color_hex(0xbfc6c2),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        60);
    lv_obj_set_pos(
        launch_countdown_prefix_label,
        83,
        y + 10);
    lv_obj_set_height(
        launch_countdown_prefix_label,
        lv_font_get_line_height(
            &nasalization_prefix_34) + 2);
    lv_label_set_long_mode(
        launch_countdown_prefix_label,
        LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(
        launch_countdown_prefix_label,
        LV_TEXT_ALIGN_RIGHT,
        0);
    lv_obj_set_style_transform_pivot_x(
        launch_countdown_prefix_label,
        30,
        0);
    lv_obj_set_style_transform_pivot_y(
        launch_countdown_prefix_label,
        lv_font_get_line_height(
            &nasalization_prefix_34) / 2,
        0);
    lv_obj_set_style_transform_scale(
        launch_countdown_prefix_label,
        166,
        0);

    launch_countdown_label = label_box(
        parent,
        value,
        &nasalization_countdown_50,
        lv_color_hex(0xf4f6f5),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        350);
    lv_obj_set_pos(
        launch_countdown_label,
        78,
        y + 6);
    lv_obj_set_height(
        launch_countdown_label,
        lv_font_get_line_height(
            &nasalization_countdown_50) + 2);
    lv_label_set_long_mode(
        launch_countdown_label,
        LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(
        launch_countdown_label,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_set_style_transform_pivot_x(
        launch_countdown_label,
        175,
        0);
    lv_obj_set_style_transform_pivot_y(
        launch_countdown_label,
        lv_font_get_line_height(
            &nasalization_countdown_50) / 2,
        0);
    lv_obj_set_style_transform_scale(
        launch_countdown_label,
        212,
        0);
}

static lv_obj_t *launch_draw_agency_logo(
    const app_remote_status_t *remote,
    int center_x,
    int center_y,
    uint32_t scale)
{
    if (remote != NULL &&
        strcasecmp(remote->launch_agency, "spacex") == 0) {
        return draw_space_asset(
            SPACE_ASSET_LAUNCH_SPACEX,
            "launch_spacex",
            center_x,
            center_y,
            scale);
    }
    if (remote != NULL &&
        strcasecmp(
            remote->launch_agency,
            "roscosmos") == 0) {
        return draw_space_asset(
            SPACE_ASSET_ROSCOSMOS,
            "roscosm",
            center_x,
            center_y,
            scale);
    }
    return draw_space_asset(
        SPACE_ASSET_NASA,
        "nasa",
        center_x,
        center_y,
        scale);
}

static const char *launch_agency_asset_name(
    const app_remote_status_t *remote)
{
    if (remote != NULL &&
        strcasecmp(remote->launch_agency, "spacex") == 0) {
        return "launch_spacex";
    }
    if (remote != NULL &&
        strcasecmp(remote->launch_agency, "roscosmos") == 0) {
        return "roscosm";
    }
    return "nasa";
}

static const char *launch_vehicle_asset_name(
    const app_remote_status_t *remote)
{
    if (remote != NULL &&
        strcasecmp(remote->launch_vehicle, "starship") == 0) {
        return "launch_starship";
    }
    if (remote != NULL &&
        strcasecmp(remote->launch_vehicle, "soyuz") == 0) {
        return "launch_soyuz";
    }
    if (remote != NULL &&
        strcasecmp(remote->launch_vehicle, "falcon9") == 0) {
        return "launch_falcon9";
    }
    return "launch_sls";
}

static lv_obj_t *launch_draw_vehicle(
    const app_remote_status_t *remote,
    int center_x,
    int center_y,
    uint32_t scale)
{
    if (remote != NULL &&
        strcasecmp(remote->launch_vehicle, "starship") == 0) {
        return draw_space_asset(
            SPACE_ASSET_LAUNCH_STARSHIP,
            "launch_starship",
            center_x,
            center_y,
            scale);
    }
    if (remote != NULL &&
        strcasecmp(remote->launch_vehicle, "soyuz") == 0) {
        return draw_space_asset(
            SPACE_ASSET_LAUNCH_SOYUZ,
            "launch_soyuz",
            center_x,
            center_y,
            scale);
    }
    if (remote != NULL &&
        strcasecmp(remote->launch_vehicle, "falcon9") == 0) {
        return draw_space_asset(
            SPACE_ASSET_LAUNCH_FALCON9,
            "launch_falcon9",
            center_x,
            center_y,
            scale);
    }
    return draw_space_asset(
        SPACE_ASSET_LAUNCH_SLS,
        "launch_sls",
        center_x,
        center_y,
        scale);
}

static void launch_draw_empty_state(void)
{
    draw_shell();
    trim_space_asset_cache(NULL, 0);
    for (int diameter = 132;
         diameter <= 404;
         diameter += 68) {
        lv_obj_t *orbit = ring(
            screen_root,
            diameter,
            lv_color_hex(0x54605b),
            1,
            (lv_opa_t)(48 - diameter / 16));
        lv_obj_set_y(orbit, -24);
    }
    launch_draw_centered_text(
        screen_root,
        "LAUNCH SCHEDULE",
        &lv_font_montserrat_20,
        lv_color_hex(0xe8eeeb),
        188,
        350);
    launch_draw_centered_text(
        screen_root,
        "SYNC FROM THE MAC COMPANION",
        &lv_font_montserrat_12,
        lv_color_hex(0x65716c),
        224,
        350);
    launch_draw_centered_text(
        screen_root,
        "-",
        &lv_font_montserrat_40,
        lv_color_hex(0x89938f),
        272,
        220);
}

static lv_obj_t *launch_arc(
    int diameter,
    int start_angle,
    int end_angle,
    lv_color_t color,
    int width,
    lv_opa_t opacity)
{
    lv_obj_t *arc = lv_arc_create(screen_root);
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(
        arc,
        start_angle,
        end_angle);
    lv_arc_set_angles(
        arc,
        start_angle,
        end_angle);
    lv_obj_remove_style(
        arc,
        NULL,
        LV_PART_KNOB);
    lv_obj_set_style_arc_width(
        arc,
        width,
        LV_PART_MAIN);
    lv_obj_set_style_arc_color(
        arc,
        color,
        LV_PART_MAIN);
    lv_obj_set_style_arc_opa(
        arc,
        opacity,
        LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(
        arc,
        true,
        LV_PART_MAIN);
    lv_obj_set_style_arc_width(
        arc,
        width,
        LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(
        arc,
        color,
        LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(
        arc,
        opacity,
        LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(
        arc,
        true,
        LV_PART_INDICATOR);
    lv_obj_remove_flag(
        arc,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    return arc;
}

static void launch_format_local_clock(
    const app_remote_status_t *remote,
    time_t now,
    char *buffer,
    size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    tracked_format_local_time(
        (int64_t)now,
        remote != NULL
            ? remote->launch_current_utc_offset_s
            : 0,
        buffer,
        buffer_size);
}

static void launch_draw_bottom_stat(
    const char *title,
    const char *value,
    int center_x)
{
    const int title_width = 94;
    const int value_width = 102;
    lv_obj_t *title_label = label_box(
        screen_root,
        title,
        &lv_font_montserrat_12,
        lv_color_hex(0x777f7b),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        title_width);
    lv_obj_set_pos(
        title_label,
        center_x - title_width / 2,
        393);
    lv_obj_set_style_text_align(
        title_label,
        LV_TEXT_ALIGN_CENTER,
        0);

    lv_obj_t *value_label = label_box(
        screen_root,
        value,
        &lv_font_montserrat_16,
        lv_color_hex(0xe8ecea),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        value_width);
    lv_obj_set_pos(
        value_label,
        center_x - value_width / 2,
        412);
    lv_obj_set_style_text_align(
        value_label,
        LV_TEXT_ALIGN_CENTER,
        0);
}

#define LAUNCH_GRADIENT_RING_SIZE 300
#define LAUNCH_GRADIENT_RING_SCALE LV_SCALE_NONE
#define LAUNCH_OUTER_GRADIENT_RING_SIZE 430

static void launch_prepare_gradient_radial_profile(void)
{
    if (launch_gradient_radial_profile_ready) {
        return;
    }
    for (int index = 0; index < 193; index++) {
        const float distance =
            (float)index / 16.0f;
        const float core =
            expf(
                -0.5f *
                (distance / 0.72f) *
                (distance / 0.72f));
        const float glow =
            0.25f *
            expf(
                -0.5f *
                (distance / 4.25f) *
                (distance / 4.25f));
        launch_gradient_radial_profile[index] =
            fminf(1.0f, core + glow);
    }
    launch_gradient_radial_profile_ready = true;
}

static bool launch_prepare_gradient_ring(void)
{
    if (launch_gradient_ring_buffer != NULL) {
        return true;
    }
    launch_gradient_ring_buffer =
        (uint8_t *)heap_caps_malloc(
            LAUNCH_GRADIENT_RING_SIZE *
                LAUNCH_GRADIENT_RING_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (launch_gradient_ring_buffer == NULL) {
        return false;
    }

    launch_prepare_gradient_radial_profile();

    const float center =
        ((float)LAUNCH_GRADIENT_RING_SIZE - 1.0f) *
        0.5f;
    const float radius = 139.0f;
    for (int y = 0;
         y < LAUNCH_GRADIENT_RING_SIZE;
         y++) {
        const float vertical =
            0.20f +
            0.80f *
                (1.0f -
                 (float)y /
                     (float)(
                         LAUNCH_GRADIENT_RING_SIZE -
                         1));
        for (int x = 0;
             x < LAUNCH_GRADIENT_RING_SIZE;
             x++) {
            const float dx = (float)x - center;
            const float dy = (float)y - center;
            const float delta =
                fabsf(
                    sqrtf(dx * dx + dy * dy) -
                    radius);
            const int profile_index =
                delta >= 12.0f
                    ? 192
                    : (int)lroundf(delta * 16.0f);
            const float alpha =
                launch_gradient_radial_profile[
                    profile_index] *
                vertical;
            launch_gradient_ring_buffer[
                y * LAUNCH_GRADIENT_RING_SIZE + x] =
                (uint8_t)lroundf(
                    255.0f *
                    fminf(1.0f, alpha));
        }
    }

    memset(
        &launch_gradient_ring_image,
        0,
        sizeof(launch_gradient_ring_image));
    launch_gradient_ring_image.header.magic =
        LV_IMAGE_HEADER_MAGIC;
    launch_gradient_ring_image.header.cf =
        LV_COLOR_FORMAT_A8;
    launch_gradient_ring_image.header.w =
        LAUNCH_GRADIENT_RING_SIZE;
    launch_gradient_ring_image.header.h =
        LAUNCH_GRADIENT_RING_SIZE;
    launch_gradient_ring_image.header.stride =
        LAUNCH_GRADIENT_RING_SIZE;
    launch_gradient_ring_image.data_size =
        LAUNCH_GRADIENT_RING_SIZE *
        LAUNCH_GRADIENT_RING_SIZE;
    launch_gradient_ring_image.data =
        launch_gradient_ring_buffer;
    return true;
}

static bool launch_prepare_outer_gradient_ring(void)
{
    if (launch_outer_gradient_ring_buffer != NULL) {
        return true;
    }
    launch_outer_gradient_ring_buffer =
        (uint8_t *)heap_caps_malloc(
            LAUNCH_OUTER_GRADIENT_RING_SIZE *
                LAUNCH_OUTER_GRADIENT_RING_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (launch_outer_gradient_ring_buffer == NULL) {
        return false;
    }

    launch_prepare_gradient_radial_profile();

    const float center =
        ((float)LAUNCH_OUTER_GRADIENT_RING_SIZE -
         1.0f) *
        0.5f;
    const float radius = 201.0f;
    for (int y = 0;
         y < LAUNCH_OUTER_GRADIENT_RING_SIZE;
         y++) {
        const float vertical =
            0.32f +
            0.68f *
                (1.0f -
                 (float)y /
                     (float)(
                         LAUNCH_OUTER_GRADIENT_RING_SIZE -
                         1));
        for (int x = 0;
             x < LAUNCH_OUTER_GRADIENT_RING_SIZE;
             x++) {
            const float dx = (float)x - center;
            const float dy = (float)y - center;
            const float radial_distance =
                sqrtf(dx * dx + dy * dy);
            float arc_edge = 0.0f;
            if (radial_distance > 0.0f) {
                const float inverse_radius =
                    1.0f / radial_distance;
                const float left_dot =
                    (dx * -0.78801075f +
                     dy * -0.61566148f) *
                    inverse_radius;
                const float right_dot =
                    (dx * 0.78801075f +
                     dy * -0.61566148f) *
                    inverse_radius;
                const float best_dot =
                    fmaxf(left_dot, right_dot);
                arc_edge = fminf(
                    1.0f,
                    fmaxf(
                        0.0f,
                        (best_dot - 0.91354546f) /
                            (0.94551858f -
                             0.91354546f)));
            }

            float alpha = 0.0f;
            if (arc_edge > 0.0f) {
                const float delta =
                    fabsf(radial_distance - radius);
                const int profile_index =
                    delta >= 12.0f
                        ? 192
                        : (int)lroundf(
                              delta * 16.0f);
                alpha =
                    launch_gradient_radial_profile[
                        profile_index] *
                    vertical *
                    arc_edge;
            }
            launch_outer_gradient_ring_buffer[
                y * LAUNCH_OUTER_GRADIENT_RING_SIZE +
                x] =
                (uint8_t)lroundf(
                    255.0f *
                    fminf(1.0f, alpha));
        }
    }

    memset(
        &launch_outer_gradient_ring_image,
        0,
        sizeof(launch_outer_gradient_ring_image));
    launch_outer_gradient_ring_image.header.magic =
        LV_IMAGE_HEADER_MAGIC;
    launch_outer_gradient_ring_image.header.cf =
        LV_COLOR_FORMAT_A8;
    launch_outer_gradient_ring_image.header.w =
        LAUNCH_OUTER_GRADIENT_RING_SIZE;
    launch_outer_gradient_ring_image.header.h =
        LAUNCH_OUTER_GRADIENT_RING_SIZE;
    launch_outer_gradient_ring_image.header.stride =
        LAUNCH_OUTER_GRADIENT_RING_SIZE;
    launch_outer_gradient_ring_image.data_size =
        LAUNCH_OUTER_GRADIENT_RING_SIZE *
        LAUNCH_OUTER_GRADIENT_RING_SIZE;
    launch_outer_gradient_ring_image.data =
        launch_outer_gradient_ring_buffer;
    return true;
}

static void render_launch_main(void)
{
    app_remote_status_t *remote =
        &launch_render_status;
    if (!launch_get_selected_status(remote)) {
        launch_draw_empty_state();
        return;
    }

    draw_shell();
    const char *const launch_assets[] = {
        launch_agency_asset_name(remote),
        launch_vehicle_asset_name(remote),
    };
    trim_space_asset_cache(
        launch_assets,
        sizeof(launch_assets) / sizeof(launch_assets[0]));
    if (launch_prepare_outer_gradient_ring()) {
        lv_obj_t *outer_ring =
            lv_image_create(screen_root);
        lv_image_set_src(
            outer_ring,
            &launch_outer_gradient_ring_image);
        lv_image_set_antialias(outer_ring, true);
        lv_obj_set_pos(
            outer_ring,
            CX -
                LAUNCH_OUTER_GRADIENT_RING_SIZE / 2,
            CY -
                LAUNCH_OUTER_GRADIENT_RING_SIZE / 2);
        lv_obj_set_style_image_recolor(
            outer_ring,
            lv_color_hex(0xd8d0c4),
            0);
        lv_obj_set_style_image_recolor_opa(
            outer_ring,
            LV_OPA_COVER,
            0);
        lv_obj_set_style_image_opa(
            outer_ring,
            (lv_opa_t)185,
            0);
        lv_obj_remove_flag(
            outer_ring,
            LV_OBJ_FLAG_CLICKABLE |
                LV_OBJ_FLAG_SCROLLABLE);
    } else {
        launch_arc(
            420,
            194,
            242,
            lv_color_hex(0xe6e1d8),
            3,
            (lv_opa_t)180);
        launch_arc(
            420,
            298,
            346,
            lv_color_hex(0xe6e1d8),
            3,
            (lv_opa_t)180);
    }

    const int64_t seconds_until_launch =
        remote->launch_net_unix -
        (int64_t)time(NULL);
    const bool final_five_minutes =
        seconds_until_launch >= 0 &&
        seconds_until_launch <= 300;
    const lv_color_t inner_color =
        final_five_minutes
            ? lv_color_hex(0xff3b30)
            : lv_color_hex(0xd8d0c4);
    launch_inner_ring_uses_gradient =
        launch_prepare_gradient_ring();
    if (launch_inner_ring_uses_gradient) {
        launch_inner_ring =
            lv_image_create(screen_root);
        lv_image_set_src(
            launch_inner_ring,
            &launch_gradient_ring_image);
        lv_image_set_pivot(
            launch_inner_ring,
            LAUNCH_GRADIENT_RING_SIZE / 2,
            LAUNCH_GRADIENT_RING_SIZE / 2);
        lv_image_set_scale(
            launch_inner_ring,
            LAUNCH_GRADIENT_RING_SCALE);
        lv_image_set_antialias(
            launch_inner_ring,
            true);
        lv_obj_set_pos(
            launch_inner_ring,
            CX -
                LAUNCH_GRADIENT_RING_SIZE / 2,
            CY - 5 -
                LAUNCH_GRADIENT_RING_SIZE / 2);
        lv_obj_set_style_image_recolor(
            launch_inner_ring,
            inner_color,
            0);
        lv_obj_set_style_image_recolor_opa(
            launch_inner_ring,
            LV_OPA_COVER,
            0);
        lv_obj_set_style_image_opa(
            launch_inner_ring,
            (lv_opa_t)185,
            0);
        lv_obj_remove_flag(
            launch_inner_ring,
            LV_OBJ_FLAG_CLICKABLE |
                LV_OBJ_FLAG_SCROLLABLE);
    } else {
        launch_inner_ring = ring(
            screen_root,
            286,
            inner_color,
            2,
            (lv_opa_t)162);
        lv_obj_set_y(launch_inner_ring, -5);
    }

    launch_draw_vehicle(
        remote,
        CX,
        236,
        232);

    launch_draw_agency_logo(
        remote,
        CX,
        strcasecmp(remote->launch_agency, "spacex") == 0
            ? 24
            : 28,
        220);
    launch_draw_centered_text(
        screen_root,
        remote->launch_mission,
        &lv_font_montserrat_20,
        lv_color_hex(0xf0f4f2),
        55,
        326);
    launch_draw_centered_text(
        screen_root,
        remote->launch_status,
        &lv_font_montserrat_12,
        launch_status_color(remote),
        84,
        340);

    time_t now = 0;
    time(&now);
    char countdown[40];
    launch_format_countdown(
        remote,
        now,
        countdown,
        sizeof(countdown));
    launch_draw_countdown(
        screen_root,
        320,
        countdown);

    char local_time[12];
    launch_format_local_clock(
        remote,
        now,
        local_time,
        sizeof(local_time));
    char weather[12];
    if (remote
            ->launch_weather_probability_percent >= 0) {
        snprintf(
            weather,
            sizeof(weather),
            "%d%%",
            remote
                ->launch_weather_probability_percent);
    } else {
        snprintf(weather, sizeof(weather), "-");
    }
    char pad_alias[12];
    launch_pad_alias(
        remote,
        pad_alias,
        sizeof(pad_alias));
    launch_draw_bottom_stat(
        "PAD",
        pad_alias,
        146);
    launch_draw_bottom_stat(
        "LOCAL",
        local_time,
        CX);
    launch_draw_bottom_stat(
        "WEATHER",
        weather,
        320);

    launch_draw_centered_text(
        screen_root,
        "\\",
        &lv_font_montserrat_20,
        launch_status_color(remote),
        410,
        30);
    lv_obj_t *divider_left =
        lv_obj_get_child(
            screen_root,
            -1);
    lv_obj_set_x(divider_left, 176);
    launch_draw_centered_text(
        screen_root,
        "/",
        &lv_font_montserrat_20,
        launch_status_color(remote),
        410,
        30);
    lv_obj_t *divider_right =
        lv_obj_get_child(
            screen_root,
            -1);
    lv_obj_set_x(divider_right, 260);
}

static void render_launch_map(void)
{
    app_remote_status_t *remote =
        &launch_render_status;
    if (!launch_get_selected_status(remote)) {
        launch_draw_empty_state();
        return;
    }

    draw_shell();
    int map_asset = SPACE_ASSET_LAUNCH_FLORIDA;
    const char *map_name = "launch_florida";
    if (strcasecmp(remote->launch_site, "starbase") == 0) {
        map_asset = SPACE_ASSET_LAUNCH_STARBASE;
        map_name = "launch_starbase";
    } else if (
        strcasecmp(remote->launch_site, "baikonur") == 0) {
        map_asset = SPACE_ASSET_LAUNCH_BAIKONUR;
        map_name = "launch_baikonur";
    }
    const char *const launch_assets[] = {
        launch_agency_asset_name(remote),
        map_name,
    };
    trim_space_asset_cache(
        launch_assets,
        sizeof(launch_assets) / sizeof(launch_assets[0]));
    draw_space_asset(
        map_asset,
        map_name,
        CX,
        CY,
        LV_SCALE_NONE);

    launch_map_pad_glow = dot(
        screen_root,
        CX,
        CY,
        26,
        lv_color_hex(0xffc34b),
        (lv_opa_t)65);
    lv_obj_set_style_shadow_color(
        launch_map_pad_glow,
        lv_color_hex(0xffb52f),
        0);
    lv_obj_set_style_shadow_width(
        launch_map_pad_glow,
        28,
        0);
    lv_obj_set_style_shadow_opa(
        launch_map_pad_glow,
        (lv_opa_t)100,
        0);
    launch_map_marker = dot(
        screen_root,
        CX,
        CY,
        12,
        lv_color_hex(0xffffff),
        LV_OPA_COVER);
    lv_obj_set_style_shadow_color(
        launch_map_marker,
        lv_color_hex(0xffd558),
        0);
    lv_obj_set_style_shadow_width(
        launch_map_marker,
        16,
        0);
    lv_obj_set_style_shadow_opa(
        launch_map_marker,
        (lv_opa_t)190,
        0);

    lv_obj_t *top = tracked_gradient_panel(
        0,
        0,
        SCREEN_SIZE,
        108,
        TRACKED_PANEL_TOP);
    launch_draw_agency_logo(
        remote,
        CX,
        strcasecmp(remote->launch_agency, "spacex") == 0
            ? 24
            : 29,
        205);
    launch_draw_centered_text(
        top,
        remote->launch_mission,
        &lv_font_montserrat_18,
        lv_color_hex(0xf0f4f2),
        57,
        326);
    launch_draw_centered_text(
        top,
        remote->launch_location,
        &lv_font_montserrat_12,
        lv_color_hex(0x77827d),
        82,
        330);

    lv_obj_t *bottom = tracked_gradient_panel(
        0,
        326,
        SCREEN_SIZE,
        140,
        TRACKED_PANEL_BOTTOM);
    time_t now = 0;
    time(&now);
    char countdown[40];
    launch_format_countdown(
        remote,
        now,
        countdown,
        sizeof(countdown));
    launch_draw_countdown(
        bottom,
        20,
        countdown);
    char pad_alias[12];
    launch_pad_alias(
        remote,
        pad_alias,
        sizeof(pad_alias));
    lv_obj_t *pad = label_box(
        bottom,
        pad_alias,
        &lv_font_montserrat_14,
        lv_color_hex(0xd7ddda),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        180);
    lv_obj_set_pos(pad, 143, 91);
    lv_obj_set_style_text_align(
        pad,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_t *status = label_box(
        bottom,
        remote->launch_status_abbrev[0] != '\0'
            ? remote->launch_status_abbrev
            : remote->launch_status,
        &lv_font_montserrat_12,
        launch_status_color(remote),
        LV_ALIGN_TOP_LEFT,
        0,
        0,
        220);
    lv_obj_set_pos(status, 123, 116);
    lv_obj_set_style_text_align(
        status,
        LV_TEXT_ALIGN_CENTER,
        0);
}

static void render_launch_qr(void)
{
    app_remote_status_t *remote =
        &launch_render_status;
    if (!launch_get_selected_status(remote)) {
        launch_draw_empty_state();
        return;
    }
    draw_shell();
    const char *const launch_assets[] = {
        launch_agency_asset_name(remote),
    };
    trim_space_asset_cache(
        launch_assets,
        sizeof(launch_assets) / sizeof(launch_assets[0]));
    launch_draw_agency_logo(
        remote,
        CX,
        strcasecmp(remote->launch_agency, "spacex") == 0
            ? 28
            : 34,
        220);
    launch_draw_centered_text(
        screen_root,
        remote->launch_mission,
        &lv_font_montserrat_18,
        lv_color_hex(0xf0f4f2),
        69,
        390);

#if LV_USE_QRCODE
    if (remote->launch_watch_url[0] != '\0') {
        lv_obj_t *qr = lv_qrcode_create(screen_root);
        lv_qrcode_set_size(qr, 270);
        lv_qrcode_set_dark_color(
            qr,
            lv_color_hex(0xffffff));
        lv_qrcode_set_light_color(
            qr,
            lv_color_hex(0x000000));
        lv_qrcode_set_quiet_zone(qr, true);
        lv_qrcode_set_data(
            qr,
            remote->launch_watch_url);
        lv_obj_set_pos(qr, 98, 104);
        lv_obj_remove_flag(
            qr,
            LV_OBJ_FLAG_CLICKABLE |
                LV_OBJ_FLAG_SCROLLABLE);
    } else {
        launch_draw_centered_text(
            screen_root,
            "-",
            &lv_font_montserrat_48,
            lv_color_hex(0x808a85),
            207,
            220);
    }
#else
    launch_draw_centered_text(
        screen_root,
        "-",
        &lv_font_montserrat_48,
        lv_color_hex(0x808a85),
        207,
        220);
#endif

    launch_draw_centered_text(
        screen_root,
        remote->launch_watch_url[0] != '\0'
            ? "SCAN TO WATCH"
            : "STREAM NOT AVAILABLE",
        &lv_font_montserrat_14,
        remote->launch_watch_url[0] != '\0'
            ? lv_color_hex(0xe9eeeb)
            : lv_color_hex(0x7a8580),
        394,
        330);
    launch_draw_centered_text(
        screen_root,
        remote->launch_status,
        &lv_font_montserrat_12,
        launch_status_color(remote),
        424,
        360);
}

static void render_launch(void)
{
    switch (launch_view) {
    case LAUNCH_VIEW_MAP:
        render_launch_map();
        break;
    case LAUNCH_VIEW_QR:
        render_launch_qr();
        break;
    case LAUNCH_VIEW_MAIN:
    default:
        render_launch_main();
        break;
    }
}

static int clock_weekday(int year, int month, int day)
{
    static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
        year--;
    }
    return (
        year +
        year / 4 -
        year / 100 +
        year / 400 +
        offsets[month - 1] +
        day
    ) % 7;
}

static int clock_days_in_month(int year, int month)
{
    static const int days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (month == 2 &&
        ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        return 29;
    }
    return days[month - 1];
}

static int clock_nth_sunday(int year, int month, int nth)
{
    const int first_weekday = clock_weekday(year, month, 1);
    return 1 + ((7 - first_weekday) % 7) + (nth - 1) * 7;
}

static int clock_last_sunday(int year, int month)
{
    const int last_day = clock_days_in_month(year, month);
    return last_day - clock_weekday(year, month, last_day);
}

static bool clock_us_daylight_time(time_t now, int standard_offset_hours)
{
    time_t local_standard = now + standard_offset_hours * 3600;
    struct tm value = {0};
    gmtime_r(&local_standard, &value);
    const int year = value.tm_year + 1900;
    const int month = value.tm_mon + 1;
    if (month < 3 || month > 11) {
        return false;
    }
    if (month > 3 && month < 11) {
        return true;
    }
    if (month == 3) {
        const int start = clock_nth_sunday(year, 3, 2);
        return value.tm_mday > start ||
               (value.tm_mday == start && value.tm_hour >= 2);
    }
    const int end = clock_nth_sunday(year, 11, 1);
    return value.tm_mday < end ||
           (value.tm_mday == end && value.tm_hour < 1);
}

static bool clock_europe_daylight_time(time_t now)
{
    struct tm utc = {0};
    gmtime_r(&now, &utc);
    const int year = utc.tm_year + 1900;
    const int month = utc.tm_mon + 1;
    if (month < 3 || month > 10) {
        return false;
    }
    if (month > 3 && month < 10) {
        return true;
    }
    if (month == 3) {
        const int start = clock_last_sunday(year, 3);
        return utc.tm_mday > start ||
               (utc.tm_mday == start && utc.tm_hour >= 1);
    }
    const int end = clock_last_sunday(year, 10);
    return utc.tm_mday < end ||
           (utc.tm_mday == end && utc.tm_hour < 1);
}

static int clock_utc_offset_hours(const char *key, time_t now)
{
    if (key == NULL) {
        return 0;
    }
    if (strcasecmp(key, "SJC") == 0 ||
        strcasecmp(key, "SFO") == 0 ||
        strcasecmp(key, "SAN") == 0) {
        return -8 + (clock_us_daylight_time(now, -8) ? 1 : 0);
    }
    if (strcasecmp(key, "JFK") == 0) {
        return -5 + (clock_us_daylight_time(now, -5) ? 1 : 0);
    }
    if (strcasecmp(key, "DUS") == 0) {
        return 1 + (clock_europe_daylight_time(now) ? 1 : 0);
    }
    if (strcasecmp(key, "MOW") == 0 ||
        strcasecmp(key, "LED") == 0) {
        return 3;
    }
    if (strcasecmp(key, "ICN") == 0) {
        return 9;
    }
    return 0;
}

static void clock_time_for_key(const char *key,
                               time_t now,
                               struct tm *value,
                               char output[8])
{
    time_t local = now + clock_utc_offset_hours(key, now) * 3600;
    gmtime_r(&local, value);
    strftime(output, 8, "%H:%M", value);
}

static const char *clock_active_key(
    const app_location_preset_t *configured)
{
    if (clock_selected_key[0] != '\0') {
        return clock_selected_key;
    }
    return configured != NULL
        ? configured->key
        : "SJC";
}

static bool clock_city_list_contains(
    const app_location_preset_t *const cities[CLOCK_CITY_COUNT],
    int count,
    const char *key)
{
    for (int index = 0; index < count; index++) {
        if (cities[index] != NULL &&
            strcasecmp(cities[index]->key, key) == 0) {
            return true;
        }
    }
    return false;
}

static int clock_build_city_list(
    const app_config_t *config,
    const char *leading_key,
    const app_location_preset_t *cities[CLOCK_CITY_COUNT])
{
    memset(
        cities,
        0,
        sizeof(*cities) * CLOCK_CITY_COUNT);
    int count = 0;

    const app_location_preset_t *leading =
        app_location_preset_find(leading_key);
    if (leading != NULL) {
        cities[count++] = leading;
    }

    char configured_keys[48] = {0};
    if (config != NULL) {
        snprintf(
            configured_keys,
            sizeof(configured_keys),
            "%s",
            config->clock_city_keys);
    }
    char *save_pointer = NULL;
    for (char *key = strtok_r(
             configured_keys,
             ",",
             &save_pointer);
         key != NULL && count < CLOCK_CITY_COUNT;
         key = strtok_r(NULL, ",", &save_pointer)) {
        const app_location_preset_t *preset =
            app_location_preset_find(key);
        if (preset != NULL &&
            !clock_city_list_contains(
                cities,
                count,
                preset->key)) {
            cities[count++] = preset;
        }
    }

    return count;
}

static int find_clock_city_button_at(lv_point_t point)
{
    for (int index = 0;
         index < CLOCK_CITY_COUNT;
         index++) {
        const clock_city_button_t *button =
            &clock_city_buttons[index];
        if (!button->valid) {
            continue;
        }
        if (point.x >= button->x &&
            point.x <= button->x + button->w &&
            point.y >= button->y &&
            point.y <= button->y + button->h) {
            return index;
        }
    }
    return -1;
}

static bool clock_select_next_displayed_city(void)
{
    int displayed_indices[CLOCK_CITY_COUNT];
    int displayed_count = 0;
    int current_position = -1;
    for (int index = 0;
         index < CLOCK_CITY_COUNT;
         index++) {
        if (!clock_city_buttons[index].valid ||
            clock_city_buttons[index].key == NULL) {
            continue;
        }
        displayed_indices[displayed_count] =
            index;
        if (clock_selected_key[0] != '\0' &&
            strcasecmp(
                clock_selected_key,
                clock_city_buttons[index].key) ==
                0) {
            current_position =
                displayed_count;
        }
        displayed_count++;
    }
    if (displayed_count == 0) {
        return false;
    }
    if (current_position < 0) {
        current_position = 0;
    }
    const int next_position =
        (current_position + 1) %
        displayed_count;
    const int next_index =
        displayed_indices[next_position];
    snprintf(
        clock_selected_key,
        sizeof(clock_selected_key),
        "%s",
        clock_city_buttons[next_index].key);
    clock_bezel_uses_secondary = false;
    return true;
}

static const char *clock_default_secondary_key(
    const char *active_key)
{
    return active_key != NULL &&
               strcasecmp(active_key, "SJC") == 0
        ? "MOW"
        : "SJC";
}

static const char *clock_resolved_secondary_key(
    const app_config_t *config,
    const char *active_key)
{
    if (clock_secondary_key[0] != '\0' &&
        (active_key == NULL ||
         strcasecmp(
             clock_secondary_key,
             active_key) != 0) &&
        app_location_preset_find(
            clock_secondary_key) != NULL) {
        return clock_secondary_key;
    }

    const char *fallback =
        clock_default_secondary_key(active_key);
    if (app_location_preset_find(fallback) != NULL) {
        return fallback;
    }

    const app_location_preset_t
        *cities[CLOCK_CITY_COUNT];
    const int count =
        clock_build_city_list(
            config,
            active_key,
            cities);
    for (int index = 0; index < count; index++) {
        if (cities[index] != NULL &&
            (active_key == NULL ||
             strcasecmp(
                 cities[index]->key,
                 active_key) != 0)) {
            return cities[index]->key;
        }
    }
    return active_key != NULL
        ? active_key
        : "SJC";
}

static bool clock_select_next_secondary_city(
    char previous_key[8],
    char next_key[8])
{
    app_config_t config;
    app_config_get(&config);
    const app_location_preset_t *configured =
        app_location_preset_for_config(&config);
    const char *active_key =
        clock_active_key(configured);
    const char *current_key =
        clock_resolved_secondary_key(
            &config,
            active_key);

    snprintf(
        previous_key,
        8,
        "%s",
        current_key);

    const app_location_preset_t
        *configured_cities[CLOCK_CITY_COUNT];
    const int configured_count =
        clock_build_city_list(
            &config,
            active_key,
            configured_cities);
    const app_location_preset_t
        *cities[CLOCK_CITY_COUNT] = {0};
    int count = 0;
    const char *cycle_start_key =
        clock_default_secondary_key(active_key);
    const app_location_preset_t *cycle_start =
        app_location_preset_find(cycle_start_key);
    if (cycle_start != NULL &&
        (active_key == NULL ||
         strcasecmp(cycle_start->key, active_key) != 0)) {
        cities[count++] = cycle_start;
    }
    for (int index = 0;
         index < configured_count &&
         count < CLOCK_CITY_COUNT;
         index++) {
        const app_location_preset_t *candidate =
            configured_cities[index];
        if (candidate == NULL ||
            (active_key != NULL &&
             strcasecmp(candidate->key, active_key) == 0) ||
            clock_city_list_contains(
                cities,
                count,
                candidate->key)) {
            continue;
        }
        cities[count++] = candidate;
    }
    if (count == 0) {
        return false;
    }
    int current_index = -1;
    for (int index = 0; index < count; index++) {
        if (cities[index] != NULL &&
            strcasecmp(
                cities[index]->key,
                current_key) == 0) {
            current_index = index;
            break;
        }
    }

    for (int step = 1; step <= count; step++) {
        const int index =
            (current_index + step + count) %
            count;
        if (cities[index] == NULL) {
            continue;
        }
        snprintf(
            next_key,
            8,
            "%s",
            cities[index]->key);
        snprintf(
            clock_secondary_key,
            sizeof(clock_secondary_key),
            "%s",
            next_key);
        return strcasecmp(
                   previous_key,
                   next_key) != 0;
    }
    return false;
}

static double clock_normalize_degrees(double value)
{
    value = fmod(value, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

static double clock_normalize_hours(double value)
{
    value = fmod(value, 24.0);
    return value < 0.0 ? value + 24.0 : value;
}

static bool clock_sunrise_sunset(
    const app_location_preset_t *location,
    time_t now,
    float *sunrise,
    float *sunset)
{
    if (location == NULL ||
        sunrise == NULL ||
        sunset == NULL) {
        return false;
    }

    const double latitude = strtod(location->lat, NULL);
    const double longitude = strtod(location->lon, NULL);
    const int offset = clock_utc_offset_hours(
        location->key,
        now);
    time_t local_epoch = now + offset * 3600;
    struct tm local = {0};
    gmtime_r(&local_epoch, &local);
    const double day = local.tm_yday + 1;
    const double longitude_hour = longitude / 15.0;
    double results[2] = {0.0, 0.0};

    for (int event = 0; event < 2; event++) {
        const bool is_sunrise = event == 0;
        const double approximate =
            day +
            ((is_sunrise ? 6.0 : 18.0) -
             longitude_hour) /
                24.0;
        const double mean_anomaly =
            0.9856 * approximate - 3.289;
        double true_longitude =
            mean_anomaly +
            1.916 *
                sin(mean_anomaly * DEG_TO_RAD) +
            0.020 *
                sin(2.0 * mean_anomaly * DEG_TO_RAD) +
            282.634;
        true_longitude =
            clock_normalize_degrees(true_longitude);

        double right_ascension =
            atan(
                0.91764 *
                tan(true_longitude * DEG_TO_RAD)) /
            DEG_TO_RAD;
        right_ascension =
            clock_normalize_degrees(right_ascension);
        const double longitude_quadrant =
            floor(true_longitude / 90.0) * 90.0;
        const double ascension_quadrant =
            floor(right_ascension / 90.0) * 90.0;
        right_ascension +=
            longitude_quadrant -
            ascension_quadrant;
        right_ascension /= 15.0;

        const double sine_declination =
            0.39782 *
            sin(true_longitude * DEG_TO_RAD);
        const double cosine_declination =
            cos(asin(sine_declination));
        const double cosine_hour =
            (cos(90.833 * DEG_TO_RAD) -
             sine_declination *
                 sin(latitude * DEG_TO_RAD)) /
            (cosine_declination *
             cos(latitude * DEG_TO_RAD));
        if (cosine_hour > 1.0 ||
            cosine_hour < -1.0) {
            return false;
        }

        double local_hour =
            acos(cosine_hour) / DEG_TO_RAD;
        if (is_sunrise) {
            local_hour = 360.0 - local_hour;
        }
        local_hour /= 15.0;
        const double local_mean_time =
            local_hour +
            right_ascension -
            0.06571 * approximate -
            6.622;
        const double utc =
            clock_normalize_hours(
                local_mean_time -
                longitude_hour);
        results[event] =
            clock_normalize_hours(
                utc + offset);
    }

    *sunrise = (float)results[0];
    *sunset = (float)results[1];
    return true;
}

static lv_color_t clock_city_day_night_color(
    const app_location_preset_t *location,
    time_t now,
    const struct tm *local_time)
{
    float sunrise = 6.0f;
    float sunset = 18.0f;
    (void)clock_sunrise_sunset(
        location,
        now,
        &sunrise,
        &sunset);
    const float hour =
        local_time != NULL
            ? (float)local_time->tm_hour +
                  (float)local_time->tm_min / 60.0f
            : 0.0f;
    const bool daylight =
        hour >= sunrise && hour < sunset;
    return lv_color_hex(
        daylight
            ? 0xc41624
            : 0x174a86);
}

static void clock_draw_time_selection_dot(
    lv_obj_t *parent,
    const char *time_text,
    const lv_font_t *font,
    int center_x,
    int center_y,
    const app_location_preset_t *location,
    time_t now,
    const struct tm *local_time)
{
    lv_point_t text_size = {0};
    lv_text_get_size(
        &text_size,
        time_text,
        font,
        1,
        0,
        LV_COORD_MAX,
        LV_TEXT_FLAG_NONE);
    dot(
        parent,
        center_x - text_size.x / 2 - 13,
        center_y,
        5,
        clock_city_day_night_color(
            location,
            now,
            local_time),
        LV_OPA_COVER);
}

static lv_obj_t *clock_bezel_arc(
    lv_obj_t *parent,
    int start_angle,
    int end_angle,
    uint32_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 424, 424);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(
        arc,
        start_angle,
        end_angle);
    lv_arc_set_angles(
        arc,
        start_angle,
        end_angle);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(
        arc,
        44,
        LV_PART_MAIN);
    lv_obj_set_style_arc_color(
        arc,
        lv_color_hex(color),
        LV_PART_MAIN);
    lv_obj_set_style_arc_opa(
        arc,
        (lv_opa_t)240,
        LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(
        arc,
        false,
        LV_PART_MAIN);
    lv_obj_set_style_arc_width(
        arc,
        44,
        LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(
        arc,
        lv_color_hex(color),
        LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(
        arc,
        (lv_opa_t)240,
        LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(
        arc,
        false,
        LV_PART_INDICATOR);
    lv_obj_remove_flag(
        arc,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    return arc;
}

static void clock_bezel_arc_range(
    lv_obj_t *parent,
    float start,
    float end,
    uint32_t color)
{
    start =
        (float)clock_normalize_degrees(start);
    end =
        (float)clock_normalize_degrees(end);
    const int rounded_start =
        (int)lroundf(start);
    const int rounded_end =
        (int)lroundf(end);
    if (rounded_start == rounded_end) {
        return;
    }
    if (rounded_start < rounded_end) {
        clock_bezel_arc(
            parent,
            rounded_start,
            rounded_end,
            color);
        return;
    }
    clock_bezel_arc(
        parent,
        rounded_start,
        360,
        color);
    clock_bezel_arc(
        parent,
        0,
        rounded_end,
        color);
}

static uint32_t clock_mix_color(
    uint32_t from,
    uint32_t to,
    float progress)
{
    progress =
        fmaxf(0.0f, fminf(1.0f, progress));
    const int from_red = (from >> 16) & 0xff;
    const int from_green = (from >> 8) & 0xff;
    const int from_blue = from & 0xff;
    const int to_red = (to >> 16) & 0xff;
    const int to_green = (to >> 8) & 0xff;
    const int to_blue = to & 0xff;
    const int red =
        (int)lroundf(
            from_red +
            (to_red - from_red) * progress);
    const int green =
        (int)lroundf(
            from_green +
            (to_green - from_green) * progress);
    const int blue =
        (int)lroundf(
            from_blue +
            (to_blue - from_blue) * progress);
    return
        ((uint32_t)red << 16) |
        ((uint32_t)green << 8) |
        (uint32_t)blue;
}

static void clock_gmt_bezel(
    lv_obj_t *parent,
    const app_location_preset_t *location,
    time_t now)
{
    const uint32_t night_color = 0x0b2d59;
    const uint32_t day_color = 0xa8121e;
    clock_bezel_arc(
        parent,
        0,
        360,
        night_color);

    float sunrise = 6.0f;
    float sunset = 18.0f;
    if (!clock_sunrise_sunset(
            location,
            now,
            &sunrise,
            &sunset)) {
        sunrise = 6.0f;
        sunset = 18.0f;
    }

    const float start =
        sunrise * 15.0f - 90.0f;
    const float end =
        sunset * 15.0f - 90.0f;
    const float blend = 3.0f;
    clock_bezel_arc_range(
        parent,
        start + blend,
        end - blend,
        day_color);
    for (int slice = 0; slice < 6; slice++) {
        const float fraction =
            ((float)slice + 0.5f) / 6.0f;
        const float slice_start =
            start - blend +
            slice * (2.0f * blend / 6.0f);
        const float slice_end =
            start - blend +
            (slice + 1) * (2.0f * blend / 6.0f);
        clock_bezel_arc_range(
            parent,
            slice_start,
            slice_end,
            clock_mix_color(
                night_color,
                day_color,
                fraction));
        const float sunset_start =
            end - blend +
            slice * (2.0f * blend / 6.0f);
        const float sunset_end =
            end - blend +
            (slice + 1) * (2.0f * blend / 6.0f);
        clock_bezel_arc_range(
            parent,
            sunset_start,
            sunset_end,
            clock_mix_color(
                day_color,
                night_color,
                fraction));
    }
}

enum {
    CLOCK_MARKER_HALF_WIDTH = 32,
    CLOCK_MARKER_CURVE_RADIUS = 207,
    CLOCK_MARKER_TOP_Y = CY - CLOCK_MARKER_CURVE_RADIUS,
    CLOCK_MARKER_BASE_Y = CLOCK_MARKER_TOP_Y + 3,
    CLOCK_MARKER_TIP_Y = CLOCK_MARKER_TOP_Y + 32,
    CLOCK_MARKER_WIDTH = CLOCK_MARKER_HALF_WIDTH * 2 + 1,
    CLOCK_MARKER_HEIGHT =
        CLOCK_MARKER_TIP_Y -
        CLOCK_MARKER_TOP_Y + 1,
    CLOCK_MARKER_SUPERSAMPLE = 8,
};

static uint8_t *clock_marker_mask_buffer;
static lv_image_dsc_t clock_marker_mask_image;

static bool clock_ensure_marker_mask(void)
{
    if (clock_marker_mask_buffer != NULL) {
        return true;
    }

    const size_t buffer_size =
        CLOCK_MARKER_WIDTH *
        CLOCK_MARKER_HEIGHT;
    clock_marker_mask_buffer =
        heap_caps_malloc(
            buffer_size,
            MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT);
    if (clock_marker_mask_buffer == NULL) {
        clock_marker_mask_buffer =
            heap_caps_malloc(
                buffer_size,
                MALLOC_CAP_8BIT);
    }
    if (clock_marker_mask_buffer == NULL) {
        return false;
    }

    const float base_y =
        (float)(
            CLOCK_MARKER_BASE_Y -
            CLOCK_MARKER_TOP_Y);
    const float tip_y =
        (float)(
            CLOCK_MARKER_TIP_Y -
            CLOCK_MARKER_TOP_Y);
    const int sample_count =
        CLOCK_MARKER_SUPERSAMPLE *
        CLOCK_MARKER_SUPERSAMPLE;
    for (int pixel_y = 0;
         pixel_y < CLOCK_MARKER_HEIGHT;
         pixel_y++) {
        for (int pixel_x = 0;
             pixel_x < CLOCK_MARKER_WIDTH;
             pixel_x++) {
            int coverage = 0;
            for (int sample_y = 0;
                 sample_y <
                 CLOCK_MARKER_SUPERSAMPLE;
                 sample_y++) {
                const float y =
                    (float)pixel_y -
                    0.5f +
                    ((float)sample_y + 0.5f) /
                        CLOCK_MARKER_SUPERSAMPLE;
                for (int sample_x = 0;
                     sample_x <
                     CLOCK_MARKER_SUPERSAMPLE;
                     sample_x++) {
                    const float x =
                        (float)(
                            pixel_x -
                            CLOCK_MARKER_HALF_WIDTH) -
                        0.5f +
                        ((float)sample_x + 0.5f) /
                            CLOCK_MARKER_SUPERSAMPLE;
                    const float absolute_x =
                        fabsf(x);
                    if (absolute_x >
                        CLOCK_MARKER_HALF_WIDTH) {
                        continue;
                    }
                    const float top_y =
                        CLOCK_MARKER_CURVE_RADIUS -
                        sqrtf(
                            (float)(
                                CLOCK_MARKER_CURVE_RADIUS *
                                CLOCK_MARKER_CURVE_RADIUS) -
                            x * x);
                    const float bottom_y =
                        tip_y -
                        absolute_x *
                            (tip_y - base_y) /
                            CLOCK_MARKER_HALF_WIDTH;
                    if (y >= top_y &&
                        y <= bottom_y) {
                        coverage++;
                    }
                }
            }
            clock_marker_mask_buffer[
                pixel_y *
                    CLOCK_MARKER_WIDTH +
                pixel_x] =
                (uint8_t)(
                    (coverage * 255 +
                     sample_count / 2) /
                    sample_count);
        }
    }

    memset(
        &clock_marker_mask_image,
        0,
        sizeof(clock_marker_mask_image));
    clock_marker_mask_image.header.magic =
        LV_IMAGE_HEADER_MAGIC;
    clock_marker_mask_image.header.cf =
        LV_COLOR_FORMAT_A8;
    clock_marker_mask_image.header.w =
        CLOCK_MARKER_WIDTH;
    clock_marker_mask_image.header.h =
        CLOCK_MARKER_HEIGHT;
    clock_marker_mask_image.header.stride =
        CLOCK_MARKER_WIDTH;
    clock_marker_mask_image.data_size =
        buffer_size;
    clock_marker_mask_image.data =
        clock_marker_mask_buffer;
    return true;
}

static void clock_draw_top_marker(
    lv_obj_t *parent)
{
    if (!clock_ensure_marker_mask()) {
        filled_triangle(
            parent,
            CX - CLOCK_MARKER_HALF_WIDTH,
            CLOCK_MARKER_BASE_Y,
            CX + CLOCK_MARKER_HALF_WIDTH,
            CLOCK_MARKER_BASE_Y,
            CX,
            CLOCK_MARKER_TIP_Y,
            lv_color_hex(0xffffff),
            LV_OPA_COVER);
        return;
    }

    lv_obj_t *marker =
        lv_image_create(parent);
    lv_image_set_src(
        marker,
        &clock_marker_mask_image);
    lv_obj_set_pos(
        marker,
        CX - CLOCK_MARKER_HALF_WIDTH,
        CLOCK_MARKER_TOP_Y);
    lv_obj_set_style_image_recolor(
        marker,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(
        marker,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(
        marker,
        LV_OPA_COVER,
        0);
    lv_obj_remove_flag(
        marker,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
}

static void clock_draw_bezel_overlay(
    lv_obj_t *parent,
    const app_location_preset_t *location,
    time_t now)
{
    ring(
        parent,
        436,
        lv_color_hex(0x858b87),
        2,
        (lv_opa_t)135);
    clock_gmt_bezel(parent, location, now);
    ring(
        parent,
        318,
        lv_color_hex(0xc7ccc8),
        2,
        (lv_opa_t)120);

    const float dot_radius = 180.0f;
    for (int hour = 1; hour < 24; hour += 2) {
        const float angle =
            ((float)hour * 15.0f - 90.0f) *
            DEG_TO_RAD;
        dot(
            parent,
            CX +
                (int)lroundf(
                    cosf(angle) *
                    dot_radius),
            CY +
                (int)lroundf(
                    sinf(angle) *
                    dot_radius),
            8,
            lv_color_hex(0xffffff),
            LV_OPA_COVER);
    }

    for (int hour = 2; hour < 24; hour += 2) {
        const float angle_degrees =
            (float)hour * 15.0f - 90.0f;
        const float angle =
            angle_degrees * DEG_TO_RAD;
        char hour_text[4];
        snprintf(
            hour_text,
            sizeof(hour_text),
            "%d",
            hour);
        lv_obj_t *hour_label =
            lv_label_create(parent);
        lv_label_set_text(
            hour_label,
            hour_text);
        lv_obj_set_size(
            hour_label,
            70,
            24);
        lv_obj_set_pos(
            hour_label,
            CX +
                (int)lroundf(
                    cosf(angle) * 190.0f) -
                35,
            CY +
                (int)lroundf(
                    sinf(angle) * 190.0f) -
                12);
        lv_obj_set_style_text_font(
            hour_label,
            &muller_clock_34,
            0);
        lv_obj_set_style_text_color(
            hour_label,
            lv_color_hex(0xffffff),
            0);
        lv_obj_set_style_text_align(
            hour_label,
            LV_TEXT_ALIGN_CENTER,
            0);
        lv_obj_set_style_text_letter_space(
            hour_label,
            0,
            0);
        lv_obj_set_style_transform_pivot_x(
            hour_label,
            35,
            0);
        lv_obj_set_style_transform_pivot_y(
            hour_label,
            12,
            0);
        lv_obj_set_style_transform_scale(
            hour_label,
            315,
            0);
        lv_obj_set_style_transform_rotation(
            hour_label,
            (int32_t)lroundf(
                (angle_degrees + 90.0f) *
                10.0f),
            0);
        lv_obj_remove_flag(
            hour_label,
            LV_OBJ_FLAG_CLICKABLE |
                LV_OBJ_FLAG_SCROLLABLE);
    }

    clock_draw_top_marker(parent);
}

static int clock_hand_x(
    float angle,
    float distance,
    float perpendicular)
{
    return
        CX +
        (int)lroundf(
            cosf(angle) * distance -
            sinf(angle) * perpendicular);
}

static int clock_hand_y(
    float angle,
    float distance,
    float perpendicular)
{
    return
        CY +
        (int)lroundf(
            sinf(angle) * distance +
            cosf(angle) * perpendicular);
}

static lv_obj_t *clock_hand_line(
    lv_obj_t *parent,
    float angle,
    float start,
    float end,
    lv_color_t color,
    int width)
{
    lv_obj_t *object =
        line(
            parent,
            clock_hand_x(angle, start, 0.0f),
            clock_hand_y(angle, start, 0.0f),
            clock_hand_x(angle, end, 0.0f),
            clock_hand_y(angle, end, 0.0f),
            color,
            width,
            LV_OPA_COVER);
    if (object != NULL) {
        lv_obj_set_style_line_rounded(
            object,
            false,
            0);
    }
    return object;
}

static void clock_hand_triangle(
    lv_obj_t *parent,
    float angle,
    float tip_distance,
    float base_distance,
    float half_width,
    lv_color_t color)
{
    filled_triangle(
        parent,
        clock_hand_x(
            angle,
            tip_distance,
            0.0f),
        clock_hand_y(
            angle,
            tip_distance,
            0.0f),
        clock_hand_x(
            angle,
            base_distance,
            half_width),
        clock_hand_y(
            angle,
            base_distance,
            half_width),
        clock_hand_x(
            angle,
            base_distance,
            -half_width),
        clock_hand_y(
            angle,
            base_distance,
            -half_width),
        color,
        LV_OPA_COVER);
}

static void clock_hand_quad(
    lv_obj_t *parent,
    float angle,
    float start_distance,
    float end_distance,
    float half_width,
    lv_color_t color)
{
    const int start_left_x =
        clock_hand_x(
            angle,
            start_distance,
            half_width);
    const int start_left_y =
        clock_hand_y(
            angle,
            start_distance,
            half_width);
    const int start_right_x =
        clock_hand_x(
            angle,
            start_distance,
            -half_width);
    const int start_right_y =
        clock_hand_y(
            angle,
            start_distance,
            -half_width);
    const int end_left_x =
        clock_hand_x(
            angle,
            end_distance,
            half_width);
    const int end_left_y =
        clock_hand_y(
            angle,
            end_distance,
            half_width);
    const int end_right_x =
        clock_hand_x(
            angle,
            end_distance,
            -half_width);
    const int end_right_y =
        clock_hand_y(
            angle,
            end_distance,
            -half_width);

    filled_triangle(
        parent,
        start_left_x,
        start_left_y,
        end_left_x,
        end_left_y,
        end_right_x,
        end_right_y,
        color,
        LV_OPA_COVER);
    filled_triangle(
        parent,
        start_left_x,
        start_left_y,
        end_right_x,
        end_right_y,
        start_right_x,
        start_right_y,
        color,
        LV_OPA_COVER);
}

static void clock_draw_minute_hand_legacy(
    lv_obj_t *parent,
    float angle)
{
    const lv_color_t metal =
        lv_color_hex(0x9da29f);
    const lv_color_t luminous =
        lv_color_hex(0xf7f8f6);

    clock_hand_quad(
        parent,
        angle,
        -4.0f,
        90.0f,
        7.5f,
        metal);
    clock_hand_triangle(
        parent,
        angle,
        117.0f,
        90.0f,
        7.5f,
        metal);
    clock_hand_quad(
        parent,
        angle,
        -4.0f,
        84.0f,
        3.0f,
        luminous);
}

enum {
    CLOCK_HOUR_HAND_SOURCE_SCALE = 2,
    CLOCK_HOUR_HAND_WIDTH = 124 * CLOCK_HOUR_HAND_SOURCE_SCALE,
    CLOCK_HOUR_HAND_HEIGHT = 54 * CLOCK_HOUR_HAND_SOURCE_SCALE,
    CLOCK_HOUR_HAND_PIVOT_X = 14 * CLOCK_HOUR_HAND_SOURCE_SCALE,
    CLOCK_HOUR_HAND_PIVOT_Y = 27 * CLOCK_HOUR_HAND_SOURCE_SCALE,
    CLOCK_HOUR_HAND_SUPERSAMPLE = 4,
};

static lv_color32_t *clock_hour_hand_buffer;
static lv_image_dsc_t clock_hour_hand_image;

static float clock_hour_hand_segment_distance_sq(
    float x,
    float y,
    float start_x,
    float start_y,
    float end_x,
    float end_y)
{
    const float segment_x = end_x - start_x;
    const float segment_y = end_y - start_y;
    const float length_sq =
        segment_x * segment_x +
        segment_y * segment_y;
    float t = 0.0f;
    if (length_sq > 0.0001f) {
        t =
            ((x - start_x) * segment_x +
             (y - start_y) * segment_y) /
            length_sq;
        t = fmaxf(0.0f, fminf(1.0f, t));
    }
    const float nearest_x =
        start_x + t * segment_x;
    const float nearest_y =
        start_y + t * segment_y;
    const float dx = x - nearest_x;
    const float dy = y - nearest_y;
    return dx * dx + dy * dy;
}

static bool clock_hour_hand_metal_contains(
    float x,
    float y)
{
    const bool shaft =
        x >= -6.0f &&
        x <= 68.0f &&
        fabsf(y) <= 7.0f;
    float pointer_half_width = -1.0f;
    if (x >= 64.0f && x <= 106.0f) {
        pointer_half_width =
            8.0f *
            (106.0f - x) /
            (106.0f - 64.0f);
    }
    const bool pointer =
        pointer_half_width >= 0.0f &&
        fabsf(y) <= pointer_half_width;
    return shaft || pointer;
}

static bool clock_hour_hand_lume_contains(
    float x,
    float y)
{
    const bool shaft =
        x >= -4.0f &&
        x <= 68.0f &&
        fabsf(y) <= 3.0f;
    float pointer_half_width = -1.0f;
    if (x >= 64.0f && x <= 100.0f) {
        pointer_half_width =
            3.2f *
            (100.0f - x) /
            (100.0f - 64.0f);
    }
    const bool pointer =
        pointer_half_width >= 0.0f &&
        fabsf(y) <=
            pointer_half_width;
    return shaft || pointer;
}

static bool clock_hour_hand_medallion_metal_contains(
    float x,
    float y)
{
    const float dx = x - 64.0f;
    return dx * dx + y * y <= 21.0f * 21.0f;
}

static bool clock_hour_hand_medallion_lume_contains(
    float x,
    float y)
{
    const float medallion_x = 64.0f;
    const float dx = x - medallion_x;
    const bool inside_medallion =
        dx * dx + y * y <=
        14.5f * 14.5f;
    if (!inside_medallion) {
        return false;
    }

    const float spoke_inner = 3.0f;
    const float spoke_outer = 15.0f;
    const float spoke_half_width = 2.0f;
    for (int spoke = 0; spoke < 3; spoke++) {
        const float spoke_angle =
            (float)spoke *
            120.0f *
            DEG_TO_RAD;
        const float cosine = cosf(spoke_angle);
        const float sine = sinf(spoke_angle);
        if (clock_hour_hand_segment_distance_sq(
                x,
                y,
                medallion_x + cosine * spoke_inner,
                sine * spoke_inner,
                medallion_x + cosine * spoke_outer,
                sine * spoke_outer) <=
            spoke_half_width * spoke_half_width) {
            return false;
        }
    }

    return dx * dx + y * y > 3.5f * 3.5f;
}

static bool clock_ensure_hour_hand_masks(void)
{
    if (clock_hour_hand_buffer != NULL) {
        return true;
    }

    const size_t pixel_count =
        CLOCK_HOUR_HAND_WIDTH *
        CLOCK_HOUR_HAND_HEIGHT;
    const size_t buffer_size =
        pixel_count *
        sizeof(lv_color32_t);
    clock_hour_hand_buffer =
        (lv_color32_t *)heap_caps_malloc(
            buffer_size,
            MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT);
    if (clock_hour_hand_buffer == NULL) {
        clock_hour_hand_buffer =
            (lv_color32_t *)heap_caps_malloc(
                buffer_size,
                MALLOC_CAP_8BIT);
    }
    if (clock_hour_hand_buffer == NULL) {
        return false;
    }

    const int sample_count =
        CLOCK_HOUR_HAND_SUPERSAMPLE *
        CLOCK_HOUR_HAND_SUPERSAMPLE;
    for (int pixel_y = 0;
         pixel_y < CLOCK_HOUR_HAND_HEIGHT;
         pixel_y++) {
        for (int pixel_x = 0;
             pixel_x < CLOCK_HOUR_HAND_WIDTH;
             pixel_x++) {
            int metal_samples = 0;
            int lume_samples = 0;
            for (int sample_y = 0;
                 sample_y <
                 CLOCK_HOUR_HAND_SUPERSAMPLE;
                 sample_y++) {
                const float y =
                    ((float)(
                         pixel_y -
                         CLOCK_HOUR_HAND_PIVOT_Y) -
                     0.5f +
                     ((float)sample_y + 0.5f) /
                         CLOCK_HOUR_HAND_SUPERSAMPLE) /
                    CLOCK_HOUR_HAND_SOURCE_SCALE;
                for (int sample_x = 0;
                     sample_x <
                     CLOCK_HOUR_HAND_SUPERSAMPLE;
                     sample_x++) {
                    const float x =
                        ((float)(
                             pixel_x -
                             CLOCK_HOUR_HAND_PIVOT_X) -
                         0.5f +
                         ((float)sample_x + 0.5f) /
                             CLOCK_HOUR_HAND_SUPERSAMPLE) /
                        CLOCK_HOUR_HAND_SOURCE_SCALE;
                    if (clock_hour_hand_medallion_lume_contains(
                            x,
                            y)) {
                        lume_samples++;
                    } else if (
                        clock_hour_hand_medallion_metal_contains(
                            x,
                            y)) {
                        metal_samples++;
                    } else if (
                        clock_hour_hand_lume_contains(
                            x,
                            y)) {
                        lume_samples++;
                    } else if (
                        clock_hour_hand_metal_contains(
                            x,
                            y)) {
                        metal_samples++;
                    }
                }
            }
            const int offset =
                pixel_y *
                    CLOCK_HOUR_HAND_WIDTH +
                pixel_x;
            const int visible_samples =
                metal_samples + lume_samples;
            if (visible_samples == 0) {
                clock_hour_hand_buffer[offset] =
                    lv_color32_make(0, 0, 0, 0);
                continue;
            }

            /* Resolve every supersample to its final foreground layer before
             * averaging.  The ESP32 then rotates one antialiased image rather
             * than four coincident masks, so the rim cannot develop seams. */
            const uint8_t red =
                (uint8_t)(
                    (metal_samples * 0x9d +
                     lume_samples * 0xf7 +
                     visible_samples / 2) /
                    visible_samples);
            const uint8_t green =
                (uint8_t)(
                    (metal_samples * 0xa2 +
                     lume_samples * 0xf8 +
                     visible_samples / 2) /
                    visible_samples);
            const uint8_t blue =
                (uint8_t)(
                    (metal_samples * 0x9f +
                     lume_samples * 0xf6 +
                     visible_samples / 2) /
                    visible_samples);
            const uint8_t alpha =
                (uint8_t)(
                    (visible_samples * 255 +
                     sample_count / 2) /
                    sample_count);
            clock_hour_hand_buffer[offset] =
                lv_color32_make(
                    red,
                    green,
                    blue,
                    alpha);
        }
    }

    memset(
        &clock_hour_hand_image,
        0,
        sizeof(clock_hour_hand_image));
    clock_hour_hand_image.header.magic =
        LV_IMAGE_HEADER_MAGIC;
    clock_hour_hand_image.header.cf =
        LV_COLOR_FORMAT_ARGB8888;
    clock_hour_hand_image.header.w =
        CLOCK_HOUR_HAND_WIDTH;
    clock_hour_hand_image.header.h =
        CLOCK_HOUR_HAND_HEIGHT;
    clock_hour_hand_image.header.stride =
        CLOCK_HOUR_HAND_WIDTH *
        sizeof(lv_color32_t);
    clock_hour_hand_image.data_size =
        buffer_size;
    clock_hour_hand_image.data =
        (const uint8_t *)clock_hour_hand_buffer;
    return true;
}

static void clock_draw_hour_hand_legacy(
    lv_obj_t *parent,
    float angle)
{
    const lv_color_t metal =
        lv_color_hex(0x9da29f);
    const lv_color_t luminous =
        lv_color_hex(0xf7f8f6);
    const float medallion_distance = 64.0f;
    const int medallion_x =
        clock_hand_x(
            angle,
            medallion_distance,
            0.0f);
    const int medallion_y =
        clock_hand_y(
            angle,
            medallion_distance,
            0.0f);

    clock_hand_line(
        parent,
        angle,
        -4.0f,
        68.0f,
        metal,
        14);
    clock_hand_line(
        parent,
        angle,
        -4.0f,
        68.0f,
        luminous,
        6);
    clock_hand_triangle(
        parent,
        angle,
        106.0f,
        64.0f,
        8.0f,
        metal);
    clock_hand_triangle(
        parent,
        angle,
        100.0f,
        64.0f,
        3.2f,
        luminous);

    /* Draw the Mercedes medallion last. Its complete rim must sit over the
     * shaft and pointer instead of being merged into either silhouette. */
    soft_blob(
        parent,
        medallion_x,
        medallion_y,
        21,
        metal,
        LV_OPA_COVER);
    soft_blob(
        parent,
        medallion_x,
        medallion_y,
        15,
        luminous,
        LV_OPA_COVER);

    const float spoke_distances[3] = {
        77.0f,
        57.0f,
        57.0f,
    };
    const float spoke_offsets[3] = {
        0.0f,
        6.0f,
        -6.0f,
    };
    for (int spoke = 0;
         spoke < 3;
         spoke++) {
        lv_obj_t *object =
            line(
                parent,
                medallion_x,
                medallion_y,
                clock_hand_x(
                    angle,
                    spoke_distances[spoke],
                    spoke_offsets[spoke]),
                clock_hand_y(
                    angle,
                    spoke_distances[spoke],
                    spoke_offsets[spoke]),
                metal,
                4,
                LV_OPA_COVER);
        if (object != NULL) {
            lv_obj_set_style_line_rounded(
                object,
                true,
                0);
        }
    }

}

static void clock_draw_hand_pivot(
    lv_obj_t *parent)
{
    /* Both supplied hand images include their own finished pivot cap. */
    (void)parent;
}

static void clock_draw_hour_hand_layer(
    lv_obj_t *parent,
    const lv_image_dsc_t *source,
    float angle)
{
    lv_obj_t *image =
        lv_image_create(parent);
    lv_image_set_src(image, source);
    lv_obj_set_pos(
        image,
        CX - CLOCK_HOUR_HAND_PIVOT_X,
        CY - CLOCK_HOUR_HAND_PIVOT_Y);
    lv_image_set_pivot(
        image,
        CLOCK_HOUR_HAND_PIVOT_X,
        CLOCK_HOUR_HAND_PIVOT_Y);
    lv_image_set_rotation(
        image,
        (int32_t)lroundf(
            angle /
            DEG_TO_RAD *
            10.0f));
    lv_image_set_scale(
        image,
        LV_SCALE_NONE /
            CLOCK_HOUR_HAND_SOURCE_SCALE);
    lv_image_set_antialias(image, true);
    lv_obj_set_style_image_opa(
        image,
        LV_OPA_COVER,
        0);
    lv_obj_remove_flag(
        image,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
}

static void clock_draw_hour_hand_procedural(
    lv_obj_t *parent,
    float angle)
{
    if (!clock_ensure_hour_hand_masks()) {
        clock_draw_hour_hand_legacy(
            parent,
            angle);
        return;
    }

    clock_draw_hour_hand_layer(
        parent,
        &clock_hour_hand_image,
        angle);
}

static void clock_draw_asset_hand(
    lv_obj_t *parent,
    const lv_image_dsc_t *source,
    int pivot_x,
    int pivot_y,
    float angle)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, source);
    lv_obj_set_pos(
        image,
        CX - pivot_x,
        CY - pivot_y);
    lv_image_set_pivot(
        image,
        pivot_x,
        pivot_y);
    lv_image_set_rotation(
        image,
        (int32_t)lroundf(
            angle /
                DEG_TO_RAD *
                10.0f +
            900.0f));
    lv_image_set_scale(
        image,
        AIRSPACE_CLOCK_HAND_SCALE);
    lv_image_set_antialias(image, true);
    lv_obj_set_style_image_opa(
        image,
        LV_OPA_COVER,
        0);
    lv_obj_remove_flag(
        image,
        LV_OBJ_FLAG_CLICKABLE |
        LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *clock_draw_dial_crown(
    lv_obj_t *parent)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(
        image,
        &airspace_clock_crown);
    lv_obj_set_pos(
        image,
        CX - 20,
        118);
    lv_obj_set_style_image_recolor(
        image,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_image_recolor_opa(
        image,
        LV_OPA_COVER,
        0);
    lv_obj_set_style_image_opa(
        image,
        LV_OPA_COVER,
        0);
    lv_obj_remove_flag(
        image,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    return image;
}

static void clock_crown_opacity_animation(
    void *object,
    int32_t value)
{
    lv_obj_t *image = object;
    if (image != NULL && lv_obj_is_valid(image)) {
        lv_obj_set_style_image_opa(
            image,
            (lv_opa_t)value,
            0);
    }
}

static void clock_show_live_crown(
    lv_obj_t *parent)
{
    gmt_live_crown = NULL;
    if (clock_digital_mode || parent == NULL) {
        return;
    }
    gmt_live_crown =
        clock_draw_dial_crown(parent);
    lv_obj_set_style_image_opa(
        gmt_live_crown,
        clock_crown_visible
            ? LV_OPA_COVER
            : LV_OPA_TRANSP,
        0);
}

static bool clock_animate_live_crown(bool visible)
{
    if (gmt_live_crown == NULL ||
        !lv_obj_is_valid(gmt_live_crown)) {
        return false;
    }
    lv_anim_delete(
        gmt_live_crown,
        clock_crown_opacity_animation);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(
        &animation,
        gmt_live_crown);
    lv_anim_set_exec_cb(
        &animation,
        clock_crown_opacity_animation);
    lv_anim_set_values(
        &animation,
        lv_obj_get_style_image_opa(
            gmt_live_crown,
            0),
        visible ? LV_OPA_COVER : LV_OPA_TRANSP);
    lv_anim_set_duration(&animation, 420);
    lv_anim_set_path_cb(
        &animation,
        lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
    return true;
}

static void clock_draw_hour_hand(
    lv_obj_t *parent,
    float angle)
{
    clock_draw_asset_hand(
        parent,
        &airspace_clock_hand_hour,
        AIRSPACE_CLOCK_HAND_HOUR_PIVOT_X,
        AIRSPACE_CLOCK_HAND_HOUR_PIVOT_Y,
        angle);
}

static void clock_draw_minute_hand(
    lv_obj_t *parent,
    float angle)
{
    clock_draw_asset_hand(
        parent,
        &airspace_clock_hand_minute,
        AIRSPACE_CLOCK_HAND_MINUTE_PIVOT_X,
        AIRSPACE_CLOCK_HAND_MINUTE_PIVOT_Y,
        angle);
}

static void clock_draw_gmt_hand(
    lv_obj_t *parent,
    float angle)
{
    const lv_color_t red =
        lv_color_hex(0xe3222f);
    clock_hand_line(
        parent,
        angle,
        -5.0f,
        128.0f,
        red,
        3);
    clock_hand_triangle(
        parent,
        angle,
        154.0f,
        126.0f,
        15.0f,
        red);
    clock_hand_triangle(
        parent,
        angle,
        146.0f,
        132.0f,
        8.0f,
        lv_color_hex(0xffffff));
}

static int clock_date_day_for_key(
    const char *location_key,
    time_t now)
{
    struct tm local = {0};
    char ignored_time[8];
    clock_time_for_key(
        location_key != NULL
            ? location_key
            : "SJC",
        now,
        &local,
        ignored_time);
    return local.tm_mday;
}

static bool clock_prepare_date_transition(
    const char *location_key,
    time_t now,
    int *previous_day,
    int *current_day)
{
    const int next_day =
        clock_date_day_for_key(
            location_key,
            now);
    const bool same_location =
        gmt_date_location_key_seen[0] != '\0' &&
        location_key != NULL &&
        strcasecmp(
            gmt_date_location_key_seen,
            location_key) == 0;
    const bool animate =
        !clock_digital_mode &&
        same_location &&
        gmt_date_day_seen > 0 &&
        gmt_date_day_seen != next_day;

    if (previous_day != NULL) {
        *previous_day =
            animate
                ? gmt_date_day_seen
                : next_day;
    }
    if (current_day != NULL) {
        *current_day = next_day;
    }
    gmt_date_day_seen = next_day;
    snprintf(
        gmt_date_location_key_seen,
        sizeof(gmt_date_location_key_seen),
        "%.7s",
        location_key != NULL
            ? location_key
            : "");
    return animate;
}

static char clock_date_digit(
    int day,
    int slot)
{
    if (slot == 0) {
        return day >= 10
            ? (char)('0' + (day / 10) % 10)
            : '\0';
    }
    return (char)('0' + day % 10);
}

static lv_obj_t *clock_date_digit_label(
    lv_obj_t *parent,
    char digit,
    int y)
{
    if (digit == '\0') {
        return NULL;
    }
    char text[2] = {digit, '\0'};
    lv_obj_t *object =
        lv_label_create(parent);
    lv_label_set_text(
        object,
        text);
    lv_obj_set_size(
        object,
        28,
        24);
    lv_obj_set_pos(
        object,
        0,
        y);
    lv_obj_set_style_text_font(
        object,
        &muller_clock_34,
        0);
    lv_obj_set_style_text_color(
        object,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_text_align(
        object,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_remove_flag(
        object,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static void clock_date_digit_y_animation(
    void *object,
    int32_t y)
{
    lv_obj_set_y(
        (lv_obj_t *)object,
        y);
}

static void clock_start_date_digit_animation(
    lv_obj_t *object,
    int from_y,
    int to_y)
{
    if (object == NULL) {
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(
        &animation,
        object);
    lv_anim_set_exec_cb(
        &animation,
        clock_date_digit_y_animation);
    lv_anim_set_values(
        &animation,
        from_y,
        to_y);
    lv_anim_set_duration(
        &animation,
        560);
    lv_anim_set_path_cb(
        &animation,
        lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

static void clock_show_static_date(
    lv_obj_t *window,
    int day)
{
    if (window == NULL) {
        return;
    }
    char text[4];
    snprintf(
        text,
        sizeof(text),
        "%d",
        day);
    lv_obj_t *object =
        lv_label_create(window);
    lv_label_set_text(
        object,
        text);
    lv_obj_set_size(
        object,
        70,
        24);
    lv_obj_set_pos(
        object,
        0,
        0);
    lv_obj_set_style_text_font(
        object,
        &muller_clock_34,
        0);
    lv_obj_set_style_text_color(
        object,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_text_align(
        object,
        LV_TEXT_ALIGN_RIGHT,
        0);
    lv_obj_set_style_text_letter_space(
        object,
        2,
        0);
    lv_obj_remove_flag(
        object,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
}

static void clock_date_animation_driver(
    void *object,
    int32_t value)
{
    (void)object;
    (void)value;
}

static void clock_date_animation_completed(
    lv_anim_t *animation)
{
    lv_obj_t *window =
        animation != NULL
            ? (lv_obj_t *)animation->var
            : NULL;
    if (window == NULL ||
        window != gmt_date_animation_window ||
        !lv_obj_is_valid(window)) {
        return;
    }
    lv_obj_clean(window);
    clock_show_static_date(
        window,
        gmt_date_animation_day);
    gmt_date_animation_window = NULL;
}

static void clock_show_live_date(
    lv_obj_t *parent,
    int previous_day,
    int current_day,
    bool animate)
{
    if (parent == NULL ||
        clock_digital_mode) {
        return;
    }

    lv_obj_t *window =
        lv_obj_create(parent);
    lv_obj_remove_style_all(window);
    lv_obj_set_size(
        window,
        70,
        24);
    lv_obj_set_pos(
        window,
        288,
        CY - 12);
    lv_obj_remove_flag(
        window,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE |
            LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    const bool digit_count_changed =
        (previous_day >= 10) !=
        (current_day >= 10);
    for (int slot_index = 0;
         slot_index < 2;
         slot_index++) {
        lv_obj_t *slot =
            lv_obj_create(window);
        lv_obj_remove_style_all(slot);
        lv_obj_set_size(
            slot,
            28,
            24);
        lv_obj_set_pos(
            slot,
            14 + slot_index * 28,
            0);
        lv_obj_remove_flag(
            slot,
            LV_OBJ_FLAG_CLICKABLE |
                LV_OBJ_FLAG_SCROLLABLE |
                LV_OBJ_FLAG_OVERFLOW_VISIBLE);

        const char old_digit =
            clock_date_digit(
                previous_day,
                slot_index);
        const char new_digit =
            clock_date_digit(
                current_day,
                slot_index);
        const bool digit_changed =
            animate &&
            (digit_count_changed ||
             old_digit != new_digit);
        if (!digit_changed) {
            (void)clock_date_digit_label(
                slot,
                new_digit,
                0);
            continue;
        }

        lv_obj_t *old_label =
            clock_date_digit_label(
                slot,
                old_digit,
                0);
        lv_obj_t *new_label =
            clock_date_digit_label(
                slot,
                new_digit,
                24);
        clock_start_date_digit_animation(
            old_label,
            0,
            -24);
        clock_start_date_digit_animation(
            new_label,
            24,
            0);
    }
    if (animate) {
        gmt_date_animation_window =
            window;
        gmt_date_animation_day =
            current_day;
        lv_anim_t completion;
        lv_anim_init(&completion);
        lv_anim_set_var(
            &completion,
            window);
        lv_anim_set_exec_cb(
            &completion,
            clock_date_animation_driver);
        lv_anim_set_values(
            &completion,
            0,
            1000);
        lv_anim_set_duration(
            &completion,
            560);
        lv_anim_set_completed_cb(
            &completion,
            clock_date_animation_completed);
        lv_anim_start(&completion);
    } else {
        lv_obj_clean(window);
        clock_show_static_date(
            window,
            current_day);
    }
}

static void render_gmt_scene(
    lv_obj_t *screen,
    bool analog_base_only,
    bool omit_analog_hands,
    bool omit_analog_date,
    bool omit_analog_crown)
{
    draw_shell_on(screen);

    app_config_t cfg;
    app_config_get(&cfg);
    const app_location_preset_t *configured =
        app_location_preset_for_config(&cfg);
    const char *active_key =
        clock_active_key(configured);
    const app_location_preset_t *selected =
        app_location_preset_find(active_key);
    const app_location_preset_t *bezel_location =
        selected;

    time_t now = 0;
    time(&now);
    if (now <= 0) {
        label(
            screen_root,
            "WAITING FOR TIME",
            &lv_font_montserrat_18,
            lv_color_hex(0xffb000),
            LV_ALIGN_CENTER,
            0,
            0);
        return;
    }

    lv_obj_t *content = lv_obj_create(screen_root);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    soft_blob(content, CX, CY, 300, lv_color_hex(0x0c1722), (lv_opa_t)60);
    memset(
        clock_city_buttons,
        0,
        sizeof(clock_city_buttons));
    const app_location_preset_t
        *displayed_cities[CLOCK_CITY_COUNT];
    const char *leading_key =
        configured != NULL
            ? configured->key
            : "SJC";
    const int displayed_city_count =
        clock_build_city_list(
            &cfg,
            leading_key,
            displayed_cities);

    if (clock_digital_mode) {
        label(
            content,
            "WORLD TIME",
            &lv_font_montserrat_16,
            lv_color_hex(0xf2f5f3),
            LV_ALIGN_TOP_MID,
            0,
            117);

        ensure_weather_gradients();
        for (int index = 0;
             index < displayed_city_count;
             index++) {
            const app_location_preset_t *city =
                displayed_cities[index];
            const int y = 164 + index * 34;
            struct tm local = {0};
            char time_text[8] = "--:--";
            clock_time_for_key(
                city->key,
                now,
                &local,
                time_text);
            const bool active =
                strcasecmp(
                    active_key,
                    city->key) == 0;
            float sunrise = 6.0f;
            float sunset = 18.0f;
            clock_sunrise_sunset(
                city,
                now,
                &sunrise,
                &sunset);
            const float local_hour =
                local.tm_hour +
                local.tm_min / 60.0f;
            const bool daylight =
                local_hour >= sunrise &&
                local_hour < sunset;
            clock_city_buttons[index] =
                (clock_city_button_t) {
                    .valid = true,
                    .x = 100,
                    .y = y - 4,
                    .w = 260,
                    .h = 34,
                    .key = city->key,
                };

            if (index > 0) {
                draw_weather_gradient_bar(
                    130,
                    y - 5,
                    210,
                    1,
                    &weather_divider_gradient);
            }
            if (active) {
                filled_triangle(
                    content,
                    104,
                    y + 3,
                    104,
                    y + 17,
                    115,
                    y + 10,
                    lv_color_hex(0xffffff),
                    LV_OPA_COVER);
            }
            dot(
                content,
                128,
                y + 10,
                6,
                lv_color_hex(
                    clock_mix_color(
                        daylight
                            ? 0xa8121e
                            : 0x0b2d59,
                        0xffffff,
                        0.30f)),
                LV_OPA_COVER);
            label(
                content,
                city->key,
                &lv_font_montserrat_16,
                lv_color_hex(0xf0f3f1),
                LV_ALIGN_TOP_LEFT,
                144,
                y + 1);
            label(
                content,
                city->label,
                &lv_font_montserrat_12,
                lv_color_hex(0x747e77),
                LV_ALIGN_TOP_LEFT,
                196,
                y + 4);
            lv_obj_t *time_label = label_box(
                content,
                time_text,
                &muller_clock_22,
                lv_color_hex(0xffffff),
                LV_ALIGN_TOP_LEFT,
                0,
                0,
                82);
            lv_obj_set_pos(
                time_label,
                271,
                y - 1);
            lv_obj_set_style_text_align(
                time_label,
                LV_TEXT_ALIGN_RIGHT,
                0);
            lv_obj_set_style_transform_scale(
                time_label,
                194,
                0);
            lv_obj_set_style_transform_pivot_x(
                time_label,
                82,
                0);
            lv_obj_set_style_transform_pivot_y(
                time_label,
                10,
                0);
        }
    } else {
        struct tm local = {0};
        char local_time[8] = "--:--";
        clock_time_for_key(selected != NULL ? selected->key : "SJC",
                           now,
                           &local,
                           local_time);
        const char *secondary_key =
            clock_resolved_secondary_key(
                &cfg,
                selected != NULL
                    ? selected->key
                    : active_key);
        const app_location_preset_t
            *secondary_bezel_location =
                app_location_preset_find(
                    secondary_key);
        if (clock_bezel_uses_secondary &&
            secondary_bezel_location != NULL) {
            bezel_location =
                secondary_bezel_location;
        }
        struct tm secondary = {0};
        char secondary_time[8] = "--:--";
        clock_time_for_key(
            secondary_key,
            now,
            &secondary,
            secondary_time);

        ring(
            content,
            310,
            lv_color_hex(0x202b32),
            1,
            (lv_opa_t)150);
        ring(
            content,
            286,
            lv_color_hex(0x59635e),
            2,
            (lv_opa_t)95);
        if (clock_crown_visible &&
            !omit_analog_crown) {
            clock_draw_dial_crown(content);
        }
        if (!omit_analog_date) {
            char date_text[4];
            snprintf(
                date_text,
                sizeof(date_text),
                "%d",
                local.tm_mday);
            lv_obj_t *date_label = label(
                content,
                date_text,
                &muller_clock_34,
                lv_color_hex(0xffffff),
                LV_ALIGN_RIGHT_MID,
                -108,
                0);
            lv_obj_set_style_text_letter_space(
                date_label,
                2,
                0);
        }

        const float minute_angle =
            ((float)local.tm_min * 6.0f - 90.0f) * DEG_TO_RAD;
        const float hour_angle =
            (((float)(local.tm_hour % 12) + (float)local.tm_min / 60.0f) *
                 30.0f -
             90.0f) *
            DEG_TO_RAD;
        const float secondary_angle =
            (((float)secondary.tm_hour + (float)secondary.tm_min / 60.0f) *
                 15.0f -
             90.0f) *
            DEG_TO_RAD;
        label(
            content,
            selected != NULL ? selected->label : "LOCAL",
            &lv_font_montserrat_14,
            lv_color_hex(0xffffff),
            LV_ALIGN_CENTER,
            0,
            24);
        label(
            content,
            local_time,
            &muller_clock_34,
            lv_color_hex(0xffffff),
            LV_ALIGN_CENTER,
            0,
            52);
        if (!clock_bezel_uses_secondary) {
            clock_draw_time_selection_dot(
                content,
                local_time,
                &muller_clock_34,
                CX,
                CY + 52,
                selected,
                now,
                &local);
        }
        ensure_weather_gradients();
        draw_weather_gradient_bar(
            158,
            311,
            150,
            1,
            &weather_divider_gradient);
        const app_location_preset_t
            *secondary_location =
                app_location_preset_find(
                    secondary_key);
        if (clock_bezel_uses_secondary) {
            clock_draw_time_selection_dot(
                content,
                secondary_time,
                &muller_clock_22,
                CX,
                349,
                secondary_location,
                now,
                &secondary);
        }
        if (!analog_base_only) {
            label(
                content,
                secondary_location != NULL
                    ? secondary_location->label
                    : secondary_key,
                &lv_font_montserrat_14,
                lv_color_hex(0xcbd1cd),
                LV_ALIGN_TOP_MID,
                0,
                317);
            label(
                content,
                secondary_time,
                &muller_clock_22,
                lv_color_hex(0xffffff),
                LV_ALIGN_TOP_MID,
                0,
                337);

            if (!omit_analog_hands) {
                clock_draw_gmt_hand(
                    content,
                    secondary_angle);
            }
        }
        if (!omit_analog_hands) {
            clock_draw_hour_hand(
                content,
                hour_angle);
            clock_draw_minute_hand(
                content,
                minute_angle);
            clock_draw_hand_pivot(
                content);
        }
    }

    lv_obj_t *bezel_overlay =
        lv_obj_create(screen_root);
    lv_obj_remove_style_all(bezel_overlay);
    lv_obj_set_size(
        bezel_overlay,
        SCREEN_SIZE,
        SCREEN_SIZE);
    lv_obj_set_pos(bezel_overlay, 0, 0);
    lv_obj_remove_flag(
        bezel_overlay,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    clock_draw_bezel_overlay(
        bezel_overlay,
        bezel_location,
        now);
}

static void clock_prepare_digital_city_hitboxes(void)
{
    memset(
        clock_city_buttons,
        0,
        sizeof(clock_city_buttons));
    if (!clock_digital_mode) {
        return;
    }

    app_config_t cfg;
    app_config_get(&cfg);
    const app_location_preset_t *configured =
        app_location_preset_for_config(&cfg);
    const app_location_preset_t
        *displayed_cities[CLOCK_CITY_COUNT];
    const char *leading_key =
        configured != NULL ? configured->key : "SJC";
    const int displayed_city_count =
        clock_build_city_list(
            &cfg,
            leading_key,
            displayed_cities);
    for (int index = 0;
         index < displayed_city_count;
         index++) {
        const int y = 164 + index * 34;
        clock_city_buttons[index] =
            (clock_city_button_t) {
                .valid = true,
                .x = 100,
                .y = y - 4,
                .w = 260,
                .h = 34,
                .key = displayed_cities[index]->key,
            };
    }
}

static void show_gmt_snapshot(void)
{
    gmt_live_crown = NULL;
    gmt_live_hands_layer = NULL;
    gmt_live_hands_second_seen = -1;
    draw_shell();
    lv_obj_t *image =
        lv_image_create(screen_root);
    lv_image_set_src(
        image,
        &gmt_snapshot_image);
    lv_obj_set_pos(image, 0, 0);
    lv_obj_remove_flag(
        image,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    clock_prepare_digital_city_hitboxes();
}

static void clock_update_live_hands(
    time_t now,
    const char *active_key,
    const char *secondary_key,
    bool include_gmt)
{
    if (gmt_live_hands_layer == NULL ||
        now <= 0) {
        return;
    }

    lv_obj_clean(
        gmt_live_hands_layer);
    line_pair_count = 0;

    struct tm local = {0};
    char ignored_time[8];
    clock_time_for_key(
        active_key != NULL
            ? active_key
            : "SJC",
        now,
        &local,
        ignored_time);
    const float seconds =
        (float)local.tm_sec;
    const float minute_angle =
        (((float)local.tm_min +
          seconds / 60.0f) *
             6.0f -
         90.0f) *
        DEG_TO_RAD;
    const float hour_angle =
        (((float)(local.tm_hour % 12) +
          (float)local.tm_min / 60.0f +
          seconds / 3600.0f) *
             30.0f -
         90.0f) *
        DEG_TO_RAD;

    if (include_gmt) {
        struct tm secondary = {0};
        clock_time_for_key(
            secondary_key != NULL
                ? secondary_key
                : "MOW",
            now,
            &secondary,
            ignored_time);
        const float secondary_angle =
            (((float)secondary.tm_hour +
              (float)secondary.tm_min / 60.0f +
              (float)secondary.tm_sec /
                  3600.0f) *
                 15.0f -
             90.0f) *
            DEG_TO_RAD;
        clock_draw_gmt_hand(
            gmt_live_hands_layer,
            secondary_angle);
    }
    clock_draw_hour_hand(
        gmt_live_hands_layer,
        hour_angle);
    clock_draw_minute_hand(
        gmt_live_hands_layer,
        minute_angle);
    clock_draw_hand_pivot(
        gmt_live_hands_layer);
    gmt_live_hands_second_seen =
        (int64_t)now;
}

static void clock_show_live_hands(
    lv_obj_t *parent,
    time_t now,
    const char *active_key,
    const char *secondary_key,
    bool include_gmt)
{
    gmt_live_hands_layer =
        lv_obj_create(parent);
    lv_obj_remove_style_all(
        gmt_live_hands_layer);
    lv_obj_set_size(
        gmt_live_hands_layer,
        SCREEN_SIZE,
        SCREEN_SIZE);
    lv_obj_set_pos(
        gmt_live_hands_layer,
        0,
        0);
    lv_obj_remove_flag(
        gmt_live_hands_layer,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    clock_update_live_hands(
        now,
        active_key,
        secondary_key,
        include_gmt);
}

static bool capture_gmt_snapshot(
    uint8_t **buffer,
    lv_image_dsc_t *image,
    time_t now,
    const char *location_key,
    const char *secondary_key,
    bool analog_base_only,
    bool dynamic_hands)
{
    (void)now;
    (void)location_key;
    (void)secondary_key;
    if (buffer == NULL || image == NULL) {
        return false;
    }
    if (*buffer == NULL) {
        *buffer =
            heap_caps_aligned_alloc(
                128,
                GMT_SNAPSHOT_BYTES,
                MALLOC_CAP_SPIRAM |
                    MALLOC_CAP_8BIT);
        if (*buffer == NULL) {
            ESP_LOGW(
                TAG,
                "No PSRAM for GMT snapshot");
            return false;
        }
    }

    lv_obj_t *stage = lv_obj_create(NULL);
    if (stage == NULL) {
        return false;
    }
    lv_obj_t *const previous_screen_root = screen_root;
    lv_obj_t *const previous_date_window =
        gmt_date_animation_window;
    clock_city_button_t previous_city_buttons[
        CLOCK_CITY_COUNT];
    memcpy(
        previous_city_buttons,
        clock_city_buttons,
        sizeof(previous_city_buttons));
    lv_obj_set_size(
        stage,
        SCREEN_SIZE,
        SCREEN_SIZE);

    const int64_t started_us =
        esp_timer_get_time();
    render_gmt_scene(
        stage,
        analog_base_only,
        dynamic_hands,
        true,
        true);
    if (image->data != NULL) {
        lv_image_cache_drop(image);
    }
    memset(image, 0, sizeof(*image));
    const lv_result_t result =
        lv_snapshot_take_to_buf(
            stage,
            LV_COLOR_FORMAT_RGB565,
            image,
            *buffer,
            GMT_SNAPSHOT_BYTES);
    lv_obj_delete(stage);
    /* render_gmt_scene() deliberately uses the normal renderer and therefore
     * temporarily points these globals at its detached stage.  Restore the
     * live dial after the off-screen capture so it can remain interactive
     * throughout the fade-out. */
    screen_root = previous_screen_root;
    gmt_date_animation_window = previous_date_window;
    memcpy(
        clock_city_buttons,
        previous_city_buttons,
        sizeof(previous_city_buttons));
    if (result != LV_RESULT_OK) {
        ESP_LOGW(
            TAG,
            "GMT snapshot failed");
        return false;
    }

    ESP_LOGI(
        TAG,
        "GMT snapshot composed in %lld ms",
        (long long)(
            (esp_timer_get_time() -
             started_us) /
            1000));
    return true;
}

static bool render_gmt_snapshot(
    time_t now,
    const char *location_key,
    const char *secondary_key,
    bool analog_base_only,
    bool dynamic_hands)
{
    if (!capture_gmt_snapshot(
            &gmt_snapshot_buffer,
            &gmt_snapshot_image,
            now,
            location_key,
            secondary_key,
            analog_base_only,
            dynamic_hands)) {
        return false;
    }

    gmt_snapshot_minute =
        now > 0
            ? (int)(now / 60)
            : -1;
    gmt_snapshot_digital_mode =
        clock_digital_mode;
    gmt_snapshot_base_only =
        analog_base_only;
    gmt_snapshot_dynamic_hands =
        dynamic_hands;
    gmt_snapshot_bezel_uses_secondary =
        clock_bezel_uses_secondary;
    snprintf(
        gmt_snapshot_location_key,
        sizeof(gmt_snapshot_location_key),
        "%s",
        location_key != NULL
            ? location_key
            : "");
    snprintf(
        gmt_snapshot_secondary_key,
        sizeof(gmt_snapshot_secondary_key),
        "%s",
        secondary_key != NULL
            ? secondary_key
            : "");
    return true;
}

static bool gmt_transition_snapshot_matches(
    int minute,
    const char *location_key,
    const char *secondary_key,
    bool dynamic_hands)
{
    return
        gmt_transition_snapshot_ready &&
        gmt_transition_snapshot_buffer != NULL &&
        gmt_transition_snapshot_image.data ==
            gmt_transition_snapshot_buffer &&
        gmt_transition_snapshot_minute == minute &&
        gmt_transition_snapshot_digital_mode ==
            clock_digital_mode &&
        gmt_transition_snapshot_dynamic_hands ==
            dynamic_hands &&
        gmt_transition_snapshot_bezel_uses_secondary ==
            clock_bezel_uses_secondary &&
        !gmt_transition_snapshot_base_only &&
        strcasecmp(
            gmt_transition_snapshot_location_key,
            location_key != NULL ? location_key : "") == 0 &&
        strcasecmp(
            gmt_transition_snapshot_secondary_key,
            secondary_key != NULL ? secondary_key : "") == 0;
}

static bool prepare_gmt_transition_snapshot(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    const app_location_preset_t *configured =
        app_location_preset_for_config(&cfg);
    const char *location_key =
        clock_active_key(configured);
    const char *secondary_key =
        clock_resolved_secondary_key(
            &cfg,
            location_key);
    time_t now = 0;
    time(&now);
    const int minute =
        now > 0 ? (int)(now / 60) : -1;
    const bool dynamic_hands =
        !clock_digital_mode;

    if (gmt_transition_snapshot_matches(
            minute,
            location_key,
            secondary_key,
            dynamic_hands)) {
        return true;
    }
    gmt_transition_snapshot_ready = false;
    if (!capture_gmt_snapshot(
            &gmt_transition_snapshot_buffer,
            &gmt_transition_snapshot_image,
            now,
            location_key,
            secondary_key,
            false,
            dynamic_hands)) {
        return false;
    }

    gmt_transition_snapshot_minute = minute;
    gmt_transition_snapshot_digital_mode =
        clock_digital_mode;
    gmt_transition_snapshot_base_only = false;
    gmt_transition_snapshot_dynamic_hands =
        dynamic_hands;
    gmt_transition_snapshot_bezel_uses_secondary =
        clock_bezel_uses_secondary;
    snprintf(
        gmt_transition_snapshot_location_key,
        sizeof(gmt_transition_snapshot_location_key),
        "%s",
        location_key != NULL ? location_key : "");
    snprintf(
        gmt_transition_snapshot_secondary_key,
        sizeof(gmt_transition_snapshot_secondary_key),
        "%s",
        secondary_key != NULL ? secondary_key : "");
    gmt_transition_snapshot_ready = true;
    return true;
}

static void promote_gmt_transition_snapshot(void)
{
    const bool previous_snapshot_valid =
        gmt_snapshot_buffer != NULL &&
        gmt_snapshot_image.data ==
            gmt_snapshot_buffer;
    if (gmt_snapshot_image.data != NULL) {
        lv_image_cache_drop(&gmt_snapshot_image);
    }
    if (gmt_transition_snapshot_image.data != NULL) {
        lv_image_cache_drop(
            &gmt_transition_snapshot_image);
    }

#define GMT_SWAP_VALUE(type, left, right) \
    do {                                  \
        type temporary = (left);          \
        (left) = (right);                 \
        (right) = temporary;              \
    } while (0)
    GMT_SWAP_VALUE(
        uint8_t *,
        gmt_snapshot_buffer,
        gmt_transition_snapshot_buffer);
    GMT_SWAP_VALUE(
        lv_image_dsc_t,
        gmt_snapshot_image,
        gmt_transition_snapshot_image);
    GMT_SWAP_VALUE(
        int,
        gmt_snapshot_minute,
        gmt_transition_snapshot_minute);
    GMT_SWAP_VALUE(
        bool,
        gmt_snapshot_digital_mode,
        gmt_transition_snapshot_digital_mode);
    GMT_SWAP_VALUE(
        bool,
        gmt_snapshot_base_only,
        gmt_transition_snapshot_base_only);
    GMT_SWAP_VALUE(
        bool,
        gmt_snapshot_dynamic_hands,
        gmt_transition_snapshot_dynamic_hands);
    GMT_SWAP_VALUE(
        bool,
        gmt_snapshot_bezel_uses_secondary,
        gmt_transition_snapshot_bezel_uses_secondary);
#undef GMT_SWAP_VALUE

    char key_swap[8];
    memcpy(
        key_swap,
        gmt_snapshot_location_key,
        sizeof(key_swap));
    memcpy(
        gmt_snapshot_location_key,
        gmt_transition_snapshot_location_key,
        sizeof(gmt_snapshot_location_key));
    memcpy(
        gmt_transition_snapshot_location_key,
        key_swap,
        sizeof(gmt_transition_snapshot_location_key));
    memcpy(
        key_swap,
        gmt_snapshot_secondary_key,
        sizeof(key_swap));
    memcpy(
        gmt_snapshot_secondary_key,
        gmt_transition_snapshot_secondary_key,
        sizeof(gmt_snapshot_secondary_key));
    memcpy(
        gmt_transition_snapshot_secondary_key,
        key_swap,
        sizeof(gmt_transition_snapshot_secondary_key));
    gmt_transition_snapshot_ready =
        previous_snapshot_valid;
}

typedef struct {
    lv_point_precise_t line[2];
    lv_point_precise_t outer[3];
    lv_point_precise_t inner[3];
} clock_gmt_animation_hand_data_t;

static clock_gmt_animation_hand_data_t
    gmt_animation_hand_data;

static void clock_gmt_animation_hand_draw(
    lv_event_t *event)
{
    if (lv_event_get_code(event) !=
        LV_EVENT_DRAW_MAIN) {
        return;
    }
    clock_gmt_animation_hand_data_t *data =
        (clock_gmt_animation_hand_data_t *)
            lv_event_get_user_data(event);
    if (data == NULL) {
        return;
    }

    lv_obj_t *object =
        lv_event_get_target(event);
    lv_area_t area;
    lv_obj_get_coords(
        object,
        &area);
    lv_layer_t *layer =
        lv_event_get_layer(event);

    lv_point_precise_t line_points[2];
    for (int point = 0; point < 2; point++) {
        line_points[point].x =
            area.x1 +
            data->line[point].x;
        line_points[point].y =
            area.y1 +
            data->line[point].y;
    }
    lv_draw_line_dsc_t line_descriptor;
    lv_draw_line_dsc_init(
        &line_descriptor);
    line_descriptor.base.layer =
        layer;
    line_descriptor.color =
        lv_color_hex(0xe3222f);
    line_descriptor.opa =
        LV_OPA_COVER;
    line_descriptor.width = 3;
    line_descriptor.round_start =
        false;
    line_descriptor.round_end =
        false;
    line_descriptor.points =
        line_points;
    line_descriptor.point_cnt = 2;
    lv_draw_line(
        layer,
        &line_descriptor);

    const lv_point_precise_t
        *triangle_points[2] = {
            data->outer,
            data->inner,
        };
    const lv_color_t colors[2] = {
        lv_color_hex(0xe3222f),
        lv_color_hex(0xffffff),
    };
    for (int triangle = 0;
         triangle < 2;
         triangle++) {
        lv_draw_triangle_dsc_t descriptor;
        lv_draw_triangle_dsc_init(
            &descriptor);
        descriptor.base.layer =
            layer;
        descriptor.color =
            colors[triangle];
        descriptor.opa =
            LV_OPA_COVER;
        for (int point = 0;
             point < 3;
             point++) {
            descriptor.p[point].x =
                area.x1 +
                triangle_points[
                    triangle][point].x;
            descriptor.p[point].y =
                area.y1 +
                triangle_points[
                    triangle][point].y;
        }
        lv_draw_triangle(
            layer,
            &descriptor);
    }
}

static void clock_gmt_animation_hand_update(
    lv_obj_t *object,
    float angle)
{
    int points[8][2] = {
        {
            clock_hand_x(angle, -5.0f, 0.0f),
            clock_hand_y(angle, -5.0f, 0.0f),
        },
        {
            clock_hand_x(angle, 128.0f, 0.0f),
            clock_hand_y(angle, 128.0f, 0.0f),
        },
        {
            clock_hand_x(angle, 154.0f, 0.0f),
            clock_hand_y(angle, 154.0f, 0.0f),
        },
        {
            clock_hand_x(angle, 126.0f, 15.0f),
            clock_hand_y(angle, 126.0f, 15.0f),
        },
        {
            clock_hand_x(angle, 126.0f, -15.0f),
            clock_hand_y(angle, 126.0f, -15.0f),
        },
        {
            clock_hand_x(angle, 146.0f, 0.0f),
            clock_hand_y(angle, 146.0f, 0.0f),
        },
        {
            clock_hand_x(angle, 132.0f, 8.0f),
            clock_hand_y(angle, 132.0f, 8.0f),
        },
        {
            clock_hand_x(angle, 132.0f, -8.0f),
            clock_hand_y(angle, 132.0f, -8.0f),
        },
    };
    int minimum_x = points[0][0];
    int maximum_x = points[0][0];
    int minimum_y = points[0][1];
    int maximum_y = points[0][1];
    for (int point = 1; point < 8; point++) {
        minimum_x =
            points[point][0] < minimum_x
                ? points[point][0]
                : minimum_x;
        maximum_x =
            points[point][0] > maximum_x
                ? points[point][0]
                : maximum_x;
        minimum_y =
            points[point][1] < minimum_y
                ? points[point][1]
                : minimum_y;
        maximum_y =
            points[point][1] > maximum_y
                ? points[point][1]
                : maximum_y;
    }

    const int margin = 3;
    const int origin_x =
        minimum_x - margin;
    const int origin_y =
        minimum_y - margin;
    lv_obj_invalidate(object);
    lv_obj_set_pos(
        object,
        origin_x,
        origin_y);
    lv_obj_set_size(
        object,
        maximum_x - minimum_x +
            margin * 2 + 1,
        maximum_y - minimum_y +
            margin * 2 + 1);

    for (int point = 0; point < 2; point++) {
        gmt_animation_hand_data
            .line[point].x =
                points[point][0] -
                origin_x;
        gmt_animation_hand_data
            .line[point].y =
                points[point][1] -
                origin_y;
    }
    for (int point = 0; point < 3; point++) {
        gmt_animation_hand_data
            .outer[point].x =
                points[point + 2][0] -
                origin_x;
        gmt_animation_hand_data
            .outer[point].y =
                points[point + 2][1] -
                origin_y;
        gmt_animation_hand_data
            .inner[point].x =
                points[point + 5][0] -
                origin_x;
        gmt_animation_hand_data
            .inner[point].y =
                points[point + 5][1] -
                origin_y;
    }
    lv_obj_invalidate(object);
}

static lv_obj_t *clock_gmt_animation_hand_create(
    lv_obj_t *parent,
    float angle)
{
    lv_obj_t *object =
        lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(
        object,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(
        object,
        clock_gmt_animation_hand_draw,
        LV_EVENT_DRAW_MAIN,
        &gmt_animation_hand_data);
    clock_gmt_animation_hand_update(
        object,
        angle);
    return object;
}

static void clock_gmt_hand_rotation_animation(
    void *object,
    int32_t value)
{
    const float degrees =
        gmt_secondary_hand_start_degrees +
        gmt_secondary_hand_delta_degrees *
            (float)value /
            1000.0f;
    clock_gmt_animation_hand_update(
        (lv_obj_t *)object,
        degrees * DEG_TO_RAD);
}

static void clock_secondary_roll_animation(
    void *object,
    int32_t value)
{
    (void)object;
    if (gmt_secondary_roll_old == NULL ||
        gmt_secondary_roll_new == NULL) {
        return;
    }
    const int travel =
        (int)lroundf(
            30.0f *
            (float)value /
            1000.0f);
    lv_obj_set_y(
        gmt_secondary_roll_old,
        -travel);
    lv_obj_set_y(
        gmt_secondary_roll_new,
        30 - travel);
}

static void clock_secondary_transition_completed(
    lv_anim_t *animation)
{
    (void)animation;
    gmt_secondary_transition_finish_pending =
        true;
}

static lv_obj_t *clock_secondary_time_label(
    lv_obj_t *parent,
    const char *text,
    int y)
{
    lv_obj_t *object =
        lv_label_create(parent);
    lv_label_set_text(
        object,
        text);
    lv_obj_set_size(
        object,
        150,
        30);
    lv_obj_set_pos(
        object,
        0,
        y);
    lv_obj_set_style_text_font(
        object,
        &muller_clock_22,
        0);
    lv_obj_set_style_text_color(
        object,
        lv_color_hex(0xffffff),
        0);
    lv_obj_set_style_text_align(
        object,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_remove_flag(
        object,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static bool clock_begin_secondary_transition(void)
{
    if (clock_digital_mode ||
        gmt_secondary_transition_active) {
        return false;
    }

    app_config_t config;
    app_config_get(&config);
    const app_location_preset_t *configured =
        app_location_preset_for_config(&config);
    const char *active_key =
        clock_active_key(configured);

    if (!clock_bezel_uses_secondary) {
        const char *secondary_key =
            clock_resolved_secondary_key(
                &config,
                active_key);
        snprintf(
            clock_secondary_key,
            sizeof(clock_secondary_key),
            "%s",
            secondary_key);
        clock_bezel_uses_secondary = true;
        render_requested = true;
        return true;
    }

    char previous_key[8] = {0};
    char next_key[8] = {0};
    if (!clock_select_next_secondary_city(
            previous_key,
            next_key)) {
        clock_bezel_uses_secondary = false;
        render_requested = true;
        return true;
    }
    const char *cycle_start_key =
        clock_default_secondary_key(active_key);
    if (strcasecmp(next_key, cycle_start_key) == 0 &&
        strcasecmp(previous_key, cycle_start_key) != 0) {
        clock_bezel_uses_secondary = false;
    }

    time_t now = 0;
    time(&now);
    if (now <= 0) {
        render_requested = true;
        return true;
    }

    struct tm previous_time = {0};
    struct tm next_time = {0};
    char previous_text[8] = "--:--";
    char next_text[8] = "--:--";
    clock_time_for_key(
        previous_key,
        now,
        &previous_time,
        previous_text);
    clock_time_for_key(
        next_key,
        now,
        &next_time,
        next_text);

    if (!render_gmt_snapshot(
            now,
            active_key,
            next_key,
            true,
            true)) {
        render_requested = true;
        return true;
    }
    show_gmt_snapshot();
    const int active_date_day =
        clock_date_day_for_key(
            active_key,
            now);
    clock_show_live_date(
        screen_root,
        active_date_day,
        active_date_day,
        false);

    lv_obj_t *overlay =
        lv_obj_create(screen_root);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(
        overlay,
        SCREEN_SIZE,
        SCREEN_SIZE);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_remove_flag(
        overlay,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE);

    const app_location_preset_t
        *next_location =
            app_location_preset_find(
                next_key);
    label(
        overlay,
        next_location != NULL
            ? next_location->label
            : next_key,
        &lv_font_montserrat_14,
        lv_color_hex(0xcbd1cd),
        LV_ALIGN_TOP_MID,
        0,
        317);

    lv_obj_t *roll_window =
        lv_obj_create(overlay);
    lv_obj_remove_style_all(
        roll_window);
    lv_obj_set_size(
        roll_window,
        150,
        30);
    lv_obj_set_pos(
        roll_window,
        158,
        337);
    lv_obj_remove_flag(
        roll_window,
        LV_OBJ_FLAG_CLICKABLE |
            LV_OBJ_FLAG_SCROLLABLE |
            LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    gmt_secondary_roll_old =
        clock_secondary_time_label(
            roll_window,
            previous_text,
            0);
    gmt_secondary_roll_new =
        clock_secondary_time_label(
            roll_window,
            next_text,
            30);

    const float previous_degrees =
        ((float)previous_time.tm_hour +
         (float)previous_time.tm_min / 60.0f) *
            15.0f -
        90.0f;
    const float next_degrees =
        ((float)next_time.tm_hour +
         (float)next_time.tm_min / 60.0f) *
            15.0f -
        90.0f;
    float delta_degrees =
        next_degrees -
        previous_degrees;
    while (delta_degrees > 180.0f) {
        delta_degrees -= 360.0f;
    }
    while (delta_degrees < -180.0f) {
        delta_degrees += 360.0f;
    }

    gmt_secondary_hand_start_degrees =
        previous_degrees;
    gmt_secondary_hand_delta_degrees =
        delta_degrees;
    lv_obj_t *hand =
        clock_gmt_animation_hand_create(
            overlay,
            previous_degrees *
                DEG_TO_RAD);
    clock_show_live_hands(
        overlay,
        now,
        active_key,
        next_key,
        false);

    gmt_secondary_transition_active =
        true;
    gmt_secondary_transition_finish_pending =
        false;

    lv_anim_t hand_animation;
    lv_anim_init(
        &hand_animation);
    lv_anim_set_var(
        &hand_animation,
        hand);
    lv_anim_set_exec_cb(
        &hand_animation,
        clock_gmt_hand_rotation_animation);
    lv_anim_set_values(
        &hand_animation,
        0,
        1000);
    lv_anim_set_duration(
        &hand_animation,
        720);
    lv_anim_set_path_cb(
        &hand_animation,
        lv_anim_path_ease_in_out);
    lv_anim_start(
        &hand_animation);

    lv_anim_t roll_animation;
    lv_anim_init(
        &roll_animation);
    lv_anim_set_var(
        &roll_animation,
        roll_window);
    lv_anim_set_exec_cb(
        &roll_animation,
        clock_secondary_roll_animation);
    lv_anim_set_values(
        &roll_animation,
        0,
        1000);
    lv_anim_set_duration(
        &roll_animation,
        620);
    lv_anim_set_delay(
        &roll_animation,
        50);
    lv_anim_set_path_cb(
        &roll_animation,
        lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(
        &roll_animation,
        clock_secondary_transition_completed);
    lv_anim_start(
        &roll_animation);
    return true;
}

static void render_gmt(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    const app_location_preset_t *configured =
        app_location_preset_for_config(&cfg);
    const char *location_key =
        clock_active_key(configured);
    const char *secondary_key =
        clock_resolved_secondary_key(
            &cfg,
            location_key);
    time_t now = 0;
    time(&now);
    const int minute =
        now > 0
            ? (int)(now / 60)
            : -1;
    int previous_date_day = 0;
    int current_date_day = 0;
    const bool animate_date =
        now > 0 &&
        clock_prepare_date_transition(
            location_key,
            now,
            &previous_date_day,
            &current_date_day);
    const bool dynamic_hands =
        !clock_digital_mode;

    if (gmt_transition_snapshot_matches(
            minute,
            location_key,
            secondary_key,
            dynamic_hands)) {
        promote_gmt_transition_snapshot();
    }

    const bool snapshot_matches =
        gmt_snapshot_buffer != NULL &&
        gmt_snapshot_image.data ==
            gmt_snapshot_buffer &&
        gmt_snapshot_minute == minute &&
        gmt_snapshot_digital_mode ==
            clock_digital_mode &&
        gmt_snapshot_dynamic_hands ==
            dynamic_hands &&
        gmt_snapshot_bezel_uses_secondary ==
            clock_bezel_uses_secondary &&
        !gmt_snapshot_base_only &&
        strcasecmp(
            gmt_snapshot_location_key,
            location_key != NULL
                ? location_key
                : "") == 0 &&
        strcasecmp(
            gmt_snapshot_secondary_key,
            secondary_key != NULL
                ? secondary_key
                : "") == 0;

    if (snapshot_matches) {
        show_gmt_snapshot();
        if (dynamic_hands) {
            clock_show_live_crown(screen_root);
            clock_show_live_date(
                screen_root,
                previous_date_day,
                current_date_day,
                animate_date);
            clock_show_live_hands(
                screen_root,
                now,
                location_key,
                secondary_key,
                true);
        }
        return;
    }
    if (render_gmt_snapshot(
            now,
            location_key,
            secondary_key,
            false,
            dynamic_hands)) {
        show_gmt_snapshot();
        if (dynamic_hands) {
            clock_show_live_crown(screen_root);
            clock_show_live_date(
                screen_root,
                previous_date_day,
                current_date_day,
                animate_date);
            clock_show_live_hands(
                screen_root,
                now,
                location_key,
                secondary_key,
                true);
        }
        gmt_secondary_transition_active =
            false;
        gmt_secondary_transition_finish_pending =
            false;
        return;
    }
    render_gmt_scene(
        lv_screen_active(),
        false,
        false,
        false,
        false);
}

typedef enum {
    DETAIL_PHASE_UNKNOWN = 0,
    DETAIL_PHASE_CLIMBING,
    DETAIL_PHASE_CRUISING,
    DETAIL_PHASE_DESCENDING,
} detail_flight_phase_t;

#define DETAIL_ROUTE_START_X 102
#define DETAIL_ROUTE_END_X 364
#define DETAIL_ROUTE_ENDPOINT_Y 246
#define DETAIL_ROUTE_CONTROL_1_X 156
#define DETAIL_ROUTE_CONTROL_1_Y 177
#define DETAIL_ROUTE_CONTROL_2_X 310
#define DETAIL_ROUTE_CONTROL_2_Y 177
#define DETAIL_ROUTE_MASK_X 94
#define DETAIL_ROUTE_MASK_Y 186
#define DETAIL_ROUTE_MASK_WIDTH 278
#define DETAIL_ROUTE_MASK_HEIGHT 68
#define DETAIL_ROUTE_MASK_BYTES (DETAIL_ROUTE_MASK_WIDTH * DETAIL_ROUTE_MASK_HEIGHT)
#define DETAIL_ROUTE_SPLINE_SEGMENTS 128

static uint8_t *detail_route_mask_buffer;
static lv_image_dsc_t detail_route_mask_image;
static bool detail_route_mask_ready;

static bool detail_contains_ci(const char *value, const char *needle)
{
    if (value == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }
    const size_t needle_length = strlen(needle);
    for (const char *candidate = value; *candidate != '\0'; candidate++) {
        if (strncasecmp(candidate, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

static void detail_aircraft_model_text(const app_aircraft_t *aircraft,
                                       char *destination,
                                       size_t destination_size)
{
    if (destination == NULL || destination_size == 0) {
        return;
    }
    destination[0] = '\0';
    if (aircraft == NULL) {
        return;
    }

    if (aircraft->model[0] != '\0') {
        if (aircraft->manufacturer[0] != '\0' &&
            !detail_contains_ci(aircraft->model, aircraft->manufacturer)) {
            snprintf(destination,
                     destination_size,
                     "%s %s",
                     aircraft->manufacturer,
                     aircraft->model);
        } else {
            snprintf(destination, destination_size, "%s", aircraft->model);
        }
        return;
    }

    const char *canonical = airspace_aircraft_canonical_label(
        aircraft->typecode,
        aircraft->model,
        aircraft->manufacturer);
    snprintf(destination,
             destination_size,
             "%s",
             canonical != NULL ? canonical : airspace_aircraft_icon_label_for_aircraft(aircraft));
}

static detail_flight_phase_t detail_phase_for_aircraft(const app_aircraft_t *aircraft)
{
    if (aircraft == NULL || !aircraft->vertical_rate_valid) {
        return DETAIL_PHASE_UNKNOWN;
    }
    if (aircraft->vertical_rate_mps >= 1.1f) {
        return DETAIL_PHASE_CLIMBING;
    }
    if (aircraft->vertical_rate_mps <= -1.1f) {
        return DETAIL_PHASE_DESCENDING;
    }
    if (aircraft->altitude_ft >= 18000 && aircraft->speed_kt >= 250) {
        return DETAIL_PHASE_CRUISING;
    }
    return DETAIL_PHASE_UNKNOWN;
}

static const char *detail_compass_direction(const app_aircraft_t *aircraft)
{
    static const char *directions[] = {
        "N", "NNE", "NE", "ENE",
        "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW",
        "W", "WNW", "NW", "NNW",
    };
    if (aircraft == NULL || !aircraft->track_valid) {
        return "-";
    }
    return directions[((aircraft->track_deg + 11) / 22) % 16];
}

static lv_obj_t *detail_centered_label(lv_obj_t *parent,
                                       const char *text,
                                       const lv_font_t *font,
                                       lv_color_t color,
                                       int x,
                                       int y,
                                       int width)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, color, 0);
    lv_obj_set_style_text_letter_space(object, 1, 0);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    return object;
}

static void detail_image_opacity_animation(void *object, int32_t opacity)
{
    lv_obj_set_style_image_opa((lv_obj_t *)object, (lv_opa_t)opacity, 0);
}

static void detail_x_animation(void *object, int32_t x)
{
    lv_obj_set_x((lv_obj_t *)object, x);
}

static void detail_line_opacity_animation(void *object, int32_t opacity)
{
    lv_obj_set_style_line_opa((lv_obj_t *)object, (lv_opa_t)opacity, 0);
}

static void detail_dot_scale_animation(void *object, int32_t scale)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)object, scale, 0);
}

static void detail_start_animation(lv_obj_t *object,
                                   lv_anim_exec_xcb_t callback,
                                   int32_t from,
                                   int32_t to,
                                   uint32_t duration,
                                   uint32_t delay,
                                   lv_anim_path_cb_t path)
{
    if (object == NULL) {
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, callback);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_delay(&animation, delay);
    lv_anim_set_path_cb(&animation, path);
    lv_anim_start(&animation);
}

static void detail_route_spline_point(float fraction, float *x, float *y)
{
    const float t = fmaxf(0.0f, fminf(1.0f, fraction));
    const float inverse = 1.0f - t;
    *x =
        inverse * inverse * inverse * DETAIL_ROUTE_START_X +
        3.0f * inverse * inverse * t * DETAIL_ROUTE_CONTROL_1_X +
        3.0f * inverse * t * t * DETAIL_ROUTE_CONTROL_2_X +
        t * t * t * DETAIL_ROUTE_END_X;
    *y =
        inverse * inverse * inverse * DETAIL_ROUTE_ENDPOINT_Y +
        3.0f * inverse * inverse * t * DETAIL_ROUTE_CONTROL_1_Y +
        3.0f * inverse * t * t * DETAIL_ROUTE_CONTROL_2_Y +
        t * t * t * DETAIL_ROUTE_ENDPOINT_Y;
}

static void detail_route_point(float fraction, int y_offset, int *x, int *y)
{
    float precise_x = 0.0f;
    float precise_y = 0.0f;
    detail_route_spline_point(fraction, &precise_x, &precise_y);
    *x = (int)lroundf(precise_x);
    *y = (int)lroundf(precise_y) + y_offset;
}

static void detail_route_rasterize_segment(float global_x0,
                                           float global_y0,
                                           float global_x1,
                                           float global_y1)
{
    const float x0 = global_x0 - DETAIL_ROUTE_MASK_X;
    const float y0 = global_y0 - DETAIL_ROUTE_MASK_Y;
    const float x1 = global_x1 - DETAIL_ROUTE_MASK_X;
    const float y1 = global_y1 - DETAIL_ROUTE_MASK_Y;
    const float vx = x1 - x0;
    const float vy = y1 - y0;
    const float length_squared = vx * vx + vy * vy;
    if (length_squared < 0.0001f) {
        return;
    }

    const float line_radius = 1.45f;
    const float antialias_width = 0.85f;
    int min_x = (int)floorf(fminf(x0, x1) - line_radius - antialias_width);
    int max_x = (int)ceilf(fmaxf(x0, x1) + line_radius + antialias_width);
    int min_y = (int)floorf(fminf(y0, y1) - line_radius - antialias_width);
    int max_y = (int)ceilf(fmaxf(y0, y1) + line_radius + antialias_width);
    min_x = min_x < 0 ? 0 : min_x;
    min_y = min_y < 0 ? 0 : min_y;
    max_x = max_x >= DETAIL_ROUTE_MASK_WIDTH ? DETAIL_ROUTE_MASK_WIDTH - 1 : max_x;
    max_y = max_y >= DETAIL_ROUTE_MASK_HEIGHT ? DETAIL_ROUTE_MASK_HEIGHT - 1 : max_y;

    for (int pixel_y = min_y; pixel_y <= max_y; pixel_y++) {
        for (int pixel_x = min_x; pixel_x <= max_x; pixel_x++) {
            const float sample_x = (float)pixel_x + 0.5f;
            const float sample_y = (float)pixel_y + 0.5f;
            float projection =
                ((sample_x - x0) * vx + (sample_y - y0) * vy) / length_squared;
            projection = fmaxf(0.0f, fminf(1.0f, projection));
            const float closest_x = x0 + projection * vx;
            const float closest_y = y0 + projection * vy;
            const float dx = sample_x - closest_x;
            const float dy = sample_y - closest_y;
            const float distance = sqrtf(dx * dx + dy * dy);
            const float coverage = fmaxf(
                0.0f,
                fminf(1.0f, (line_radius + antialias_width - distance) / antialias_width)
            );
            const uint8_t alpha = (uint8_t)lroundf(coverage * 255.0f);
            uint8_t *destination =
                &detail_route_mask_buffer[pixel_y * DETAIL_ROUTE_MASK_WIDTH + pixel_x];
            if (alpha > *destination) {
                *destination = alpha;
            }
        }
    }
}

static bool detail_route_mask_prepare(void)
{
    if (detail_route_mask_ready) {
        return true;
    }
    if (detail_route_mask_buffer == NULL) {
        detail_route_mask_buffer = heap_caps_malloc(
            DETAIL_ROUTE_MASK_BYTES,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (detail_route_mask_buffer == NULL) {
            detail_route_mask_buffer = heap_caps_malloc(
                DETAIL_ROUTE_MASK_BYTES,
                MALLOC_CAP_8BIT
            );
        }
    }
    if (detail_route_mask_buffer == NULL) {
        return false;
    }

    memset(detail_route_mask_buffer, 0, DETAIL_ROUTE_MASK_BYTES);
    float previous_x = 0.0f;
    float previous_y = 0.0f;
    detail_route_spline_point(0.0f, &previous_x, &previous_y);
    for (int segment = 1; segment <= DETAIL_ROUTE_SPLINE_SEGMENTS; segment++) {
        float x = 0.0f;
        float y = 0.0f;
        detail_route_spline_point(
            (float)segment / (float)DETAIL_ROUTE_SPLINE_SEGMENTS,
            &x,
            &y
        );
        detail_route_rasterize_segment(previous_x, previous_y, x, y);
        previous_x = x;
        previous_y = y;
    }

    memset(&detail_route_mask_image, 0, sizeof(detail_route_mask_image));
    detail_route_mask_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    detail_route_mask_image.header.cf = LV_COLOR_FORMAT_A8;
    detail_route_mask_image.header.w = DETAIL_ROUTE_MASK_WIDTH;
    detail_route_mask_image.header.h = DETAIL_ROUTE_MASK_HEIGHT;
    detail_route_mask_image.header.stride = DETAIL_ROUTE_MASK_WIDTH;
    detail_route_mask_image.data_size = DETAIL_ROUTE_MASK_BYTES;
    detail_route_mask_image.data = detail_route_mask_buffer;
    detail_route_mask_ready = true;
    return true;
}

static lv_obj_t *detail_route_spline(lv_obj_t *parent,
                                     lv_color_t color,
                                     lv_opa_t opacity)
{
    if (!detail_route_mask_prepare()) {
        return NULL;
    }

    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(image, &detail_route_mask_image);
    lv_obj_set_pos(image, DETAIL_ROUTE_MASK_X, DETAIL_ROUTE_MASK_Y);
    lv_obj_set_style_image_recolor(image, color, 0);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(image, opacity, 0);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}

static lv_obj_t *detail_route_curve_fallback(lv_obj_t *parent,
                                             lv_color_t color,
                                             int width,
                                             lv_opa_t opacity)
{
    int coordinates[MAX_POLYLINE_POINTS][2];
    for (int index = 0; index < MAX_POLYLINE_POINTS; index++) {
        detail_route_point(
            (float)index / (float)(MAX_POLYLINE_POINTS - 1),
            0,
            &coordinates[index][0],
            &coordinates[index][1]
        );
    }
    return polyline(parent,
                    coordinates,
                    MAX_POLYLINE_POINTS,
                    color,
                    width,
                    opacity);
}

static void detail_route_dot_animation(void *object, int32_t fraction_milli)
{
    int x = 0;
    int y = 0;
    detail_route_point((float)fraction_milli / 1000.0f, 0, &x, &y);
    lv_obj_set_pos((lv_obj_t *)object, x - 6, y - 6);
}

static float detail_phase_progress(detail_flight_phase_t phase)
{
    switch (phase) {
    case DETAIL_PHASE_CLIMBING:
        return 0.24f;
    case DETAIL_PHASE_DESCENDING:
        return 0.78f;
    case DETAIL_PHASE_CRUISING:
        return 0.52f;
    case DETAIL_PHASE_UNKNOWN:
    default:
        return 0.50f;
    }
}

static lv_color_t detail_phase_color(detail_flight_phase_t phase)
{
    switch (phase) {
    case DETAIL_PHASE_CLIMBING:
    case DETAIL_PHASE_DESCENDING:
        return lv_color_hex(0xe4862e);
    case DETAIL_PHASE_CRUISING:
        return lv_color_hex(0x24bd38);
    case DETAIL_PHASE_UNKNOWN:
    default:
        return lv_color_hex(0x66706a);
    }
}

static void detail_draw_route(const app_aircraft_t *aircraft,
                              bool animate,
                              bool show_marker,
                              float marker_progress)
{
    const detail_flight_phase_t phase = detail_phase_for_aircraft(aircraft);
    const lv_color_t color = detail_phase_color(phase);
    lv_obj_t *curve = detail_route_spline(screen_root, color, (lv_opa_t)238);
    const bool curve_is_image = curve != NULL;
    if (curve == NULL) {
        curve = detail_route_curve_fallback(screen_root, color, 3, (lv_opa_t)238);
    }
    if (animate && curve != NULL) {
        if (curve_is_image) {
            lv_obj_set_style_image_opa(curve, LV_OPA_TRANSP, 0);
        } else {
            lv_obj_set_style_line_opa(curve, LV_OPA_TRANSP, 0);
        }
        detail_start_animation(curve,
                               curve_is_image
                                   ? detail_image_opacity_animation
                                   : detail_line_opacity_animation,
                               LV_OPA_TRANSP,
                               238,
                               700,
                               100,
                               lv_anim_path_ease_out);
    }

    const char *origin =
        aircraft != NULL && aircraft->route_valid ? aircraft->origin_iata : "-";
    const char *destination =
        aircraft != NULL && aircraft->route_valid ? aircraft->destination_iata : "-";
    char origin_city_text[24] = {0};
    char destination_city_text[24] = {0};
    if (aircraft != NULL && aircraft->route_valid) {
        app_data_get_airport_city(aircraft->origin_iata,
                                  origin_city_text,
                                  sizeof(origin_city_text));
        app_data_get_airport_city(aircraft->destination_iata,
                                  destination_city_text,
                                  sizeof(destination_city_text));
    }
    detail_centered_label(screen_root,
                          origin,
                          &lv_font_montserrat_24,
                          lv_color_hex(0xf2f4f2),
                          22,
                          228,
                          72);
    detail_centered_label(screen_root,
                          destination,
                          &lv_font_montserrat_24,
                          lv_color_hex(0xf2f4f2),
                          372,
                          228,
                          72);
    if (origin_city_text[0] != '\0') {
        lv_obj_t *origin_city = detail_centered_label(screen_root,
                                                       origin_city_text,
                                                       &lv_font_montserrat_12,
                                                       lv_color_hex(0x8f9892),
                                                       0,
                                                       257,
                                                       116);
        lv_obj_set_style_text_letter_space(origin_city, 0, 0);
    }
    if (destination_city_text[0] != '\0') {
        lv_obj_t *destination_city = detail_centered_label(screen_root,
                                                            destination_city_text,
                                                            &lv_font_montserrat_12,
                                                            lv_color_hex(0x8f9892),
                                                            350,
                                                            257,
                                                            116);
        lv_obj_set_style_text_letter_space(destination_city, 0, 0);
    }

    if (!show_marker) {
        return;
    }

    const float target_fraction =
        marker_progress >= 0.0f
            ? fmaxf(0.0f, fminf(1.0f, marker_progress))
            : detail_phase_progress(phase);
    int marker_x = 0;
    int marker_y = 0;
    detail_route_point(animate ? 0.0f : target_fraction, 0, &marker_x, &marker_y);
    lv_obj_t *marker = dot(screen_root,
                           marker_x,
                           marker_y,
                           12,
                           lv_color_hex(0xffffff),
                           LV_OPA_COVER);
    if (marker != NULL) {
        lv_obj_set_style_transform_pivot_x(marker, 6, 0);
        lv_obj_set_style_transform_pivot_y(marker, 6, 0);
        if (animate) {
            detail_start_animation(marker,
                                   detail_route_dot_animation,
                                   0,
                                   (int32_t)lroundf(target_fraction * 1000.0f),
                                   1050,
                                   190,
                                   lv_anim_path_ease_in_out);
        }
        lv_anim_t pulse;
        lv_anim_init(&pulse);
        lv_anim_set_var(&pulse, marker);
        lv_anim_set_exec_cb(&pulse, detail_dot_scale_animation);
        lv_anim_set_values(&pulse, 226, 292);
        lv_anim_set_duration(&pulse, 920);
        lv_anim_set_delay(&pulse, animate ? 1200 : 200);
        lv_anim_set_reverse_duration(&pulse, 920);
        lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&pulse, lv_anim_path_ease_in_out);
        lv_anim_start(&pulse);
    }
}

static void tracked_detail_draw_flag(int endpoint_index,
                                     int center_x,
                                     int center_y)
{
    size_t flag_len = 0;
    uint32_t flag_revision = 0;
    const uint8_t *flag = app_remote_get_route_flag(
        endpoint_index,
        &flag_len,
        &flag_revision);
    if (flag == NULL || flag_len != APP_REMOTE_ROUTE_FLAG_BYTES) {
        return;
    }

    lv_image_dsc_t *descriptor =
        &tracked_route_flag_images[endpoint_index];
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor->header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor->header.flags = 0;
    descriptor->header.w = APP_REMOTE_ROUTE_FLAG_WIDTH;
    descriptor->header.h = APP_REMOTE_ROUTE_FLAG_HEIGHT;
    descriptor->header.stride = APP_REMOTE_ROUTE_FLAG_WIDTH * 2;
    descriptor->data_size = (uint32_t)flag_len;
    descriptor->data = flag;

    lv_obj_t *image = lv_image_create(screen_root);
    lv_image_set_src(image, descriptor);
    lv_obj_set_pos(
        image,
        center_x - APP_REMOTE_ROUTE_FLAG_WIDTH / 2,
        center_y - APP_REMOTE_ROUTE_FLAG_HEIGHT / 2);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_image_opa(image, LV_OPA_COVER, 0);
    (void)flag_revision;
}

static void tracked_detail_draw_route_flags(void)
{
    tracked_detail_draw_flag(0, 58, 202);
    tracked_detail_draw_flag(1, 408, 202);
}

static void detail_draw_metric(const lv_image_dsc_t *icon,
                               int center_x,
                               int top_y,
                               const char *value,
                               int rotation_degrees)
{
    lv_obj_t *image = lv_image_create(screen_root);
    lv_image_set_src(image, icon);
    lv_obj_set_pos(image, center_x - (int)icon->header.w / 2, top_y);
    lv_image_set_pivot(image, icon->header.w / 2, icon->header.h / 2);
    lv_image_set_rotation(image, rotation_degrees * 10);
    lv_image_set_antialias(image, true);
    lv_obj_set_style_image_recolor(image, lv_color_hex(radar_palette_active->text), 0);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(image, (lv_opa_t)220, 0);

    detail_centered_label(screen_root,
                          value,
                          &lv_font_montserrat_16,
                          lv_color_hex(0xf0f3f0),
                          center_x - 52,
                          top_y + 50,
                          104);
}

static void detail_draw_compass(int center_x,
                                int top_y,
                                const char *value,
                                int heading_degrees,
                                bool heading_valid)
{
    lv_obj_t *ring = lv_image_create(screen_root);
    lv_image_set_src(ring, &airspace_detail_metric_compass_ring);
    lv_obj_set_pos(ring,
                   center_x - (int)airspace_detail_metric_compass_ring.header.w / 2,
                   top_y);
    lv_obj_set_style_image_recolor(ring, lv_color_hex(radar_palette_active->main), 0);
    lv_obj_set_style_image_recolor_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(ring, (lv_opa_t)205, 0);

    lv_obj_t *arrow = lv_image_create(screen_root);
    lv_image_set_src(arrow, &airspace_detail_metric_compass_arrow);
    lv_obj_set_pos(arrow,
                   center_x - (int)airspace_detail_metric_compass_arrow.header.w / 2,
                   top_y);
    lv_image_set_pivot(arrow,
                       airspace_detail_metric_compass_arrow.header.w / 2,
                       airspace_detail_metric_compass_arrow.header.h / 2);
    lv_image_set_scale(arrow, 150);
    lv_image_set_rotation(arrow, heading_valid ? heading_degrees * 10 : 0);
    lv_image_set_antialias(arrow, true);
    lv_obj_set_style_image_recolor(arrow, lv_color_hex(radar_palette_active->text), 0);
    lv_obj_set_style_image_recolor_opa(arrow, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(arrow, heading_valid ? (lv_opa_t)240 : (lv_opa_t)85, 0);

    detail_centered_label(screen_root,
                          value,
                          &lv_font_montserrat_16,
                          lv_color_hex(0xf0f3f0),
                          center_x - 52,
                          top_y + 50,
                          104);
}

static void render_aircraft_detail(void)
{
    draw_shell();

    app_aircraft_t aircraft = selected_aircraft;
    if (!aircraft.valid) {
        label(screen_root, "target unavailable", &lv_font_montserrat_20, lv_color_hex(0xffb000),
              LV_ALIGN_CENTER, 0, -8);
        label(screen_root, "press button to exit", &lv_font_montserrat_14, lv_color_hex(0x7c8b95),
              LV_ALIGN_CENTER, 0, 30);
        return;
    }

    const bool animate_entry = detail_entry_animation_pending;

    uint32_t logo_revision = 0;
    lv_obj_t *logo = NULL;
    char logo_code[4] = {0};
    if (app_data_airline_logo_code(&aircraft, logo_code)) {
        logo = draw_airline_logo(screen_root,
                                 logo_code,
                                 CX,
                                 34,
                                 &logo_revision);
    }
    if (logo != NULL) {
        const bool animate_logo =
            detail_logo_revision_seen == 0 || detail_logo_revision_seen != logo_revision;
        if (animate_logo) {
            lv_obj_set_style_image_opa(logo, LV_OPA_TRANSP, 0);
            detail_start_animation(logo,
                                   detail_image_opacity_animation,
                                   LV_OPA_TRANSP,
                                   LV_OPA_COVER,
                                   680,
                                   120,
                                   lv_anim_path_ease_out);
        }
        detail_logo_revision_seen = logo_revision;
    } else if (aircraft.airline_name[0] != '\0') {
        detail_centered_label(screen_root,
                              aircraft.airline_name,
                              &lv_font_montserrat_14,
                              lv_color_hex(0x9aa39d),
                              83,
                              20,
                              300);
    }

    lv_obj_t *aircraft_image = draw_aircraft_detail_icon(screen_root, &aircraft, CX, 106);
    if (animate_entry && aircraft_image != NULL) {
        const int target_x = lv_obj_get_x(aircraft_image);
        const int start_x = -(int)lv_obj_get_width(aircraft_image) - 24;
        lv_obj_set_x(aircraft_image, start_x);
        lv_obj_set_style_image_opa(aircraft_image, (lv_opa_t)40, 0);
        detail_start_animation(aircraft_image,
                               detail_x_animation,
                               start_x,
                               target_x,
                               920,
                               80,
                               lv_anim_path_ease_out);
        detail_start_animation(aircraft_image,
                               detail_image_opacity_animation,
                               40,
                               LV_OPA_COVER,
                               620,
                               80,
                               lv_anim_path_ease_out);
    }

    char model[64];
    detail_aircraft_model_text(&aircraft, model, sizeof(model));
    detail_centered_label(screen_root,
                          model,
                          &lv_font_montserrat_14,
                          lv_color_hex(0x969f99),
                          53,
                          166,
                          360);

    const detail_flight_phase_t phase = detail_phase_for_aircraft(&aircraft);
    char route_signature[32];
    snprintf(route_signature,
             sizeof(route_signature),
             "%d:%s:%s:%d",
             aircraft.route_valid ? 1 : 0,
             aircraft.origin_iata,
             aircraft.destination_iata,
             (int)phase);
    const bool animate_route =
        animate_entry || strcmp(route_signature, detail_route_signature_seen) != 0;
    snprintf(detail_route_signature_seen,
             sizeof(detail_route_signature_seen),
             "%s",
             route_signature);
    detail_draw_route(
        &aircraft,
        animate_route,
        aircraft.route_progress_valid,
        aircraft.route_progress);

    if (app_data_aircraft_enrichment_pending(aircraft.icao24)) {
        lv_obj_t *pending = detail_centered_label(screen_root,
                                                   "UPDATING FLIGHT",
                                                   &lv_font_montserrat_12,
                                                   lv_color_hex(0x66706a),
                                                   143,
                                                   254,
                                                   180);
        lv_obj_set_style_text_letter_space(pending, 1, 0);
    }

    char display_label[12];
    aircraft_display_label(&aircraft, display_label, sizeof(display_label));
    detail_centered_label(screen_root,
                          display_label,
                          &lv_font_montserrat_24,
                          lv_color_hex(0xf6f7f6),
                          123,
                          274,
                          220);

    char altitude[24];
    char speed[24];
    const bool imperial = display_prefers_imperial();
    if (imperial) {
        snprintf(altitude,
                 sizeof(altitude),
                 "%d ft",
                 aircraft.altitude_ft);
        snprintf(speed,
                 sizeof(speed),
                 "%d mph",
                 (int)lroundf((float)aircraft.speed_kt * 1.150779f));
    } else {
        const int altitude_m =
            (int)lroundf((float)aircraft.altitude_ft * 0.3048f);
        const int speed_kmh =
            (int)lroundf((float)aircraft.speed_kt * 1.852f);
        snprintf(altitude, sizeof(altitude), "%d m", altitude_m);
        snprintf(speed, sizeof(speed), "%d km/h", speed_kmh);
    }
    detail_draw_metric(&airspace_detail_metric_altitude, 125, 326, altitude, 0);
    detail_draw_metric(&airspace_detail_metric_speed, 233, 326, speed, 0);
    detail_draw_compass(341,
                        326,
                        detail_compass_direction(&aircraft),
                        aircraft.track_valid ? aircraft.track_deg : 0,
                        aircraft.track_valid);

    const char *raw_identifier =
        aircraft.callsign[0] != '\0' && strcasecmp(aircraft.callsign, "UNKNOWN") != 0
            ? aircraft.callsign
            : aircraft.icao24;
    char opensky_identifier[32];
    snprintf(opensky_identifier,
             sizeof(opensky_identifier),
             "OPENSKY  %s",
             raw_identifier[0] != '\0' ? raw_identifier : "-");
    detail_centered_label(screen_root,
                          opensky_identifier,
                          &lv_font_montserrat_12,
                          lv_color_hex(0x657069),
                          93,
                          421,
                          280);

    detail_entry_animation_pending = false;
}

static void render_tracked_flight_detail(void)
{
    app_remote_status_t remote;
    app_remote_get_status(&remote);
    tracked_reconcile_terminal_snapshot(
        &remote,
        (int64_t)time(NULL));
    draw_shell();

    if (!remote.tracked_flight_valid) {
        label(
            screen_root,
            "NOTHING TO TRACK",
            &lv_font_montserrat_16,
            lv_color_hex(0xaeb7b1),
            LV_ALIGN_CENTER,
            0,
            0);
        return;
    }
    tracked_prepare_time_rotation(&remote);

    app_aircraft_t aircraft = {
        .valid = true,
        .metadata_valid = remote.tracked_flight_aircraft_type[0] != '\0',
        .route_valid =
            remote.tracked_flight_origin[0] != '\0' &&
            remote.tracked_flight_destination[0] != '\0',
        .altitude_ft = remote.tracked_flight_altitude_m > 0
            ? (int)lroundf((float)remote.tracked_flight_altitude_m / 0.3048f)
            : 0,
        .speed_kt = remote.tracked_flight_speed_kph > 0
            ? (int)lroundf((float)remote.tracked_flight_speed_kph / 1.852f)
            : 0,
        .track_deg = remote.tracked_flight_heading_deg,
        .track_valid = remote.tracked_flight_heading_valid,
        .vertical_rate_valid = remote.tracked_flight_phase[0] != '\0' &&
                               strcasecmp(remote.tracked_flight_phase, "unknown") != 0,
        .category = 4,
        .type_source = APP_AIRCRAFT_TYPE_SOURCE_FLIGHTAWARE,
    };
    if (strcasecmp(remote.tracked_flight_phase, "climbing") == 0) {
        aircraft.vertical_rate_mps = 4.0f;
    } else if (strcasecmp(remote.tracked_flight_phase, "descending") == 0) {
        aircraft.vertical_rate_mps = -4.0f;
    } else {
        aircraft.vertical_rate_mps = 0.0f;
    }
    snprintf(aircraft.callsign,
             sizeof(aircraft.callsign),
             "%.11s",
             remote.tracked_flight_number);
    snprintf(aircraft.typecode,
             sizeof(aircraft.typecode),
             "%.7s",
             remote.tracked_flight_aircraft_type);
    snprintf(aircraft.model,
             sizeof(aircraft.model),
             "%.15s",
             remote.tracked_flight_aircraft_type);
    snprintf(aircraft.airline_iata,
             sizeof(aircraft.airline_iata),
             "%.3s",
             remote.tracked_flight_airline_iata);
    snprintf(aircraft.airline_name,
             sizeof(aircraft.airline_name),
             "%.31s",
             remote.tracked_flight_airline_name);
    snprintf(aircraft.origin_iata,
             sizeof(aircraft.origin_iata),
             "%.4s",
             remote.tracked_flight_origin);
    snprintf(aircraft.destination_iata,
             sizeof(aircraft.destination_iata),
             "%.4s",
             remote.tracked_flight_destination);

    uint32_t logo_revision = 0;
    lv_obj_t *logo = NULL;
    if (aircraft.airline_iata[0] != '\0') {
        logo = draw_airline_logo(
            screen_root,
            aircraft.airline_iata,
            CX,
            34,
            &logo_revision);
    }
    if (logo == NULL && aircraft.airline_name[0] != '\0') {
        detail_centered_label(
            screen_root,
            aircraft.airline_name,
            &lv_font_montserrat_14,
            lv_color_hex(0x9aa39d),
            83,
            20,
            300);
    }

    draw_aircraft_detail_icon(
        screen_root,
        &aircraft,
        CX,
        106);

    char model[64];
    detail_aircraft_model_text(&aircraft, model, sizeof(model));
    detail_centered_label(
        screen_root,
        model,
        &lv_font_montserrat_14,
        lv_color_hex(0x969f99),
        53,
        166,
        360);

    const bool landed = tracked_is_terminal(
        &remote,
        (int64_t)time(NULL));
    const bool show_route_marker =
        !tracked_is_preflight(&remote) && !landed;
    detail_draw_route(
        &aircraft,
        false,
        show_route_marker,
        remote.route_progress);
    tracked_detail_draw_route_flags();
    if (landed) {
        lv_obj_t *landed_label = detail_centered_label(
            screen_root,
            "LANDED",
            &lv_font_montserrat_12,
            lv_color_hex(radar_palette_active->main),
            146,
            252,
            174);
        lv_obj_set_style_text_letter_space(
            landed_label,
            2,
            0);
    }
    /* The commercial flight number is the identity of this screen and stays
     * visible even when the secondary gate/time/ICAO layer is dismissed. */
    detail_centered_label(
        screen_root,
        remote.tracked_flight_number,
        &lv_font_montserrat_24,
        lv_color_hex(0xf6f7f6),
        123,
        268,
        220);
    if (tracked_detail_aux_visible) {
        detail_centered_label(
            screen_root,
            remote.tracked_flight_callsign[0] != '\0'
                ? remote.tracked_flight_callsign
                : "-",
            &lv_font_montserrat_12,
            lv_color_hex(0x77817b),
            123,
            296,
            220);
    }

    char altitude[24];
    char speed[24];
    const bool imperial = display_prefers_imperial();
    format_display_altitude_m(
        altitude,
        sizeof(altitude),
        remote.tracked_flight_altitude_m,
        imperial);
    format_display_speed_kph(
        speed,
        sizeof(speed),
        remote.tracked_flight_speed_kph,
        imperial);
    detail_draw_metric(&airspace_detail_metric_altitude, 125, 326, altitude, 0);
    detail_draw_metric(&airspace_detail_metric_speed, 233, 326, speed, 0);
    detail_draw_compass(
        341,
        326,
        detail_compass_direction(&aircraft),
        aircraft.track_deg,
        aircraft.track_valid);

    if (tracked_detail_aux_visible) {
        /* Draw each yellow shell and arrow once. Only the compact terminal or
         * gate value participates in the five-second page transition. */
        (void)tracked_draw_gate_sign_shell(
            screen_root,
            30,
            286,
            58,
            true,
            remote.tracked_flight_origin_terminal,
            remote.tracked_flight_origin_gate);
        (void)tracked_draw_gate_sign_shell(
            screen_root,
            378,
            286,
            58,
            false,
            remote.tracked_flight_destination_terminal,
            remote.tracked_flight_destination_gate);

        tracked_time_pair_t detail_pair;
        tracked_time_pair(&remote, 0, &detail_pair);
        tracked_time_page_base_y = 398;
        for (int page = 0; page < 2; page++) {
            lv_obj_t *time_page = tracked_time_page(
                screen_root,
                103,
                tracked_time_page_base_y,
                260,
                48,
                page == tracked_time_page_index
                    ? LV_OPA_COVER
                    : LV_OPA_TRANSP);
            tracked_time_pages[page] = time_page;

            tracked_gate_value_pages[0][page] =
                tracked_time_page(
                    screen_root,
                    51,
                    286,
                    37,
                    21,
                    page == tracked_time_page_index
                        ? LV_OPA_COVER
                        : LV_OPA_TRANSP);
            tracked_draw_gate_value_for_page(
                tracked_gate_value_pages[0][page],
                58,
                remote.tracked_flight_origin_terminal,
                remote.tracked_flight_origin_gate,
                page);
            tracked_gate_value_pages[1][page] =
                tracked_time_page(
                    screen_root,
                    399,
                    286,
                    37,
                    21,
                    page == tracked_time_page_index
                        ? LV_OPA_COVER
                        : LV_OPA_TRANSP);
            tracked_draw_gate_value_for_page(
                tracked_gate_value_pages[1][page],
                58,
                remote.tracked_flight_destination_terminal,
                remote.tracked_flight_destination_gate,
                page);

            detail_centered_label(
                time_page,
                detail_pair.title[page],
                &lv_font_montserrat_12,
                lv_color_hex(0x77817b),
                0,
                9,
                260);
            detail_centered_label(
                time_page,
                detail_pair.value[page],
                &lv_font_montserrat_14,
                tracked_time_value_color(
                    detail_pair.title[page],
                    remote.tracked_flight_delayed,
                    lv_color_hex(0xf1f4f2)),
                0,
                29,
                260);
        }
    }

    tracked_detail_entry_animation_pending = false;
}

static generated_scene_t current_generated_scene(void)
{
    if (current_screen == SCREEN_GMT) {
        return GENERATED_SCENE_GMT;
    }
    if (current_screen == SCREEN_SPACE) {
        if (space_iss_visible) {
            return GENERATED_SCENE_ISS;
        }
        if (space_moon_visible) {
            return GENERATED_SCENE_MOON;
        }
        return GENERATED_SCENE_SOLAR;
    }
    if (current_screen == SCREEN_LAUNCH) {
        switch (launch_view) {
        case LAUNCH_VIEW_MAP:
            return GENERATED_SCENE_LAUNCH_MAP;
        case LAUNCH_VIEW_QR:
            return GENERATED_SCENE_LAUNCH_QR;
        case LAUNCH_VIEW_MAIN:
        default:
            return GENERATED_SCENE_LAUNCH_MAIN;
        }
    }
    return GENERATED_SCENE_NONE;
}

static size_t release_generated_image(
    uint8_t **buffer,
    lv_image_dsc_t *descriptor,
    size_t bytes)
{
    if (descriptor != NULL && descriptor->data != NULL) {
        lv_image_cache_drop(descriptor);
        memset(descriptor, 0, sizeof(*descriptor));
    }
    if (buffer == NULL || *buffer == NULL) {
        return 0;
    }
    heap_caps_free(*buffer);
    *buffer = NULL;
    return bytes;
}

static void release_inactive_generated_artwork(
    generated_scene_t target)
{
    size_t released = 0;

    if (target != GENERATED_SCENE_SOLAR) {
        released += release_generated_image(
            &space_solar_snapshot_buffer,
            &space_solar_snapshot_image,
            SPACE_SOLAR_SNAPSHOT_BYTES);
    }
    if (target != GENERATED_SCENE_GMT) {
        released += release_generated_image(
            &gmt_snapshot_buffer,
            &gmt_snapshot_image,
            GMT_SNAPSHOT_BYTES);
        released += release_generated_image(
            &gmt_transition_snapshot_buffer,
            &gmt_transition_snapshot_image,
            GMT_SNAPSHOT_BYTES);
        gmt_snapshot_minute = -1;
        gmt_snapshot_location_key[0] = '\0';
        gmt_snapshot_secondary_key[0] = '\0';
        gmt_transition_snapshot_minute = -1;
        gmt_transition_snapshot_location_key[0] = '\0';
        gmt_transition_snapshot_secondary_key[0] = '\0';
        gmt_transition_snapshot_ready = false;
    }
    if (target != GENERATED_SCENE_ISS) {
        released += release_generated_image(
            &space_iss_night_buffer,
            &space_iss_night_image,
            ISS_SOLAR_OVERLAY_PIXELS);
        released += release_generated_image(
            &space_iss_terminator_buffer,
            &space_iss_terminator_image,
            ISS_SOLAR_OVERLAY_PIXELS);
    }
    if (target != GENERATED_SCENE_MOON) {
        released += release_generated_image(
            &space_moon_phase_buffer,
            &space_moon_phase_image,
            SPACE_MOON_PHASE_BYTES);
        for (int index = 0;
             index < SPACE_MOON_STRIP_COUNT;
             index++) {
            if (space_moon_strip_images[index].data != NULL) {
                lv_image_cache_drop(
                    &space_moon_strip_images[index]);
                memset(
                    &space_moon_strip_images[index],
                    0,
                    sizeof(space_moon_strip_images[index]));
            }
        }
        if (space_moon_strip_buffer != NULL) {
            heap_caps_free(space_moon_strip_buffer);
            space_moon_strip_buffer = NULL;
            released += SPACE_MOON_STRIP_BYTES;
        }
    }
    if (target != GENERATED_SCENE_LAUNCH_MAIN) {
        released += release_generated_image(
            &launch_gradient_ring_buffer,
            &launch_gradient_ring_image,
            (size_t)LAUNCH_GRADIENT_RING_SIZE *
                LAUNCH_GRADIENT_RING_SIZE);
        released += release_generated_image(
            &launch_outer_gradient_ring_buffer,
            &launch_outer_gradient_ring_image,
            (size_t)LAUNCH_OUTER_GRADIENT_RING_SIZE *
                LAUNCH_OUTER_GRADIENT_RING_SIZE);
    }

    if (released > 0) {
        ESP_LOGI(
            TAG,
            "Released %u KB of inactive generated artwork; PSRAM free=%uKB largest=%uKB",
            (unsigned)(released / 1024U),
            (unsigned)(
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) /
                1024U),
            (unsigned)(
                heap_caps_get_largest_free_block(
                    MALLOC_CAP_SPIRAM) /
                1024U));
    }
}

static void prepare_generated_scene(void)
{
    const generated_scene_t target = current_generated_scene();
    if (target == rendered_generated_scene) {
        return;
    }

    /*
     * Generated image descriptors point directly into their PSRAM buffers.
     * Delete the previous object tree and its image-cache entries before
     * reclaiming those buffers so a later screen can safely reuse the memory.
     */
    lv_obj_clean(lv_screen_active());
    release_inactive_generated_artwork(target);

    /*
     * These two scenes create a full-screen snapshot before draw_shell() has
     * a chance to trim file-backed artwork.  Free inactive file assets first
     * so the 424 KB snapshot allocation always has a contiguous block.
     */
    if (target == GENERATED_SCENE_SOLAR ||
        target == GENERATED_SCENE_GMT) {
        trim_space_asset_cache(NULL, 0);
    }
    rendered_generated_scene = target;
    space_asset_retry_attempts = 4;
    space_asset_retry_after_us = 0;
}

static void render_screen(void)
{
#ifndef AIRSPACE_DESKTOP_RENDERER
    if (startup_splash_visible) {
        if (render_startup_shutdown_splash()) {
            if (splash_transition == SPLASH_TRANSITION_IDLE) {
                begin_startup_splash_transition();
            }
            render_requested = false;
            return;
        }
        ESP_LOGW(TAG, "Startup artwork unavailable; continuing to Radar");
        startup_splash_visible = false;
        (void)bsp_display_brightness_set(
            splash_restore_brightness);
    }
#endif
    if (power_off_pending) {
#ifndef AIRSPACE_DESKTOP_RENDERER
        if (render_startup_shutdown_splash()) {
            if (splash_transition == SPLASH_TRANSITION_IDLE) {
                begin_shutdown_splash_transition();
            }
        } else {
            draw_shell();
            label(
                screen_root,
                "POWERING OFF",
                &lv_font_montserrat_24,
                lv_color_hex(0xf4f4f3),
                LV_ALIGN_CENTER,
                0,
                0);
        }
#else
        render_powering_off();
#endif
        render_requested = false;
        return;
    }
    if (power_menu_visible) {
        render_power_menu();
        render_requested = false;
        return;
    }
    if (dock_feedback_visible) {
        render_dock_feedback();
        render_requested = false;
        return;
    }
    if (aircraft_detail_active) {
        render_aircraft_detail();
        render_requested = false;
        return;
    }

    sync_radar_display_config();

    prepare_generated_scene();

    switch (current_screen) {
    case SCREEN_RADAR:
        render_radar();
        break;
    case SCREEN_TRACKED_FLIGHT:
        if (tracked_flight_view == TRACKED_FLIGHT_DETAIL) {
            render_tracked_flight_detail();
        } else {
            render_tracked_flight();
        }
        break;
    case SCREEN_WEATHER:
        render_weather();
        break;
    case SCREEN_SPACE:
        render_space();
        break;
    case SCREEN_LAUNCH:
        render_launch();
        break;
    case SCREEN_GMT:
        render_gmt();
        break;
    default:
        render_radar();
        break;
    }
    render_requested = false;
#ifndef AIRSPACE_DESKTOP_RENDERER
    if (splash_cleanup_pending) {
        release_startup_splash_image();
    }
    if (screen_transition == SCREEN_TRANSITION_RENDER_HIDDEN) {
        /* Force the complete destination frame into panel RAM while hidden.
         * Full-display transitions use the AMOLED brightness floor.  The GMT
         * transition recreates an opaque mask above only the inner dial so
         * its bezel remains continuously visible. */
        if (screen_transition_visual ==
            SCREEN_TRANSITION_GMT_INNER) {
            if (screen_transition_inner_mask != NULL &&
                lv_obj_is_valid(screen_transition_inner_mask)) {
                lv_obj_set_style_bg_opa(
                    screen_transition_inner_mask,
                    LV_OPA_COVER,
                    0);
                lv_obj_move_foreground(
                    screen_transition_inner_mask);
            } else if (!create_gmt_inner_transition_mask(
                           LV_OPA_COVER)) {
                screen_transition_visual =
                    SCREEN_TRANSITION_FULL_DISPLAY;
                const uint8_t floor_raw =
                    (uint8_t)lroundf(
                        fminf(
                            SCREEN_TRANSITION_FLOOR_PERCENT,
                            (float)screen_transition_target_brightness) *
                        255.0f / 100.0f);
                (void)app_display_brightness_raw_set(
                    floor_raw);
            }
        }
        lv_refr_now(NULL);
        screen_transition = SCREEN_TRANSITION_FADE_IN;
        screen_transition_started_us = esp_timer_get_time();
    }
#endif
    if (dock_mode_enabled && dock_waiting_for_screen_render &&
        !screen_transition_active()) {
        dock_waiting_for_screen_render = false;
        dock_deadline_us =
            esp_timer_get_time() +
            (int64_t)dock_visible_duration_seconds() * 1000000LL;
    }
}

static void update_radar_animation(void)
{
#if RADAR_SWEEP_ENABLED
    if (aircraft_detail_active ||
        current_screen != SCREEN_RADAR ||
        radar_range_menu_open ||
        !radar_wedge_visible ||
        radar_sweep_obj == NULL) {
        radar_sweep_epoch_us = 0;
        radar_sweep_last_tick_us = 0;
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if (radar_sweep_last_tick_us > 0) {
        const int64_t gap_us = now_us - radar_sweep_last_tick_us;
        radar_sweep_gap_total_us += (uint64_t)gap_us;
        radar_sweep_gap_count++;
        if (gap_us > radar_sweep_gap_max_us) {
            radar_sweep_gap_max_us = gap_us;
        }
    }
    radar_sweep_last_tick_us = now_us;
    if (radar_sweep_epoch_us == 0) {
        radar_sweep_epoch_us = now_us - (int64_t)((double)radar_sweep_angle_deg * 1000000.0 /
                                                  (double)RADAR_SWEEP_DEG_PER_SECOND);
    }

    const double elapsed_s = (double)(now_us - radar_sweep_epoch_us) / 1000000.0;
    float next_angle = fmodf((float)(elapsed_s * (double)RADAR_SWEEP_DEG_PER_SECOND), 360.0f);
    if (next_angle < 0.0f) {
        next_angle += 360.0f;
    }

    radar_invalidate_sweep(radar_sweep_angle_deg);

    radar_sweep_angle_deg = next_angle;
    radar_invalidate_sweep(radar_sweep_angle_deg);
#endif
}

#if RADAR_SWEEP_ENABLED
static void radar_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (startup_splash_visible || display_sleeping || power_off_pending ||
        power_menu_visible || dock_feedback_visible ||
        screen_transition_active()) {
        return;
    }
    update_radar_animation();
}

static void radar_aircraft_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (startup_splash_visible || display_sleeping || power_off_pending ||
        power_menu_visible || dock_feedback_visible ||
        screen_transition_active()) {
        return;
    }
    radar_update_aircraft_animation();
}
#endif

static void handle_health_log(void)
{
    if (++health_log_ticks < HEALTH_LOG_TICKS) {
        return;
    }
    health_log_ticks = 0;

    if (refresh_battery_status_cache() &&
        radar_range_menu_open &&
        radar_menu_page == 0) {
        refresh_radar_menu_overlay();
    }

    lv_mem_monitor_t lvmon;
    lv_mem_monitor(&lvmon);
    const uint32_t sweep_average_us = radar_sweep_gap_count > 0
        ? (uint32_t)(radar_sweep_gap_total_us / radar_sweep_gap_count)
        : 0;
    const uint32_t sweep_max_us = radar_sweep_gap_max_us > 0
        ? (uint32_t)radar_sweep_gap_max_us
        : 0;
    const UBaseType_t lvgl_stack_low_watermark =
        uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG,
             "health: int=%uKB largest=%uKB dma=%uKB psram=%uKB largest_psram=%uKB lv_stack_low=%uB lv_used=%u%% lv_frag=%u%% lv_free=%uKB lv_biggest=%uKB sweep_avg=%uus sweep_max=%uus plane_moves=%u",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024,
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024,
             (unsigned)lvgl_stack_low_watermark,
             lvmon.used_pct,
             lvmon.frag_pct,
             (unsigned)(lvmon.free_size / 1024),
             (unsigned)(lvmon.free_biggest_size / 1024),
             (unsigned)sweep_average_us,
             (unsigned)sweep_max_us,
             (unsigned)radar_aircraft_move_count);
    radar_sweep_gap_total_us = 0;
    radar_sweep_gap_count = 0;
    radar_sweep_gap_max_us = 0;
    radar_aircraft_move_count = 0;
}

static void handle_remote_updates(void)
{
    app_remote_status_t remote;
    app_remote_get_status(&remote);

    if (remote.revision != remote_revision_seen) {
        const bool style_changed =
            remote_style_seen >= 0 &&
            remote_style_seen != (int)remote.style;
        const bool map_changed =
            remote.map_revision !=
            remote_map_revision_seen;
        const bool radar_map_changed =
            remote.radar_map_revision !=
            remote_radar_map_revision_seen;
        remote_revision_seen = remote.revision;
        remote_map_revision_seen = remote.map_revision;
        remote_radar_map_revision_seen = remote.radar_map_revision;
        remote_style_seen = (int)remote.style;
        tracked_logo_revision_seen = 0;
        tracked_logo_poll_ticks = 0;
        if (remote.tracked_flight_valid &&
            remote.tracked_flight_airline_iata[0] != '\0') {
            app_data_request_airline_logo(remote.tracked_flight_airline_iata);
        }
        if (style_changed) {
            set_screen(
                screen_for_remote_style(remote.style));
            if (current_screen == SCREEN_TRACKED_FLIGHT) {
                tracked_flight_view =
                    TRACKED_FLIGHT_GLOBE;
                tracked_panels_visible = true;
                tracked_panels_entry_animation_pending =
                    true;
            } else if (current_screen == SCREEN_WEATHER) {
                app_config_t cfg;
                app_config_get(&cfg);
                snprintf(
                    weather_selected_key,
                    sizeof(weather_selected_key),
                    "%.7s",
                    cfg.location_key);
                app_data_set_weather_location_key(
                    weather_selected_key);
                weather_forecast_visible = false;
                weather_radar_visible = false;
                weather_radar_loading = false;
                weather_radar_playing = false;
                weather_radar_playback_index = -1;
                weather_radar_playback_step_us = 0;
                weather_entry_animation_pending = true;
                if (remote.style ==
                    APP_REMOTE_STYLE_WEATHER_RADAR) {
                    show_weather_radar();
                }
            } else if (current_screen == SCREEN_SPACE) {
                select_space_view(
                    remote.style == APP_REMOTE_STYLE_SPACE_ISS
                        ? SPACE_VIEW_ISS
                        : remote.style == APP_REMOTE_STYLE_SPACE_MOON
                            ? SPACE_VIEW_MOON
                            : SPACE_VIEW_SOLAR,
                    false);
            } else if (
                current_screen == SCREEN_LAUNCH) {
                if (remote.style ==
                    APP_REMOTE_STYLE_LAUNCH_MAP) {
                    launch_view = LAUNCH_VIEW_MAP;
                } else if (
                    remote.style ==
                    APP_REMOTE_STYLE_LAUNCH_QR) {
                    launch_view = LAUNCH_VIEW_QR;
                } else {
                    launch_view = LAUNCH_VIEW_MAIN;
                }
                launch_animation_ticks = 0;
                launch_countdown_second_seen = -1;
            }
        }
        if (style_changed ||
            current_screen ==
                SCREEN_TRACKED_FLIGHT ||
            current_screen ==
                SCREEN_LAUNCH ||
            (current_screen == SCREEN_RADAR &&
             radar_map_changed) ||
            (current_screen == SCREEN_TRACKED_FLIGHT &&
             map_changed)) {
            render_requested = true;
        }
        ESP_LOGI(TAG, "Remote update: style=%s map_rev=%lu radar_map_rev=%lu progress=%.2f",
                 app_remote_style_text(remote.style),
                 (unsigned long)remote.map_revision,
                 (unsigned long)remote.radar_map_revision,
                 remote.route_progress);
        return;
    }

    if (remote.map_revision != remote_map_revision_seen) {
        remote_map_revision_seen = remote.map_revision;
        if (current_screen == SCREEN_TRACKED_FLIGHT) {
            render_requested = true;
        }
    }

    if (remote.radar_map_revision != remote_radar_map_revision_seen) {
        remote_radar_map_revision_seen = remote.radar_map_revision;
        if (current_screen == SCREEN_RADAR) {
            render_requested = true;
        }
    }
}

static void handle_tracked_logo_refresh(void)
{
    if (current_screen != SCREEN_TRACKED_FLIGHT || ++tracked_logo_poll_ticks < 20) {
        return;
    }
    tracked_logo_poll_ticks = 0;

    app_remote_status_t remote;
    app_remote_get_status(&remote);
    if (!remote.tracked_flight_valid ||
        remote.tracked_flight_airline_iata[0] == '\0') {
        return;
    }

    size_t logo_len = 0;
    uint16_t logo_width = 0;
    uint16_t logo_height = 0;
    uint32_t logo_revision = 0;
    const uint8_t *logo = app_data_get_airline_logo(
        remote.tracked_flight_airline_iata,
        &logo_len,
        &logo_width,
        &logo_height,
        &logo_revision);
    if (logo == NULL ||
        logo_len == 0 ||
        logo_width == 0 ||
        logo_height == 0) {
        /*
         * The decoder intentionally keeps only one airline logo resident in
         * PSRAM. A nearby-aircraft detail may replace the tracked flight's
         * logo, so request it again from the persistent PNG cache instead of
         * leaving the tracker permanently blank.
         */
        app_data_request_airline_logo(
            remote.tracked_flight_airline_iata);
        return;
    }
    if (logo != NULL &&
        logo_len > 0 &&
        logo_width > 0 &&
        logo_height > 0 &&
        logo_revision != tracked_logo_revision_seen) {
        tracked_logo_revision_seen = logo_revision;
        render_requested = true;
    }
}

static bool find_pointer(lv_point_t *point, bool *pressed)
{
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
            continue;
        }
        *pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
        lv_indev_get_point(indev, point);
        return true;
    }

    *pressed = false;
    point->x = 0;
    point->y = 0;
    return false;
}

static void handle_input(void)
{
    const int64_t now_us = esp_timer_get_time();
    const axp_power_key_events_t top_events =
        take_top_power_button_events();
    if (top_events.press_edge) {
        top_button_held = true;
        top_button_pressed_us = now_us;
    }
    if (top_events.release_edge) {
        top_button_held = false;
    }
    const bool boot_pressed = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
    const bool boot_press_edge = boot_pressed && !boot_was_pressed;
    const bool boot_release_edge = !boot_pressed && boot_was_pressed;
    lv_point_t point = {0};
    bool pointer_pressed = false;
    (void)find_pointer(&point, &pointer_pressed);
    const bool touch_release_edge = !pointer_pressed && touch_was_pressed;

    if (display_sleeping) {
        const bool wake_requested =
            top_events.press_edge ||
            top_events.release_edge ||
            top_events.short_press ||
            top_events.long_press;
        if (wake_requested) {
            wake_display();
            top_release_guard = top_button_held;
            boot_release_guard = boot_pressed;
            touch_release_guard = pointer_pressed;
            boot_press_started_us = 0;
            top_button_pressed_us = 0;
        }
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        if (pointer_pressed) {
            touch_last_point = point;
        }
        return;
    }

    if (input_startup_guard) {
        if (input_startup_guard_until_us == 0) {
            input_startup_guard_until_us =
                now_us + INPUT_STARTUP_GUARD_US;
        }
        boot_press_started_us = 0;
        top_button_pressed_us = 0;
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        if (now_us >= input_startup_guard_until_us &&
            !top_button_held && !boot_pressed &&
            !pointer_pressed) {
            input_startup_guard = false;
            ESP_LOGI(TAG, "Input startup guard released");
        }
        return;
    }

    if (power_off_pending) {
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        return;
    }

    if (power_menu_visible) {
        if (power_menu_opening_release_guard &&
            top_events.release_edge) {
            power_menu_opening_release_guard = false;
        } else if (!power_menu_opening_release_guard &&
                   top_events.short_press) {
            dismiss_power_menu();
            boot_was_pressed = boot_pressed;
            touch_was_pressed = pointer_pressed;
            return;
        }

        if (touch_release_edge) {
            if (!touch_release_guard &&
                point_in_rect(touch_last_point, 55, 151, 356, 92)) {
                boot_was_pressed = boot_pressed;
                touch_was_pressed = pointer_pressed;
                enter_display_sleep();
                return;
            }
            if (!touch_release_guard &&
                point_in_rect(touch_last_point, 55, 267, 356, 92)) {
                boot_was_pressed = boot_pressed;
                touch_was_pressed = pointer_pressed;
                begin_power_off();
                return;
            }
            if (!touch_release_guard) {
                dismiss_power_menu();
            }
            touch_release_guard = false;
            boot_was_pressed = boot_pressed;
            touch_was_pressed = pointer_pressed;
            return;
        }
        if (pointer_pressed) {
            touch_last_point = point;
        }
        if (boot_pressed) {
            boot_release_guard = true;
        }
        boot_press_started_us = 0;
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        return;
    }

    if (dock_feedback_visible) {
        top_release_guard = top_release_guard || top_button_held;
        boot_release_guard = boot_release_guard || boot_pressed;
        touch_release_guard = touch_release_guard || pointer_pressed;
        boot_press_started_us = 0;
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        return;
    }

    if (dock_chord_locked) {
        if (!top_button_held && !boot_pressed) {
            dock_chord_locked = false;
            top_release_guard = false;
            boot_release_guard = false;
            top_button_pressed_us = 0;
            boot_press_started_us = 0;
        }
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        return;
    }

    if (top_release_guard || boot_release_guard || touch_release_guard) {
        if (top_release_guard && !top_button_held) {
            top_release_guard = false;
            top_button_pressed_us = 0;
        }
        if (boot_release_guard && !boot_pressed) {
            boot_release_guard = false;
        }
        if (touch_release_guard && !pointer_pressed) {
            touch_release_guard = false;
        }
        boot_press_started_us = 0;
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        return;
    }

    if (boot_press_edge && !boot_release_guard) {
        boot_press_started_us = now_us;
    }

    const bool chord_times_valid =
        top_button_pressed_us > 0 &&
        boot_press_started_us > 0 &&
        llabs(top_button_pressed_us - boot_press_started_us) <=
            DOCK_CHORD_WINDOW_US;
    const int64_t chord_stable_since =
        top_button_pressed_us > boot_press_started_us
            ? top_button_pressed_us
            : boot_press_started_us;
    if (!dock_chord_locked &&
        !top_release_guard && !boot_release_guard &&
        top_button_held && boot_pressed &&
        chord_times_valid &&
        now_us - chord_stable_since >= DOCK_CHORD_STABLE_US) {
        dock_chord_locked = true;
        top_release_guard = true;
        boot_release_guard = true;
        touch_release_guard = pointer_pressed;
        boot_press_started_us = 0;
        toggle_dock_mode();
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        return;
    }

    if (dock_mode_enabled) {
        /* Dock Mode is presentation-only.  The two-button exit chord is
         * handled above; consume every other button and touch action,
         * including the software PWR long-press menu.  The AXP2101 emergency
         * hardware shutdown remains available independently. */
        if (!top_button_held) {
            top_button_pressed_us = 0;
        }
        if (!boot_pressed) {
            boot_press_started_us = 0;
        }
        if (pointer_pressed) {
            touch_last_point = point;
        }
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        return;
    }

    if (!top_release_guard && top_events.long_press) {
        open_power_menu();
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        return;
    }

    if (screen_transition_active()) {
        /* Consume input during a fade so a release cannot navigate into a
         * second page while the transition is still completing. */
        if (!top_button_held) {
            top_button_pressed_us = 0;
        }
        if (!boot_pressed) {
            boot_press_started_us = 0;
        }
        if (pointer_pressed) {
            touch_last_point = point;
        }
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        return;
    }

    if (!top_release_guard && top_events.short_press) {
        top_button_pressed_us = 0;
        next_screen();
        boot_was_pressed = boot_pressed;
        touch_was_pressed = pointer_pressed;
        return;
    }

    if (boot_release_edge && !boot_release_guard) {
        const int64_t duration_us =
            boot_press_started_us > 0
                ? now_us - boot_press_started_us
                : 0;
        boot_press_started_us = 0;
        if (duration_us >= 650000) {
            handle_bottom_button_long_press();
            boot_was_pressed = boot_pressed;
            touch_was_pressed = pointer_pressed;
            return;
        }
        if (duration_us >= 30000) {
            handle_bottom_button_short_press();
            boot_was_pressed = boot_pressed;
            touch_was_pressed = pointer_pressed;
            return;
        }
    }
    boot_was_pressed = boot_pressed;

    if (pointer_pressed && !touch_was_pressed) {
        touch_down_point = point;
        touch_press_started_us = now_us;
        if (current_screen == SCREEN_RADAR &&
            radar_range_menu_open &&
            radar_menu_page == 2) {
            radar_brightness_drag_candidate = true;
            radar_brightness_drag_active = false;
            radar_brightness_drag_anchor_percent =
                radar_brightness_target_percent;
        }
    }

    if (pointer_pressed) {
        touch_last_point = point;
    }

    if (pointer_pressed && radar_brightness_drag_candidate) {
        const int dx = point.x - touch_down_point.x;
        const int dy = point.y - touch_down_point.y;
        const int abs_dx = abs(dx);
        const int abs_dy = abs(dy);
        if (!radar_brightness_drag_active &&
            abs_dy >= 4 && abs_dy > abs_dx) {
            radar_brightness_drag_active = true;
        }
        if (radar_brightness_drag_active) {
            /* A full comfortable swipe covers nearly the complete 10-100%
             * range, while every touch sample retains sub-percent detail. */
            set_radar_brightness_target(
                radar_brightness_drag_anchor_percent -
                (float)dy * 0.40f);
            touch_was_pressed = true;
            return;
        }
    }

    if (!pointer_pressed && touch_was_pressed &&
        radar_brightness_drag_active) {
        radar_brightness_drag_active = false;
        radar_brightness_drag_candidate = false;
        set_display_brightness_percent(
            (int)lroundf(radar_brightness_target_percent));
        touch_was_pressed = false;
        return;
    }
    if (!pointer_pressed) {
        radar_brightness_drag_candidate = false;
    }

    if (!pointer_pressed && touch_was_pressed) {
        const int dx = touch_last_point.x - touch_down_point.x;
        const int dy = touch_last_point.y - touch_down_point.y;
        const int abs_dx = abs(dx);
        const int abs_dy = abs(dy);
        const int64_t touch_duration_us =
            touch_press_started_us > 0
                ? now_us - touch_press_started_us
                : 0;
        touch_press_started_us = 0;

        if (aircraft_detail_active) {
            aircraft_detail_active = false;
            render_requested = true;
            touch_was_pressed = pointer_pressed;
            return;
        }

        if (current_screen == SCREEN_RADAR) {
            if (!radar_range_menu_open &&
                touch_down_point.y < 72 &&
                dy > 30 &&
                abs_dy > abs_dx) {
                radar_range_menu_open = true;
                radar_menu_page = 0;
                begin_screen_transition();
                touch_was_pressed = pointer_pressed;
                return;
            }
            if (radar_range_menu_open) {
                if (abs_dx > 44 && abs_dx > abs_dy) {
                    const int next_page = radar_menu_page + (dx < 0 ? 1 : -1);
                    const int bounded_page = next_page < 0
                        ? 0
                        : (next_page >= RADAR_MENU_PAGE_COUNT
                            ? RADAR_MENU_PAGE_COUNT - 1
                            : next_page);
                    if (bounded_page != radar_menu_page) {
                        radar_menu_page = bounded_page;
                        if (radar_menu_page == 3) {
                            app_data_request_api_validation();
                        }
                        begin_screen_transition();
                    }
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                if (abs_dx >= 28 || abs_dy >= 28) {
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                if (is_point_in_range_button(touch_last_point, &radar_menu_button)) {
                    radar_range_menu_open = false;
                    begin_screen_transition();
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                const int page_index = find_radar_page_button_at(touch_last_point);
                if (page_index >= 0) {
                    radar_menu_page = page_index;
                    if (radar_menu_page == 3) {
                        app_data_request_api_validation();
                    }
                    begin_screen_transition();
                    touch_was_pressed = pointer_pressed;
                    return;
                }


                if (radar_menu_page == 2 &&
                    is_point_in_range_button(
                        touch_last_point,
                        &radar_brightness_up_button)) {
                    app_config_t cfg;
                    app_config_get(&cfg);
                    set_display_brightness_percent(
                        (int)cfg.display_brightness_percent + 10);
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                if (radar_menu_page == 2 &&
                    is_point_in_range_button(
                        touch_last_point,
                        &radar_brightness_down_button)) {
                    app_config_t cfg;
                    app_config_get(&cfg);
                    set_display_brightness_percent(
                        (int)cfg.display_brightness_percent - 10);
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                const int range_button_index = find_radar_range_button_at(touch_last_point);
                if (range_button_index >= 0) {
                    set_radar_range_miles(radar_range_buttons[range_button_index].miles);
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                const int wifi_profile_button_index = find_radar_wifi_profile_button_at(touch_last_point);
                if (wifi_profile_button_index >= 0) {
                    set_active_wifi_profile(radar_wifi_profile_buttons[wifi_profile_button_index].profile);
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                const int location_button_index = find_radar_location_button_at(touch_last_point);
                if (location_button_index >= 0) {
                    set_radar_location_key(radar_location_buttons[location_button_index].key);
                    radar_range_menu_open = false;
                    set_screen(SCREEN_RADAR);
                    begin_screen_transition();
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                const int accent_button_index = find_radar_accent_button_at(touch_last_point);
                if (accent_button_index >= 0) {
                    set_radar_accent_color(radar_accent_buttons[accent_button_index].color);
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                if (is_point_in_labels_button(touch_last_point)) {
                    toggle_aircraft_labels();
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                if (is_point_in_wedge_button(touch_last_point)) {
                    toggle_radar_wedge();
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                const int weather_unit_index =
                    find_radar_weather_unit_button_at(
                        touch_last_point);
                if (weather_unit_index >= 0) {
                    set_weather_units(
                        (app_weather_units_t)weather_unit_index);
                    touch_was_pressed = pointer_pressed;
                    return;
                }

                touch_was_pressed = pointer_pressed;
                return;
            }

            if (abs_dx < 28 && abs_dy < 28) {
                const int aircraft_index = find_radar_target_at(touch_last_point);
                if (aircraft_index >= 0) {
                    show_aircraft_detail(aircraft_index);
                    touch_was_pressed = pointer_pressed;
                    return;
                }
            }
        }

        if (current_screen == SCREEN_WEATHER &&
            weather_radar_visible &&
            abs_dx < 28 &&
            abs_dy < 28) {
            const int range_index =
                find_weather_radar_range_button_at(
                    touch_last_point);
            if (range_index >= 0) {
                set_radar_range_miles(
                    weather_radar_range_buttons[
                        range_index].miles);
                touch_was_pressed = pointer_pressed;
                return;
            }

            const int center_dx =
                touch_last_point.x - CX;
            const int center_dy =
                touch_last_point.y - CY;
            if (center_dx * center_dx +
                    center_dy * center_dy <=
                WEATHER_RADAR_CENTER_HIT_RADIUS *
                    WEATHER_RADAR_CENTER_HIT_RADIUS) {
                const int frame_count =
                    weather_data_snapshot != NULL
                        ? weather_data_snapshot
                            ->weather_radar_frame_count
                        : 0;
                if (frame_count > 1) {
                    weather_radar_playing =
                        !weather_radar_playing;
                    if (weather_radar_playing) {
                        if (weather_radar_playback_index < 0 ||
                            weather_radar_playback_index >=
                                frame_count - 1) {
                            weather_radar_playback_index = 0;
                        }
                        weather_radar_playback_step_us =
                            esp_timer_get_time();
                    }
                    render_requested = true;
                }
                touch_was_pressed = pointer_pressed;
                return;
            }
        }

        if (current_screen == SCREEN_WEATHER &&
            abs_dy > 28 &&
            abs_dy > abs_dx) {
            if (!weather_radar_visible &&
                !weather_forecast_visible &&
                dy > 0 &&
                touch_down_point.y < CY) {
                weather_forecast_visible = true;
                begin_screen_transition();
                touch_was_pressed = pointer_pressed;
                return;
            }
            if (!weather_radar_visible &&
                !weather_forecast_visible &&
                dy < 0 &&
                touch_down_point.y > CY) {
                show_weather_radar();
                begin_screen_transition();
                touch_was_pressed = pointer_pressed;
                return;
            }
            if (dy > 0 &&
                touch_down_point.y < CY &&
                weather_radar_visible) {
                weather_radar_visible = false;
                weather_radar_loading = false;
                weather_radar_playing = false;
                weather_radar_playback_index = -1;
                weather_radar_playback_step_us = 0;
                begin_screen_transition();
                touch_was_pressed = pointer_pressed;
                return;
            }
            if (dy < 0 &&
                touch_down_point.y > CY &&
                weather_forecast_visible) {
                weather_forecast_visible = false;
                begin_screen_transition();
                touch_was_pressed = pointer_pressed;
                return;
            }
        }

        if (current_screen == SCREEN_TRACKED_FLIGHT &&
            abs_dy > 28 &&
            abs_dy > abs_dx) {
            app_remote_status_t tracked_status;
            app_remote_get_status(&tracked_status);
            tracked_reconcile_terminal_snapshot(
                &tracked_status,
                (int64_t)time(NULL));
            if (!tracked_status.tracked_flight_valid ||
                tracked_should_show_countdown(&tracked_status)) {
                /* The empty and scheduled-countdown states are deliberately
                 * single-page experiences. Consume the gesture so it cannot
                 * leak into main-page navigation underneath. */
                touch_was_pressed = pointer_pressed;
                return;
            }
            if (tracked_flight_view == TRACKED_FLIGHT_GLOBE) {
                tracked_flight_view = dy > 0
                    ? TRACKED_FLIGHT_FRAME
                    : TRACKED_FLIGHT_DETAIL;
                if (tracked_flight_view == TRACKED_FLIGHT_DETAIL) {
                    tracked_detail_entry_animation_pending = true;
                } else {
                    tracked_panels_visible = true;
                    tracked_panels_entry_animation_pending = true;
                }
            } else if (tracked_flight_view == TRACKED_FLIGHT_FRAME && dy < 0) {
                tracked_flight_view = TRACKED_FLIGHT_GLOBE;
                tracked_panels_visible = true;
                tracked_panels_entry_animation_pending = true;
            } else if (tracked_flight_view == TRACKED_FLIGHT_DETAIL && dy > 0) {
                tracked_flight_view = TRACKED_FLIGHT_GLOBE;
                tracked_panels_visible = true;
                tracked_panels_entry_animation_pending = true;
            }
            begin_screen_transition();
            touch_was_pressed = pointer_pressed;
            return;
        }

        if (current_screen == SCREEN_TRACKED_FLIGHT &&
            tracked_flight_view == TRACKED_FLIGHT_DETAIL &&
            abs_dx < 28 &&
            abs_dy < 28 &&
            (touch_last_point.x - CX) * (touch_last_point.x - CX) +
                    (touch_last_point.y - CY) * (touch_last_point.y - CY) <=
                100 * 100) {
            tracked_detail_aux_visible =
                !tracked_detail_aux_visible;
            render_requested = true;
            touch_was_pressed = pointer_pressed;
            return;
        }

        if (current_screen == SCREEN_TRACKED_FLIGHT &&
            tracked_flight_view != TRACKED_FLIGHT_DETAIL &&
            abs_dx < 28 &&
            abs_dy < 28 &&
            (touch_last_point.x - CX) * (touch_last_point.x - CX) +
                    (touch_last_point.y - CY) * (touch_last_point.y - CY) <=
                100 * 100) {
            if (tracked_panels_visible) {
                tracked_hide_panels();
                tracked_panels_visible = false;
            } else {
                tracked_panels_visible = true;
                tracked_panels_entry_animation_pending = true;
                render_requested = true;
            }
            touch_was_pressed = pointer_pressed;
            return;
        }

        if (current_screen == SCREEN_GMT &&
            clock_digital_mode &&
            abs_dx < 28 &&
            abs_dy < 28) {
            const int city_index =
                find_clock_city_button_at(
                    touch_last_point);
            if (city_index >= 0 &&
                clock_city_buttons[city_index].key != NULL) {
                snprintf(
                    clock_selected_key,
                    sizeof(clock_selected_key),
                    "%s",
                    clock_city_buttons[city_index].key);
                clock_bezel_uses_secondary = false;
                render_requested = true;
                touch_was_pressed = pointer_pressed;
                return;
            }
        }

        if (current_screen == SCREEN_GMT &&
            !clock_digital_mode &&
            abs_dx < 28 &&
            abs_dy < 28) {
            const int center_dx =
                touch_last_point.x - CX;
            const int center_dy =
                touch_last_point.y - CY;
            if (center_dx * center_dx +
                    center_dy * center_dy <=
                CLOCK_CROWN_TOGGLE_HIT_RADIUS *
                    CLOCK_CROWN_TOGGLE_HIT_RADIUS) {
                clock_crown_visible =
                    !clock_crown_visible;
                if (!clock_animate_live_crown(
                        clock_crown_visible)) {
                    render_requested = true;
                }
                touch_was_pressed =
                    pointer_pressed;
                return;
            }
        }

        if (current_screen == SCREEN_GMT &&
            abs_dy > 28 &&
            abs_dy > abs_dx) {
            if (!clock_digital_mode && dy > 0) {
                clock_digital_mode = true;
                begin_gmt_inner_transition();
            } else if (clock_digital_mode && dy < 0) {
                clock_digital_mode = false;
                clock_bezel_uses_secondary = false;
                begin_gmt_inner_transition();
            }
            touch_was_pressed = pointer_pressed;
            return;
        }

        if (current_screen == SCREEN_SPACE &&
            space_view == SPACE_VIEW_SOLAR &&
            abs_dx < 28 &&
            abs_dy < 28) {
            const int sun_dx = touch_last_point.x - CX;
            const int sun_dy = touch_last_point.y - CY;
            if (sun_dx * sun_dx + sun_dy * sun_dy <= 48 * 48) {
                space_planet_annotation_mode =
                    (uint8_t)(
                        (space_planet_annotation_mode + 1) %
                        3);
                space_solar_intro_active = false;
                render_requested = true;
                touch_was_pressed = pointer_pressed;
                return;
            }
        }

        if (current_screen == SCREEN_SPACE &&
            (space_view == SPACE_VIEW_ISS ||
             space_view == SPACE_VIEW_ECLIPSE) &&
            abs_dx < 28 &&
            abs_dy < 28) {
            const int center_dx =
                touch_last_point.x - CX;
            const int center_dy =
                touch_last_point.y - CY;
            if (center_dx * center_dx +
                    center_dy * center_dy <=
                SPACE_CITY_MARKER_HIT_RADIUS *
                    SPACE_CITY_MARKER_HIT_RADIUS) {
                if (space_view == SPACE_VIEW_ISS &&
                    touch_duration_us >=
                        SPACE_ISS_FLAGS_LONG_PRESS_US) {
                    toggle_iss_flags();
                    touch_was_pressed = pointer_pressed;
                    return;
                }
                space_city_marker_visible =
                    !space_city_marker_visible;
                render_requested = true;
                touch_was_pressed = pointer_pressed;
                return;
            }
            /* Outside the central 50%-diameter target, fall through to the
             * ordinary edge-tap navigation below. */
        }

        if (current_screen == SCREEN_SPACE &&
            abs_dy > 28 &&
            abs_dy > abs_dx) {
            if (space_view == SPACE_VIEW_SOLAR && dy > 0) {
                select_space_view(SPACE_VIEW_ECLIPSE, false);
                begin_screen_transition();
            } else if (
                (space_view == SPACE_VIEW_ECLIPSE ||
                 space_view == SPACE_VIEW_ISS) &&
                dy < 0) {
                select_space_view(SPACE_VIEW_SOLAR, false);
                begin_screen_transition();
            } else if (
                space_view == SPACE_VIEW_SOLAR && dy < 0) {
                select_space_view(SPACE_VIEW_MOON, false);
                begin_screen_transition();
            } else if (
                space_view == SPACE_VIEW_MOON && dy > 0) {
                select_space_view(SPACE_VIEW_SOLAR, false);
                begin_screen_transition();
            }
            touch_was_pressed = pointer_pressed;
            return;
        }

        if (current_screen == SCREEN_LAUNCH &&
            abs_dy > 28 &&
            abs_dy > abs_dx) {
            if (launch_view == LAUNCH_VIEW_MAIN) {
                launch_view = dy < 0
                    ? LAUNCH_VIEW_MAP
                    : LAUNCH_VIEW_QR;
            } else if (
                launch_view == LAUNCH_VIEW_MAP &&
                dy > 0) {
                launch_view = LAUNCH_VIEW_MAIN;
            } else if (
                launch_view == LAUNCH_VIEW_QR &&
                dy < 0) {
                launch_view = LAUNCH_VIEW_MAIN;
            }
            launch_animation_ticks = 0;
            launch_countdown_second_seen = -1;
            begin_screen_transition();
            touch_was_pressed = pointer_pressed;
            return;
        }

        if (abs_dx > 44 && abs_dx > abs_dy) {
            if (dx < 0) {
                previous_screen();
            } else {
                next_screen();
            }
        } else if (touch_down_point.x < 88) {
            previous_screen();
        } else if (touch_down_point.x > SCREEN_SIZE - 88) {
            next_screen();
        }
    }

    touch_was_pressed = pointer_pressed;
}

static bool weather_data_matches_selection(
    const app_data_status_t *data)
{
    return data != NULL &&
           data->weather_valid &&
           data->weather_location_key[0] != '\0' &&
           strcasecmp(
               data->weather_location_key,
               weather_selected_key) == 0;
}

static void update_weather_marker(
    lv_obj_t *marker,
    int x,
    int width,
    float normalized,
    bool valid)
{
    if (marker == NULL) {
        return;
    }
    if (normalized < 0.0f) {
        normalized = 0.0f;
    } else if (normalized > 1.0f) {
        normalized = 1.0f;
    }
    lv_obj_set_style_bg_opa(
        marker,
        valid ? LV_OPA_COVER : LV_OPA_TRANSP,
        0);
    if (valid) {
        lv_obj_set_x(
            marker,
            x + (int)lroundf(
                    normalized *
                    (float)(width - 1)));
    }
}

static bool update_weather_radar_in_place(
    const app_data_status_t *data)
{
    if (data == NULL ||
        !weather_radar_visible ||
        pushed_map_tile_objects[0] == NULL) {
        return false;
    }

    int frame_count =
        data->weather_radar_frame_count;
    if (frame_count >
        APP_WEATHER_RADAR_FRAME_COUNT) {
        frame_count =
            APP_WEATHER_RADAR_FRAME_COUNT;
    }
    const bool overlay_matches =
        data->weather_radar_overlay_valid &&
        strcasecmp(
            data->weather_radar_overlay_location_key,
            weather_selected_key) == 0 &&
        fabsf(
            data->weather_radar_overlay_radius_miles -
                radar_range_miles) < 0.1f;
    if (!overlay_matches || frame_count <= 0) {
        return false;
    }

    if (weather_radar_loading) {
        weather_radar_loading = false;
        return false;
    }

    int frame_index =
        weather_radar_playback_index >= 0
            ? weather_radar_playback_index
            : 0;
    if (frame_index >= frame_count) {
        frame_index = frame_count - 1;
    }
    size_t overlay_len = 0;
    uint32_t overlay_revision = 0;
    int frame_unix = 0;
    const uint8_t *overlay =
        app_data_get_weather_radar_overlay_frame(
            frame_index,
            &overlay_len,
            &overlay_revision,
            &frame_unix);
    if (overlay == NULL) {
        return false;
    }

    size_t map_len = 0;
    uint32_t map_revision = 0;
    int map_index =
        app_remote_radar_range_index_for_miles(
            radar_range_miles);
    const uint8_t *map =
        app_remote_get_radar_map_for_location(
            weather_selected_key,
            radar_range_miles,
            &map_len,
            &map_revision,
            &map_index);
    if (map == NULL) {
        return false;
    }

    padded_map_revision_seen = UINT32_MAX;
    map = prepare_padded_map_tiles(
        (uint8_t)(MAP_KIND_WEATHER_BASE +
                  map_index),
        map_len,
        map,
        map_revision);
    const uint8_t *composite =
        compose_weather_radar(
            (uint8_t *)map,
            overlay,
            overlay_len,
            overlay_revision,
            map_revision,
            map_index,
            frame_index);
    if (composite == NULL) {
        return false;
    }
    for (int tile = 0;
         tile < MAP_TILE_COUNT;
         tile++) {
        if (pushed_map_tile_objects[tile] != NULL) {
            lv_obj_invalidate(
                pushed_map_tile_objects[tile]);
        }
    }

    if (weather_radar_temperature_label != NULL) {
        if (weather_data_matches_selection(data)) {
            lv_label_set_text_fmt(
                weather_radar_temperature_label,
                "%d°",
                weather_display_temperature(data->weather_temperature));
        } else {
            lv_label_set_text(
                weather_radar_temperature_label,
                "-");
        }
    }
    if (weather_radar_time_label != NULL) {
        if (frame_index <= 0) {
            lv_label_set_text(
                weather_radar_time_label,
                "NOW");
        } else {
            lv_label_set_text_fmt(
                weather_radar_time_label,
                "+%d MIN",
                frame_index *
                    APP_WEATHER_RADAR_FORECAST_STEP_MINUTES);
        }
    }
    if (weather_radar_progress_fill != NULL &&
        weather_radar_progress_marker != NULL) {
        const int timeline_x1 = 151;
        const int timeline_x2 = 315;
        float progress = 0.0f;
        if (frame_count > 1) {
            progress =
                (float)frame_index /
                (float)(frame_count - 1);
        }
        const int marker_x =
            timeline_x1 +
            (int)lroundf(
                progress *
                (float)(timeline_x2 - timeline_x1));
        lv_obj_set_width(
            weather_radar_progress_fill,
            marker_x > timeline_x1
                ? marker_x - timeline_x1
                : 1);
        lv_obj_set_x(
            weather_radar_progress_marker,
            marker_x - 2);
    }
    return true;
}

static bool update_weather_values_in_place(
    const app_data_status_t *data)
{
    const bool valid =
        weather_data_matches_selection(data);
    if (weather_forecast_visible) {
        if (weather_forecast_day_labels[0] ==
            NULL) {
            return false;
        }
        for (int day = 0;
             day < APP_WEATHER_FORECAST_DAYS;
             day++) {
            const bool day_valid =
                valid &&
                data->forecast_day[day][0] !=
                    '\0';
            lv_label_set_text(
                weather_forecast_day_labels[day],
                day_valid
                    ? data->forecast_day[day]
                    : "-");
            if (day_valid) {
                lv_label_set_text_fmt(
                    weather_forecast_high_labels[day],
                    "%d°",
                    weather_display_temperature(data->forecast_high[day]));
                lv_label_set_text_fmt(
                    weather_forecast_low_labels[day],
                    "%d°",
                    weather_display_temperature(data->forecast_low[day]));
                lv_label_set_text_fmt(
                    weather_forecast_precip_labels[day],
                    "%d%%",
                    data->forecast_precip_percent[day]);
            } else {
                lv_label_set_text(
                    weather_forecast_high_labels[day],
                    "-");
                lv_label_set_text(
                    weather_forecast_low_labels[day],
                    "-");
                lv_label_set_text(
                    weather_forecast_precip_labels[day],
                    "-");
            }
        }
        return true;
    }
    if (weather_radar_visible) {
        return update_weather_radar_in_place(
            data);
    }
    if (valid && weather_entry_animation_pending) {
        /*
         * The first response must rebuild the condition artwork once, then
         * use the same zero-to-value animation as a cached page entry.
         * Later refreshes continue down the in-place path below.
         */
        return false;
    }
    if (weather_temperature_label == NULL ||
        weather_precipitation_label == NULL ||
        weather_humidity_label == NULL ||
        weather_wind_label == NULL ||
        weather_pressure_label == NULL ||
        weather_aqi_label == NULL ||
        weather_uv_label == NULL) {
        return false;
    }

    if (!valid) {
        lv_label_set_text(
            weather_temperature_label,
            "-");
        lv_label_set_text(
            weather_summary_label,
            "-");
        lv_label_set_text(
            weather_precipitation_label,
            "-");
        lv_label_set_text(
            weather_humidity_label,
            "-");
        lv_label_set_text(
            weather_wind_label,
            "-");
        lv_label_set_text(
            weather_pressure_label,
            "-");
        lv_label_set_text(
            weather_aqi_label,
            "-");
        lv_label_set_text(
            weather_uv_label,
            "-");
        for (int index = 0; index < 4; index++) {
            update_weather_marker(
                weather_scale_markers[index],
                index % 2 == 0 ? 74 : 252,
                140,
                0.0f,
                false);
        }
        return true;
    }

    const int display_pressure =
        weather_display_pressure(data->weather_pressure_hpa);
    snprintf(
        weather_wind_direction_text,
        sizeof(weather_wind_direction_text),
        "%s",
        weather_cardinal_direction(
            data->weather_wind_deg));
    weather_temperature_animation(
        weather_temperature_label,
        weather_display_temperature(data->weather_temperature));
    lv_label_set_text(
        weather_summary_label,
        data->weather_summary);
    weather_precipitation_animation(
        weather_precipitation_label,
        data->weather_precipitation_tenths_mm);
    weather_humidity_animation(
        weather_humidity_label,
        data->weather_humidity_percent);
    weather_wind_animation(
        weather_wind_label,
        data->weather_wind_kt);
    weather_pressure_animation(
        weather_pressure_label,
        display_pressure);
    if (data->weather_air_quality_valid) {
        weather_aqi_animation(
            weather_aqi_label,
            data->weather_aqi);
        weather_uv_animation(
            weather_uv_label,
            data->weather_uv_tenths);
    } else {
        lv_label_set_text(
            weather_aqi_label,
            "-");
        lv_label_set_text(
            weather_uv_label,
            "-");
    }
    update_weather_marker(
        weather_scale_markers[0],
        74,
        140,
        (float)data->weather_wind_kt /
            40.0f,
        true);
    update_weather_marker(
        weather_scale_markers[1],
        252,
        140,
        weather_pressure_normalized(data->weather_pressure_hpa),
        data->weather_pressure_hpa > 0);
    update_weather_marker(
        weather_scale_markers[2],
        74,
        140,
        (float)data->weather_aqi / 200.0f,
        data->weather_air_quality_valid);
    update_weather_marker(
        weather_scale_markers[3],
        252,
        140,
        (float)data->weather_uv_tenths /
            110.0f,
        data->weather_air_quality_valid);
    return true;
}

static void handle_data_refresh(void)
{
    if (++data_poll_ticks < 20) {
        return;
    }
    data_poll_ticks = 0;

    const app_data_status_t *data = ui_data_status();
    if (data != NULL &&
        data->api_health_revision != api_health_revision_seen) {
        api_health_revision_seen = data->api_health_revision;
        if (current_screen == SCREEN_RADAR &&
            radar_range_menu_open &&
            radar_menu_page == 3) {
            refresh_radar_menu_overlay();
        }
    }
    if (data == NULL ||
        data->fetched_unix <= 0 ||
        data->fetched_unix == data_fetched_unix_seen) {
        return;
    }

    data_fetched_unix_seen = data->fetched_unix;
    if (aircraft_detail_active && selected_aircraft.icao24[0] != '\0') {
        for (int i = 0;
             i < data->aircraft_count && i < APP_MAX_AIRCRAFT;
             i++) {
            if (strcasecmp(
                    data->aircraft[i].icao24,
                    selected_aircraft.icao24) == 0) {
                selected_aircraft = data->aircraft[i];
                char logo_code[4] = {0};
                if (app_data_airline_logo_code(&selected_aircraft, logo_code)) {
                    app_data_request_airline_logo(logo_code);
                }
                render_requested = true;
                break;
            }
        }
    }
    if (current_screen == SCREEN_WEATHER) {
        if (!update_weather_values_in_place(
                data)) {
            render_requested = true;
        }
    } else if (
        current_screen == SCREEN_SPACE &&
        space_iss_visible &&
        !space_eclipse_visible) {
        render_requested = true;
    }
}

static void handle_space_animation(void)
{
    if (current_screen != SCREEN_SPACE || render_requested) {
        space_animation_ticks = 0;
        return;
    }
    if (space_moon_visible) {
        return;
    }
    space_animation_ticks++;
    update_space_city_marker_pulse();
    if (space_iss_visible && space_eclipse_visible) {
        return;
    }
    if (!space_iss_visible) {
        if (!space_planet_targets_valid) {
            const time_t now = time(NULL);
            if (space_ephemeris_time_is_valid(now)) {
                update_space_planet_targets();
                space_solar_intro_active = true;
                space_solar_intro_start_us = 0;
            } else {
                return;
            }
        }
        if (space_solar_intro_active &&
            space_animation_ticks >= 1) {
            space_animation_ticks = 0;
            update_solar_planets();
            update_solar_earth_glow();
        } else if (
            !space_solar_intro_active &&
            space_animation_ticks >= 2) {
            space_animation_ticks = 0;
            const time_t now = time(NULL);
            if (space_ephemeris_time_is_valid(now) &&
                (int64_t)now / 3600 !=
                    space_orbit_hour_seen) {
                update_space_planet_targets();
                update_solar_planets();
            }
            update_solar_earth_glow();
        }
        return;
    }
    if (space_animation_ticks >= 20) {
        space_animation_ticks = 0;
        update_iss_marker();
    }
}

static void handle_launch_animation(void)
{
    if (current_screen != SCREEN_LAUNCH ||
        render_requested) {
        launch_animation_ticks = 0;
        launch_countdown_second_seen = -1;
        return;
    }

    launch_animation_ticks++;
    time_t now = 0;
    time(&now);
    if (launch_countdown_label != NULL &&
        launch_countdown_second_seen !=
            (int64_t)now) {
        app_remote_status_t remote;
        (void)launch_get_selected_status(&remote);
        char countdown[40];
        launch_format_countdown(
            &remote,
            now,
            countdown,
            sizeof(countdown));
        launch_set_countdown_text(countdown);
        launch_countdown_second_seen =
            (int64_t)now;
    }

    const float wave =
        0.5f +
        0.5f *
            sinf(
                (float)launch_animation_ticks *
                0.095f);
    if (launch_view == LAUNCH_VIEW_MAIN) {
        if (launch_inner_ring != NULL) {
            app_remote_status_t remote;
            (void)launch_get_selected_status(&remote);
            const int64_t seconds_until_launch =
                remote.launch_net_unix -
                (int64_t)now;
            const bool final_five_minutes =
                seconds_until_launch >= 0 &&
                seconds_until_launch <= 300;
            const bool final_hour =
                seconds_until_launch >= 0 &&
                seconds_until_launch <= 3600;
            const lv_color_t pulse_color =
                final_five_minutes
                    ? lv_color_hex(0xff3b30)
                    : lv_color_hex(0xd8d0c4);
            if (launch_inner_ring_uses_gradient) {
                lv_obj_set_style_image_recolor(
                    launch_inner_ring,
                    pulse_color,
                    0);
                lv_obj_set_style_image_opa(
                    launch_inner_ring,
                    final_hour
                        ? (lv_opa_t)(
                              145 + 82 * wave)
                        : (lv_opa_t)185,
                    0);
                lv_image_set_scale(
                    launch_inner_ring,
                    final_hour
                        ? LAUNCH_GRADIENT_RING_SCALE +
                              (int)lroundf(
                                  8.0f * wave)
                        : LAUNCH_GRADIENT_RING_SCALE);
            } else {
                lv_obj_set_style_border_color(
                    launch_inner_ring,
                    pulse_color,
                    0);
                lv_obj_set_style_border_opa(
                    launch_inner_ring,
                    final_hour
                        ? (lv_opa_t)(
                              132 + 72 * wave)
                        : (lv_opa_t)162,
                    0);
            }
        }
    } else if (
        launch_view == LAUNCH_VIEW_MAP) {
        if (launch_map_marker != NULL) {
            lv_obj_set_style_bg_opa(
                launch_map_marker,
                (lv_opa_t)(
                    175 + 80 * wave),
                0);
        }
        if (launch_map_pad_glow != NULL) {
            const int size =
                24 + (int)lroundf(12.0f * wave);
            lv_obj_set_size(
                launch_map_pad_glow,
                size,
                size);
            lv_obj_set_pos(
                launch_map_pad_glow,
                CX - size / 2,
                CY - size / 2);
            lv_obj_set_style_bg_opa(
                launch_map_pad_glow,
                (lv_opa_t)(28 + 52 * wave),
                0);
            lv_obj_set_style_shadow_opa(
                launch_map_pad_glow,
                (lv_opa_t)(60 + 70 * wave),
                0);
        }
    }
}

static void handle_weather_radar_playback(void)
{
    if (dock_weather_radar_autoplay_pending &&
        dock_mode_enabled &&
        current_screen == SCREEN_WEATHER &&
        weather_radar_visible &&
        weather_data_snapshot != NULL &&
        weather_data_snapshot->weather_radar_frame_count > 1) {
        weather_radar_playing = true;
        weather_radar_playback_index = 0;
        weather_radar_playback_step_us = esp_timer_get_time();
        dock_weather_radar_autoplay_pending = false;
        render_requested = true;
    }
    if (current_screen != SCREEN_WEATHER ||
        !weather_radar_visible ||
        !weather_radar_playing ||
        weather_data_snapshot == NULL) {
        return;
    }

    int frame_count =
        weather_data_snapshot->weather_radar_frame_count;
    if (frame_count > APP_WEATHER_RADAR_FRAME_COUNT) {
        frame_count = APP_WEATHER_RADAR_FRAME_COUNT;
    }
    if (frame_count <= 1) {
        weather_radar_playing = false;
        weather_radar_playback_index = -1;
        weather_radar_playback_step_us = 0;
        render_requested = true;
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if (weather_radar_playback_step_us == 0) {
        weather_radar_playback_step_us = now_us;
        return;
    }
    if (now_us - weather_radar_playback_step_us <
        (int64_t)WEATHER_RADAR_PLAYBACK_FRAME_MS * 1000) {
        return;
    }

    weather_radar_playback_step_us = now_us;
    weather_radar_playback_index++;
    if (weather_radar_playback_index >= frame_count) {
        weather_radar_playback_index = 0;
    }
    if (!update_weather_radar_in_place(
            ui_data_status())) {
        render_requested = true;
    }
}

static void handle_clock_refresh(void)
{
    if (current_screen != SCREEN_GMT) {
        gmt_minute_seen = -1;
        gmt_live_hands_second_seen = -1;
        return;
    }
    if (gmt_secondary_transition_active) {
        return;
    }

    time_t now = 0;
    time(&now);
    if (now <= 0) {
        return;
    }

    const int minute = (int)(now / 60);
    if (gmt_minute_seen >= 0 && minute != gmt_minute_seen) {
        render_requested = true;
    } else if (
        !clock_digital_mode &&
        gmt_live_hands_layer != NULL &&
        gmt_live_hands_second_seen !=
            (int64_t)now) {
        app_config_t config;
        app_config_get(&config);
        const app_location_preset_t *configured =
            app_location_preset_for_config(
                &config);
        const char *active_key =
            clock_active_key(configured);
        const char *secondary_key =
            clock_resolved_secondary_key(
                &config,
                active_key);
        clock_update_live_hands(
            now,
            active_key,
            secondary_key,
            true);
    }
    gmt_minute_seen = minute;
}

static void handle_power_and_dock_timers(void)
{
    const int64_t now_us = esp_timer_get_time();

    if (power_off_pending &&
        power_off_deadline_us > 0 &&
        now_us >= power_off_deadline_us) {
        power_off_deadline_us = 0;
#ifndef AIRSPACE_DESKTOP_RENDERER
        stop_splash_brightness_timer();
        splash_transition = SPLASH_TRANSITION_IDLE;
        (void)bsp_display_brightness_set(0);
        const esp_err_t panel_result =
            app_display_panel_on_off(false);
        if (panel_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "CO5300 display-off before shutdown failed: %s",
                esp_err_to_name(panel_result));
        }
#endif
        lv_refr_now(NULL);
        if (!axp2101_software_power_off()) {
            ESP_LOGE(TAG, "AXP2101 software power-off command failed");
            power_off_pending = false;
#ifndef AIRSPACE_DESKTOP_RENDERER
            (void)app_display_panel_on_off(true);
            (void)bsp_display_brightness_set(
                splash_shutdown_start_brightness);
            app_data_set_paused(false);
            splash_cleanup_pending = true;
#endif
            render_requested = true;
        }
        return;
    }

    if (power_menu_visible &&
        power_menu_deadline_us > 0 &&
        now_us >= power_menu_deadline_us) {
        dismiss_power_menu();
    }

    if (dock_feedback_visible &&
        dock_feedback_deadline_us > 0 &&
        now_us >= dock_feedback_deadline_us) {
        const bool was_on = dock_feedback_on;
        dock_feedback_visible = false;
        dock_feedback_deadline_us = 0;
        if (was_on && dock_mode_enabled) {
            dock_activate_step(0);
        } else {
            render_requested = true;
        }
    }

    if (dock_mode_enabled &&
        !dock_feedback_visible &&
        !dock_waiting_for_screen_render &&
        dock_deadline_us > 0 &&
        now_us >= dock_deadline_us) {
        const int count = dock_sequence_count();
        if (count > 0) {
            dock_activate_step((dock_sequence_index + 1) % count);
        }
    }
}

static void app_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    handle_input();
    if (display_sleeping) {
        return;
    }
#ifndef AIRSPACE_DESKTOP_RENDERER
    if (startup_splash_visible) {
        /* The artwork is already in the framebuffer.  Brightness is the
         * only animated property, so ignore unrelated redraw requests. */
        render_requested = false;
        return;
    }
#endif
    handle_power_and_dock_timers();
    if (power_off_pending || power_menu_visible ||
        dock_feedback_visible) {
        if (render_requested) {
            render_screen();
        }
        return;
    }
    if (screen_transition_active()) {
        if (render_requested) {
            render_screen();
        }
        return;
    }
    handle_remote_updates();
    handle_data_refresh();
    handle_tracked_logo_refresh();
    update_tracked_aircraft_animation();
    handle_tracked_time_rotation();
    handle_clock_refresh();
    handle_space_animation();
    handle_launch_animation();
    handle_weather_radar_playback();
    handle_health_log();
    if (space_asset_retry_after_us > 0 &&
        esp_timer_get_time() >= space_asset_retry_after_us) {
        space_asset_retry_after_us = 0;
        if (space_asset_retry_attempts > 0) {
            space_asset_retry_attempts--;
            render_requested = true;
        }
    }
    if (gmt_secondary_transition_finish_pending) {
        gmt_secondary_transition_finish_pending =
            false;
        gmt_secondary_transition_active =
            false;
        gmt_secondary_roll_old = NULL;
        gmt_secondary_roll_new = NULL;
        render_requested = true;
    }
    if (++status_refresh_ticks >= FULL_REFRESH_TICKS) {
        status_refresh_ticks = 0;
        if (current_screen != SCREEN_TRACKED_FLIGHT &&
            current_screen != SCREEN_RADAR &&
            current_screen != SCREEN_WEATHER) {
            render_requested = true;
        }
    }
    if (render_requested &&
        !(current_screen == SCREEN_GMT &&
          gmt_secondary_transition_active) &&
        !space_map_overlay_transition_active) {
        if (!refresh_space_map_overlay()) {
            render_screen();
        }
    }
}

#ifndef AIRSPACE_DESKTOP_RENDERER
void app_main(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;
    esp_chip_info(&chip_info);
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size));

    ESP_LOGI(TAG, "CPU cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "Flash: %lu MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "Internal heap: %u KB", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    ESP_LOGI(TAG, "PSRAM total: %u KB", heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024);
    ESP_LOGI(TAG, "PSRAM free: %u KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);
    app_config_init_defaults();
    ESP_ERROR_CHECK(app_config_load());
    app_config_t boot_config;
    app_config_get(&boot_config);
    splash_restore_brightness =
        boot_config.display_brightness_percent >= 10 &&
                boot_config.display_brightness_percent <= 100
            ? boot_config.display_brightness_percent
            : 100;
    display_sleep_brightness = splash_restore_brightness;
    screen_transition_target_brightness = splash_restore_brightness;

    configure_boot_button();
    boot_was_pressed = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
    input_startup_guard_until_us =
        esp_timer_get_time() + INPUT_STARTUP_GUARD_US;
    bsp_display_cfg_t display_config = {
        .lv_adapter_cfg =
            ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation =
            ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode =
            ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
        },
    };
    display_config.lv_adapter_cfg.task_stack_size =
        LVGL_TASK_STACK_SIZE;
    bsp_display_start_with_config(
        &display_config);
    (void)bsp_display_brightness_set(0);
    startup_splash_visible = true;
    splash_transition = SPLASH_TRANSITION_IDLE;
    input_startup_guard_until_us =
        esp_timer_get_time() +
        SPLASH_STARTUP_HOLD_US +
        SPLASH_STARTUP_RAMP_US +
        250000LL;
    configure_top_power_button();
    /* Build the small, reusable detail-route mask before it can block input. */
    (void)detail_route_mask_prepare();

    bsp_display_lock(-1);
    render_screen();
    /* Commit the black-backed startup artwork before allowing the AMOLED's
     * brightness command through. This prevents panel RAM or an incomplete
     * first framebuffer from flashing during CO5300 initialization. */
    lv_refr_now(NULL);
    panel_boot_brightness_guard = false;
    lv_timer_create(app_timer_cb, UI_TIMER_MS, NULL);
#if RADAR_SWEEP_ENABLED
    lv_timer_create(radar_timer_cb, RADAR_SWEEP_TIMER_MS, NULL);
    lv_timer_create(radar_aircraft_timer_cb, RADAR_AIRCRAFT_TIMER_MS, NULL);
#endif
    bsp_display_unlock();

    app_network_start();
    app_remote_start();
    app_data_start();
    app_config_t loaded_config;
    app_config_get(&loaded_config);
    radar_range_miles = (float)loaded_config.radar_range_miles;
    app_data_set_aircraft_radius_miles(radar_range_miles);
}
#else

static lv_display_t *desktop_display;
static uint8_t *desktop_framebuffer;
static char desktop_renderer_error[160];

static uint32_t desktop_tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static void desktop_flush(
    lv_display_t *display,
    const lv_area_t *area,
    uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

bool airspace_renderer_initialize(const char *asset_root)
{
    if (desktop_display != NULL) {
        return true;
    }

    desktop_renderer_error[0] = '\0';
    airspace_desktop_set_asset_root(asset_root);
    lv_init();
    lv_tick_set_cb(desktop_tick_ms);

    desktop_display = lv_display_create(SCREEN_SIZE, SCREEN_SIZE);
    if (desktop_display == NULL) {
        snprintf(
            desktop_renderer_error,
            sizeof(desktop_renderer_error),
            "Could not create the LVGL display");
        return false;
    }

    const size_t framebuffer_bytes =
        (size_t)SCREEN_SIZE * SCREEN_SIZE * 2U;
    desktop_framebuffer = calloc(1, framebuffer_bytes);
    if (desktop_framebuffer == NULL) {
        snprintf(
            desktop_renderer_error,
            sizeof(desktop_renderer_error),
            "Could not allocate the LVGL framebuffer");
        return false;
    }

    lv_display_set_color_format(
        desktop_display,
        LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(
        desktop_display,
        desktop_framebuffer,
        NULL,
        framebuffer_bytes,
        LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(
        desktop_display,
        desktop_flush);
    lv_display_set_default(desktop_display);

    screen_root = lv_screen_active();
    sync_radar_display_config();
    render_requested = true;
    render_screen();
    lv_refr_now(desktop_display);
    return true;
}

void airspace_renderer_set_page(int page)
{
    aircraft_detail_active = false;
    radar_range_menu_open = false;
    power_menu_visible = false;
    dock_feedback_visible = false;
    power_off_pending = false;
    weather_forecast_visible = false;
    weather_radar_visible = false;
    space_view = SPACE_VIEW_SOLAR;
    space_city_marker_visible = false;
    space_eclipse_terminator_active = false;
    space_eclipse_terminator_started_us = 0;
    clock_digital_mode = false;
    tracked_flight_view = TRACKED_FLIGHT_GLOBE;

    switch (page) {
    case AIRSPACE_PREVIEW_RADAR_MAIN:
        current_screen = SCREEN_RADAR;
        break;
    case AIRSPACE_PREVIEW_RADAR_LOCATIONS:
    case AIRSPACE_PREVIEW_RADAR_CONNECTIVITY:
    case AIRSPACE_PREVIEW_RADAR_BRIGHTNESS:
    case AIRSPACE_PREVIEW_RADAR_APIS:
        current_screen = SCREEN_RADAR;
        radar_range_menu_open = true;
        radar_menu_page =
            page - AIRSPACE_PREVIEW_RADAR_LOCATIONS;
        break;
    case AIRSPACE_PREVIEW_AIRCRAFT_DETAIL: {
        const app_data_status_t *data =
            ui_data_status();
        current_screen = SCREEN_RADAR;
        aircraft_detail_active = true;
        if (data != NULL && data->aircraft_count > 0) {
            selected_aircraft = data->aircraft[0];
        }
        detail_entry_animation_pending = false;
        break;
    }
    case AIRSPACE_PREVIEW_FLIGHT_GLOBE:
        current_screen = SCREEN_TRACKED_FLIGHT;
        tracked_flight_view = TRACKED_FLIGHT_GLOBE;
        tracked_panels_entry_animation_pending = false;
        break;
    case AIRSPACE_PREVIEW_FLIGHT_FRAME:
        current_screen = SCREEN_TRACKED_FLIGHT;
        tracked_flight_view = TRACKED_FLIGHT_FRAME;
        tracked_panels_entry_animation_pending = false;
        break;
    case AIRSPACE_PREVIEW_FLIGHT_DETAIL:
        current_screen = SCREEN_TRACKED_FLIGHT;
        tracked_flight_view = TRACKED_FLIGHT_DETAIL;
        tracked_detail_entry_animation_pending = false;
        break;
    case AIRSPACE_PREVIEW_WEATHER_CURRENT:
        current_screen = SCREEN_WEATHER;
        {
            app_config_t preview_config;
            app_config_get(&preview_config);
            snprintf(
                weather_selected_key,
                sizeof(weather_selected_key),
                "%.7s",
                preview_config.location_key);
            app_data_set_weather_location_key(
                weather_selected_key);
        }
        break;
    case AIRSPACE_PREVIEW_WEATHER_FORECAST:
        current_screen = SCREEN_WEATHER;
        weather_forecast_visible = true;
        {
            app_config_t preview_config;
            app_config_get(&preview_config);
            snprintf(
                weather_selected_key,
                sizeof(weather_selected_key),
                "%.7s",
                preview_config.location_key);
            app_data_set_weather_location_key(
                weather_selected_key);
        }
        break;
    case AIRSPACE_PREVIEW_WEATHER_RADAR:
        current_screen = SCREEN_WEATHER;
        weather_radar_visible = true;
        {
            app_config_t preview_config;
            app_config_get(&preview_config);
            snprintf(
                weather_selected_key,
                sizeof(weather_selected_key),
                "%.7s",
                preview_config.location_key);
        }
        app_data_set_weather_location_key(weather_selected_key);
        app_data_request_weather_radar(radar_range_miles);
        break;
    case AIRSPACE_PREVIEW_SPACE_SOLAR:
        current_screen = SCREEN_SPACE;
        space_solar_intro_active = false;
        break;
    case AIRSPACE_PREVIEW_SPACE_MOON:
        current_screen = SCREEN_SPACE;
        space_view = SPACE_VIEW_MOON;
        break;
    case AIRSPACE_PREVIEW_SPACE_ISS:
        current_screen = SCREEN_SPACE;
        space_view = SPACE_VIEW_ISS;
        space_city_marker_visible = false;
        break;
    case AIRSPACE_PREVIEW_CLOCK_GMT:
        current_screen = SCREEN_GMT;
        clock_selected_key[0] = '\0';
        clock_secondary_key[0] = '\0';
        clock_bezel_uses_secondary = false;
        gmt_snapshot_minute = -1;
        gmt_snapshot_location_key[0] = '\0';
        gmt_snapshot_secondary_key[0] = '\0';
        break;
    case AIRSPACE_PREVIEW_CLOCK_WORLD:
        current_screen = SCREEN_GMT;
        clock_digital_mode = true;
        clock_selected_key[0] = '\0';
        clock_secondary_key[0] = '\0';
        clock_bezel_uses_secondary = false;
        gmt_snapshot_minute = -1;
        gmt_snapshot_location_key[0] = '\0';
        gmt_snapshot_secondary_key[0] = '\0';
        break;
    case AIRSPACE_PREVIEW_LAUNCH_MAIN:
        current_screen = SCREEN_LAUNCH;
        launch_view = LAUNCH_VIEW_MAIN;
        break;
    case AIRSPACE_PREVIEW_LAUNCH_MAP:
        current_screen = SCREEN_LAUNCH;
        launch_view = LAUNCH_VIEW_MAP;
        break;
    case AIRSPACE_PREVIEW_LAUNCH_QR:
        current_screen = SCREEN_LAUNCH;
        launch_view = LAUNCH_VIEW_QR;
        break;
    case AIRSPACE_PREVIEW_POWER_MENU:
        current_screen = SCREEN_RADAR;
        power_menu_visible = true;
        break;
    case AIRSPACE_PREVIEW_DOCK_ON:
        current_screen = SCREEN_RADAR;
        dock_feedback_visible = true;
        dock_feedback_on = true;
        break;
    case AIRSPACE_PREVIEW_DOCK_OFF:
        current_screen = SCREEN_RADAR;
        dock_feedback_visible = true;
        dock_feedback_on = false;
        break;
    case AIRSPACE_PREVIEW_SPACE_ECLIPSE:
        current_screen = SCREEN_SPACE;
        space_view = SPACE_VIEW_ECLIPSE;
        space_city_marker_visible = false;
        break;
    default:
        current_screen = SCREEN_RADAR;
        break;
    }
    render_requested = true;
}

void airspace_renderer_force_tracked_time_page(int page)
{
    if (page < 0 || page > 1) {
        return;
    }
    tracked_time_page_index = (uint8_t)page;
    tracked_time_page_epoch_us = esp_timer_get_time();
    render_requested = true;
}

void airspace_renderer_render(void)
{
    if (desktop_display == NULL) {
        return;
    }
    if (render_requested) {
        render_screen();
    }
    update_radar_animation();
    radar_update_aircraft_animation();
    update_tracked_aircraft_animation();
    handle_tracked_time_rotation();
    handle_space_animation();
    handle_launch_animation();
    handle_clock_refresh();
    lv_timer_handler();
    lv_refr_now(desktop_display);
}

const uint16_t *airspace_renderer_pixels(void)
{
    return (const uint16_t *)desktop_framebuffer;
}

int airspace_renderer_width(void)
{
    return SCREEN_SIZE;
}

int airspace_renderer_height(void)
{
    return SCREEN_SIZE;
}

int airspace_renderer_stride_bytes(void)
{
    return SCREEN_SIZE * 2;
}

const char *airspace_renderer_error(void)
{
    return desktop_renderer_error;
}
#endif
