// AirESP32ace
// Created by VM, November 2026
#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

/* The preset wire format is a POSIX TZ rule, not an airport code. Evaluate
 * fixed offsets and M.m.w.d transition rules without mutating process-wide
 * TZ (the network clock and renderer run concurrently). */
static inline bool preset_tz_number(const char **p, int limit, int *out)
{
    if (**p < '0' || **p > '9') return false;
    int n = 0;
    do {
        n = n * 10 + (*(*p)++ - '0');
        if (n > limit) return false;
    } while (**p >= '0' && **p <= '9');
    *out = n;
    return true;
}
static inline bool preset_tz_name(const char **p)
{
    const char *s = *p;
    if (*s == '<') {
        ++s;
        const char *first = s;
        while (*s && *s != '>') ++s;
        if (!*s || s == first) return false;
        *p = s + 1;
    } else {
        while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) ++s;
        if (s - *p < 3) return false;
        *p = s;
    }
    return true;
}
static inline bool preset_tz_duration(const char **p, int *seconds, int limit)
{
    const bool neg = **p == '-';
    if (neg || **p == '+') ++*p;
    int h = 0, m = 0, s = 0;
    if (!preset_tz_number(p, limit, &h)) return false;
    if (**p == ':') {
        ++*p;
        if (!preset_tz_number(p, 59, &m)) return false;
        if (**p == ':') {
            ++*p;
            if (!preset_tz_number(p, 59, &s)) return false;
        }
    }
    *seconds = (neg ? -1 : 1) * (h * 3600 + m * 60 + s);
    return true;
}
static inline bool preset_tz_transition(const char **p, int year, int jan1weekday,
                                        int *second)
{
    int month, week, weekday, clock = 7200;
    if (*(*p)++ != 'M' || !preset_tz_number(p, 12, &month) || month < 1 ||
        *(*p)++ != '.' || !preset_tz_number(p, 5, &week) || week < 1 ||
        *(*p)++ != '.' || !preset_tz_number(p, 6, &weekday)) return false;
    if (**p == '/') {
        ++*p;
        if (!preset_tz_duration(p, &clock, 167)) return false;
    }
    int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    days[1] += year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    int first = 0;
    for (int i = 0; i < month - 1; i++) first += days[i];
    int dom = (weekday - (jan1weekday + first) % 7 + 7) % 7 + 7 * (week - 1);
    if (dom >= days[month - 1]) dom -= 7;
    *second = (first + dom) * 86400 + clock;
    return true;
}
static inline int preset_utc_offset(const char *rule, time_t now)
{
    int standard, daylight;
    if (!rule || !preset_tz_name(&rule) || !preset_tz_duration(&rule, &standard, 24)) return 0;
    if (!*rule || !preset_tz_name(&rule)) return -standard;
    daylight = standard - 3600;
    if (*rule != ',' && !preset_tz_duration(&rule, &daylight, 24)) return -standard;
    struct tm base;
    time_t local = now - standard;
    if (!gmtime_r(&local, &base)) return -standard;
    int jan1 = (base.tm_wday - base.tm_yday % 7 + 7) % 7;
    int start, end;
    if (*rule++ != ',' || !preset_tz_transition(&rule, base.tm_year + 1900, jan1, &start) ||
        *rule++ != ',' || !preset_tz_transition(&rule, base.tm_year + 1900, jan1, &end) || *rule)
        return -standard;
    end += daylight - standard;
    int elapsed = ((base.tm_yday * 24 + base.tm_hour) * 60 + base.tm_min) * 60 + base.tm_sec;
    bool active = start < end ? elapsed >= start && elapsed < end : elapsed >= start || elapsed < end;
    return active ? -daylight : -standard;
}
