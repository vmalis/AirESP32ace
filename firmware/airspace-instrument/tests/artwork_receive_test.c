// AirESP32ace
// Created by VM, November 2026
/* Compile the unmodified IDF adapter against a fake clock/socket to verify
 * handling of native TLS retry codes, idle/total deadlines and prefetched
 * request bytes. No network, flash or credentials involved. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {
    unsigned next_byte, calls, toggles, fail_at; bool always_retry;
    int64_t latency;
} httpd_req_t;
typedef struct { httpd_req_t *req; int64_t deadline; size_t prefix; bool space; } art_request_t;
static int64_t now;
static int64_t esp_timer_get_time(void) { return now; }
static void vTaskDelay(unsigned ticks) { now+=ticks*1000; }
#define HTTPD_SOCK_ERR_TIMEOUT (-3)
#define ESP_TLS_ERR_SSL_WANT_READ (-0x6900)
#define ESP_TLS_ERR_SSL_WANT_WRITE (-0x6880)
#define ESP_TLS_ERR_SSL_TIMEOUT (-0x6800)
static int httpd_req_recv(httpd_req_t *req,char *b,size_t n)
{
    static const int codes[]={HTTPD_SOCK_ERR_TIMEOUT,ESP_TLS_ERR_SSL_WANT_READ,
                               ESP_TLS_ERR_SSL_WANT_WRITE,ESP_TLS_ERR_SSL_TIMEOUT};
    ++req->calls;
    if (req->next_byte<128) assert(n==1); /* No lost bytes after prefetched HTTP header. */
    now+=req->latency;
    if (req->calls==req->fail_at) return 0;
    if (req->always_retry || (req->toggles && req->calls%2)) return codes[(req->calls/2)%4];
    for (size_t i=0;i<n;i++) b[i]=(char)(req->next_byte++);
    return (int)n;
}
#include "artwork_receive_under_test.inc"
int main(void)
{
    uint8_t bytes[1536];
    httpd_req_t req={.toggles=1,.latency=250000};
    art_request_t a={&req,450000000,128,true};
    int n=art_receive(&a,bytes,sizeof(bytes));
    assert(n==(int)sizeof(bytes) && !a.prefix && now>45000000); /* still active beyond old limit */
    for (size_t i=0;i<sizeof(bytes);++i) assert(bytes[i]==(uint8_t)i);
    req.always_retry=true;
    int64_t start=now;
    assert(art_receive(&a,bytes,sizeof(bytes))==-ETIMEDOUT && now-start>=20000000 && now-start<21000000);
    req.always_retry=false; req.toggles=0; req.fail_at=req.calls+1;
    assert(art_receive(&a,bytes,sizeof(bytes))==0);
    a.deadline=now; assert(art_receive(&a,bytes,sizeof(bytes))==-ETIMEDOUT);
    puts("Artwork HTTPS: TLS retry codes, parser prefix, >45s active transfer, idle/total timeout and closed peer passed");
}
