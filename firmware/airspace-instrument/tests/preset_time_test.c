// AirESP32ace
// Created by VM, November 2026
#include "preset_time.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const char *cases[] = {
        "PST8PDT,M3.2.0,M11.1.0", "EST5EDT,M3.2.0,M11.1.0",
        "CET-1CEST,M3.5.0,M10.5.0/3", "MSK-3", "KST-9", "IST-5:30",
        "AEST-10AEDT,M10.1.0,M4.1.0/3",
    };
    for (unsigned z = 0; z < sizeof(cases)/sizeof(*cases); z++) {
        assert(setenv("TZ", cases[z], 1) == 0);
        tzset();
        for (time_t t = 1767225600; t < 1861920000; t += 1800) {
            struct tm reference;
            assert(localtime_r(&t, &reference));
            assert(preset_utc_offset(cases[z], t) == reference.tm_gmtoff);
        }
    }
    puts("PASS: preset clock agrees with host timezone rules, every half hour, 2026–28");
}
