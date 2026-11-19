// AirESP32ace
// Created by VM, November 2026
#pragma once
#include <stdint.h>
const char *app_display_last_rendered_page(void);
uint32_t app_display_heartbeat(void);
/* ESP-IDF minimum remaining bytes on the display task; 0 until sampled. */
uint32_t app_display_stack_low_bytes(void);
/* Last Radar render: 0 not attempted, 1 source missing, 2 padding failed, 3 drawn. */
uint32_t app_display_radar_background(void);
/* Last actual Weather emblem source width, and ISS strip result (0 never,
 * 1 disabled, 2 requested but missing, 3 submitted to the renderer). */
uint32_t app_display_weather_emblem_width(void);
uint32_t app_display_iss_flags_result(void);
