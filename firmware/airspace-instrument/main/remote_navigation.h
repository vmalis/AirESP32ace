// AirESP32ace
// Created by VM, November 2026
#pragma once
#include "cJSON.h"
#include <string.h>

/* Only a one-field navigation command is volatile. Never short-circuit a
 * package, even if its first member is a style or its queue is empty. */
static inline const char *app_remote_navigation_style(const cJSON *root)
{
    const cJSON *field = cJSON_IsObject(root) ? root->child : NULL;
    if (!field || field->next || !field->string || strcmp(field->string,"style")) return NULL;
    /* Invalid one-field navigation is still navigation: reject it rather
     * than falling through to the flash-writing state path. */
    return cJSON_IsString(field) && field->valuestring ? field->valuestring : "";
}
