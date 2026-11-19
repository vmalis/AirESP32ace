// AirESP32ace
// Created by VM, November 2026
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "connectivity_policy.h"
#include "provider_policy.h"

int main(void)
{
    app_wifi_retry_t r;
    const int delay[] = {1,2,4,8,16,32,60};
    int64_t t = 0;
    app_wifi_retry_reset(&r, true, t);
    assert(app_wifi_retry_begin(&r, t));
    assert(!app_wifi_retry_begin(&r, t+999));
    for (unsigned n=0; n<5000; n++) {
        app_wifi_retry_failed(&r, t);
        t += 1000 * delay[n<6?n:6];
        assert(!app_wifi_retry_begin(&r,t-1));
        assert(app_wifi_retry_begin(&r,t));
    }
    app_wifi_retry_reset(&r, false, t); // connected, sleeping, or unconfigured
    assert(!app_wifi_retry_begin(&r,t+1000000));
    app_wifi_retry_reset(&r, true, t); // new profile/wake: no residual backoff
    assert(app_wifi_retry_begin(&r,t));
    assert(!app_wifi_retry_begin(&r,t+59999));
    assert(app_wifi_retry_begin(&r,t+60000)); // missing driver event
    assert(r.failures == 0);

    const int now=1800000000;
    app_data_status_t cache = {.aircraft_valid=true, .iss_valid=true, .weather_valid=true,
        .iss_track_count=1, .aircraft_count=3, .aircraft_fetched_unix=now, .weather_fetched_unix=now,
        .revision=42, .fetched_unix=now};
    cache.iss_track_timestamp[0]=now+300;
    for(int i=0;i<3;i++) {
        cache.aircraft[i].valid=true;
        cache.aircraft[i].position_unix=now-180+i;
        cache.aircraft[i].icao24[0]='a'+i;
    }
    app_data_status_t view=cache;
    app_expire_observations(&view,now+1);
    assert(view.aircraft_count==2 && view.aircraft[0].icao24[0]=='b' && view.aircraft[1].icao24[0]=='c');
    assert(cache.aircraft_count==3 && view.fetched_unix==now && view.weather_valid && view.iss_valid);
    unsigned rev=view.revision;
    view=cache; app_expire_observations(&view,now+2);
    assert(view.aircraft_count==1 && view.revision!=rev);
    view=cache; app_expire_observations(&view,now+360);
    assert(view.aircraft_count==0 && !view.aircraft_valid && view.iss_valid);
    view=cache; app_expire_observations(&view,now+361);
    assert(!view.iss_valid && view.weather_valid);
    view=cache; app_expire_observations(&view,now+1801);
    assert(!view.weather_valid && view.fetched_unix==now);
    view=cache; view.iss_track_count=99; app_expire_observations(&view,now);
    assert(!view.iss_valid);
    assert(app_provider_health(401,false)==APP_API_HEALTH_INVALID);
    assert(app_provider_health(429,false)==APP_API_HEALTH_LIMITED);
    assert(app_provider_health(0,false)==APP_API_HEALTH_UNAVAILABLE);
    for (int auth=0; auth<=1; auth++) {
        app_sky_budget_t bucket={0};
        int credits=0, tracker_success=0;
        for(int t=0;t<86400;t++) {
            if(t%60==0 && app_sky_reserve(&bucket,t,4,auth,false)) {
                credits+=4; tracker_success++;
            }
            if(app_sky_reserve(&bucket,t,1,auth,true)) credits++;
        }
        assert(tracker_success>0);
        assert(2*credits < (auth?4000:400));
    }
    puts("PASS: 5,000 retry cycles, reset/cancel, driver silence, observation expiry, and credential states");
}
