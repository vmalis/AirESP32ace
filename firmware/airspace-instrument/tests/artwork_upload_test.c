// AirESP32ace
// Created by VM, November 2026
#include "artwork_upload.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const unsigned char *bytes; size_t position, limit;
    int terminal, lock_depth, probes, shrink_on;
    uint64_t free; uint32_t cluster;
} fixture_t;
static int capacity(void *ctx,uint64_t *free_bytes,uint32_t *cluster)
{
    fixture_t *x=ctx; ++x->probes;
    assert(!x->lock_depth);
    if (!x->cluster) { errno=EIO; return -1; }
    *free_bytes=(x->shrink_on && x->probes>=x->shrink_on) ? 4096 : x->free;
    *cluster=x->cluster; return 0;
}
static int receive(void *ctx,uint8_t *buf,size_t max)
{
    fixture_t *x=ctx;
    assert(!x->lock_depth);
    size_t n=x->limit-x->position;
    if (!n) return x->terminal;
    if (n>max) n=max;
    if (n>1001) n=1001; /* fragment independently from file/cluster boundaries */
    memcpy(buf,x->bytes+x->position,n); x->position+=n; return (int)n;
}
static void lock(void *ctx) { fixture_t *x=ctx; assert(x->lock_depth++==0); }
static void unlock(void *ctx) { fixture_t *x=ctx; assert(--x->lock_depth==0); }
static uint32_t fnv(const unsigned char *p,size_t n)
{
    uint32_t h=2166136261U; while(n--) h=(h^*p++)*16777619U; return h;
}
static void put(const char *path,const char *contents)
{
    FILE *f=fopen(path,"wb"); assert(f);
    assert(fwrite(contents,1,strlen(contents),f)==strlen(contents) && fclose(f)==0);
}
static void match(const char *path,const char *contents)
{
    FILE *f=fopen(path,"rb"); assert(f);
    for (;*contents;contents++) assert(fgetc(f)==*contents);
    assert(fgetc(f)==EOF && !ferror(f) && fclose(f)==0);
}
static void missing(const char *path) { struct stat s; assert(stat(path,&s)<0 && errno==ENOENT); }
static app_art_result_t install(const char *path,size_t size,bool known,uint32_t hash,
                                fixture_t *x,app_art_report_t *report)
{
    app_art_io_t io={x,capacity,receive,lock,unlock};
    x->probes=0; x->position=0;
    app_art_result_t rc=app_artwork_install(path,size,known,hash,&io,report);
    assert(x->lock_depth==0);
    return rc;
}

int main(int argc,char **argv)
{
    assert(argc==2); char p[160],s[160],b[160],unrelated[160];
    snprintf(p,sizeof(p),"%s/a.rgb",argv[1]); snprintf(s,sizeof(s),"%s/a.anw",argv[1]);
    snprintf(b,sizeof(b),"%s/a.aol",argv[1]); snprintf(unrelated,sizeof(unrelated),"%s/z.dat",argv[1]);
    put(unrelated,"untouched");
    uint64_t needed;
    assert(app_artwork_required(466*466*2,4096,&needed) && needed==573440);
    assert(!app_artwork_required(1,0,&needed) && !app_artwork_required(1,4000,&needed));
    assert(!app_artwork_required(0,4096,&needed) && !app_artwork_required(1000000,4096,&needed));
    const unsigned char update[]="new!";
    const uint32_t hash=fnv(update,4);
    app_art_report_t r;
    fixture_t x={.bytes=update,.limit=4,.free=900000,.cluster=4096};
    assert(install(p,4,true,hash,&x,&r)==ART_OK && r.received==4); match(p,"new!");
    x.free=0;
    assert(install(p,4,true,hash,&x,&r)==ART_OK && x.position==4); match(p,"new!");
    put(p,"good"); missing(s); missing(b);
    assert(install(p,4,true,hash,&x,&r)==ART_NO_SPACE && r.sys_errno==ENOSPC);
    assert(r.needed_bytes==139264 && !r.free_bytes && !r.received); match(p,"good"); missing(s);
    x.free=139263; assert(install(p,4,true,hash,&x,&r)==ART_NO_SPACE); match(p,"good");
    x.free=139264; x.cluster=0;
    assert(install(p,4,true,hash,&x,&r)==ART_VOLUME_UNAVAILABLE); match(p,"good"); missing(s);
    x.cluster=4096; x.limit=2; x.terminal=-ETIMEDOUT;
    assert(install(p,4,true,hash,&x,&r)==ART_TIMEOUT && r.received==2); match(p,"good"); missing(s);
    x.terminal=0; assert(install(p,4,true,hash,&x,&r)==ART_DISCONNECTED); match(p,"good"); missing(s);
    x.limit=4; assert(install(p,4,true,hash^1,&x,&r)==ART_BAD_CONTENT); match(p,"good"); missing(s);
    assert(install(p,4,false,0,&x,&r)==ART_OK); match(p,"new!"); missing(s); missing(b);

    /* Reset before promotion, between renames, and before the success ACK. */
    put(s,"partial"); assert(app_artwork_recover(p)==ART_OK); match(p,"new!"); missing(s);
    assert(rename(p,b)==0); put(s,"candidate"); assert(app_artwork_recover(p)==ART_OK);
    match(p,"new!"); missing(s); missing(b);
    assert(rename(p,b)==0); put(p,"candidate"); assert(app_artwork_recover(p)==ART_OK);
    match(p,"new!"); missing(s); missing(b);
    assert(rename(p,b)==0); assert(app_artwork_recover(p)==ART_OK); match(p,"new!");
    put(b,"backup"); put(s,"partial"); assert(app_artwork_recover(p)==ART_CONFLICT);
    match(p,"new!"); match(b,"backup"); match(s,"partial"); assert(unlink(b)==0 && unlink(s)==0);
    assert(mkdir(s,0700)==0); assert(install(p,4,false,0,&x,&r)==ART_MEDIA_IO);
    match(p,"new!"); assert(rmdir(s)==0);

    size_t n=466*466*2; unsigned char *large=malloc(n); assert(large);
    for (size_t i=0;i<n;i++) large[i]=(uint8_t)i;
    x.bytes=large; x.limit=n; x.free=1000000; x.shrink_on=2;
    assert(install(p,n,false,0,&x,&r)==ART_NO_SPACE && r.received>=65536);
    match(p,"new!"); missing(s); missing(b);
    x.shrink_on=0; assert(install(p,n,true,fnv(large,n),&x,&r)==ART_OK && r.received==n && x.probes>2);
    struct stat st; assert(stat(p,&st)==0 && st.st_size==(off_t)n);
    x.free=0; assert(install(p,n,true,fnv(large,n),&x,&r)==ART_OK); /* verified existing */
    free(large); match(unrelated,"untouched");
    puts("Artwork: reserve/geometry, progress, cache, rollback, re-read, partial I/O and ambiguous recovery passed");
}
