// AirESP32ace
// Created by VM, November 2026
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef int esp_err_t;
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url, *user_agent; int timeout_ms, buffer_size, buffer_size_tx;
    void *crt_bundle_attach, *user_data, *event_handler;
    bool disable_auto_redirect;
} esp_http_client_config_t;
#define ESP_OK 0
#define CONFIG_AIRSPACE_HTTP_USER_AGENT "test"
#define esp_crt_bundle_attach NULL
#define provider_header NULL
#define HTTP_METHOD_POST 1
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define AIRCRAFT_RATE_LIMIT_RETRY_MS 300000
static int opensky_rate_limited_until_unix;
static int code, declared, available, consumed, step, elapsed, opened, partial_write, header_delay;
static int initial_alloc;
static int mock_retry_header, saw_api_key, saw_bearer;
static bool chunk_complete, fail_realloc;
static char log_text[256];
static const char *TAG="test";
static void warn(const char *tag, const char *fmt, ...) {
    assert(tag); va_list a; va_start(a,fmt); vsnprintf(log_text,sizeof(log_text),fmt,a); va_end(a);
}
#define ESP_LOGW warn
static int64_t esp_timer_get_time(void) {return (int64_t)elapsed*1000;}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg){
    assert(cfg->url && cfg->user_agent && cfg->disable_auto_redirect);
    *(int *)cfg->user_data = mock_retry_header;
    return &opened;
}
static void esp_http_client_close(void *c){assert(c);}
static void esp_http_client_cleanup(void *c){assert(c);}
static int esp_http_client_set_header(void *c,const char *n,const char *v){
    assert(c&&n&&v);
    if (!strcmp(n,"x-apikey")) { assert(!strcmp(v,"fixture")); saw_api_key++; }
    if (!strcmp(n,"Authorization")) { assert(!strcmp(v,"Bearer example")); saw_bearer++; }
    return 0;
}
static void esp_http_client_set_method(void *c,int m){assert(c&&m==HTTP_METHOD_POST);}
static int esp_http_client_open(void *c,int n){assert(c&&n>=0);return opened;}
static void esp_http_client_set_timeout_ms(void *c,int t){assert(c&&t>0);}
static const char *esp_err_to_name(int e){assert(e);return "test";}
static int esp_http_client_write(void *c,const char *s,int n){assert(c&&s);return n-partial_write;}
static int esp_http_client_fetch_headers(void *c){assert(c);elapsed+=header_delay;return declared;}
static int esp_http_client_get_status_code(void *c){assert(c);return code;}
static int esp_http_client_read(void *c,char *buf,int sz){
    assert(c&&sz>0); elapsed+=step;
    int n=available-consumed<sz?available-consumed:sz;
    memset(buf,'a',n);consumed+=n;return n;
}
/* May already be true when fetch_headers consumed the wire body but read()
 * still has buffered bytes to return. */
static bool esp_http_client_is_complete_data_received(void *c){assert(c);return chunk_complete;}
static void *heap_caps_malloc(size_t n,int flags){assert(!flags);initial_alloc=(int)n;return malloc(n);}
static void *heap_caps_realloc(void *p,size_t n,int f){assert(!f);return fail_realloc?NULL:realloc(p,n);}
#define heap_caps_free free
#include "http_under_test.inc"

static void check(int declared_len,int actual,int limit,bool expected) {
    int http_code=999;
    size_t len=123;
    declared=declared_len; available=actual; consumed=elapsed=initial_alloc=0;
    char *body=http_request_with_timeout("https://example.invalid/path?token=private-fixture",
        "POST","private-fixture","test",NULL,limit,&http_code,&len,12000,NULL,NULL);
    assert((body!=NULL)==expected && !strstr(log_text,"private-fixture"));
    if(expected) {
        assert(len==(size_t)actual && strlen(body)==len && http_code==code);
        if(declared_len>0) assert(initial_alloc==declared_len+1);
    } else assert(len==0);
    free(body);
}

int main(void) {
    code=200; chunk_complete=true;
    check(2,2,128*1024,true);   // buffered during header parsing
    check(0,5012,8192,true);    // chunked and grows
    check(0,8192,8192,true);    // exact cap, no truncation
    check(0,8193,8192,false);
    check(10000,10000,1024,false);
    check(0,0,1024,false);
    chunk_complete=false; check(8000,4096,9000,false);
    chunk_complete=true; fail_realloc=true;check(0,7000,9000,false);fail_realloc=false;
    opened=-1;check(128,128,1024,false);opened=0;
    partial_write=1;check(128,128,1024,false);partial_write=0;
    header_delay=12001;check(128,128,1024,false);
    header_delay=0; declared=available=4; consumed=elapsed=0;
    int retry_after=3600; mock_retry_header=91;
    char *ok=http_request_with_timeout("https://aeroapi.flightaware.com/aeroapi/account/usage",
        "GET",NULL,NULL,NULL,1024,&code,NULL,8000,"fixture",&retry_after);
    assert(ok && saw_api_key==1 && retry_after==91); free(ok);
    assert(!http_request_with_timeout("https://example.invalid/","GET",NULL,NULL,NULL,
        1024,&code,NULL,8000,"fixture",&retry_after) && saw_api_key==1);
    consumed=0;retry_after=3600;mock_retry_header=0;
    ok=http_request_with_timeout("https://example.invalid/","GET",NULL,NULL,"example",
        1024,&code,NULL,8000,NULL,&retry_after);
    assert(ok && saw_bearer==1 && retry_after==3600); free(ok);
    puts("PASS: provider body bounds, cached short response, deadline, partial writes, redacted logs");
}
