// AirESP32ace
// Created by VM, November 2026
#include "storage_policy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the production mount orchestration; only hardware/IDF calls are
 * replaced. No real partition, files, credentials or network in this test. */
typedef int esp_err_t, wl_handle_t, FRESULT, FATFS;
typedef unsigned char BYTE;
typedef struct { bool readonly; size_t size; } esp_partition_t;
typedef struct { unsigned fmt, n_fat; size_t au_size; } MKFS_PARM;
typedef struct { const char *base_path, *fat_drive; unsigned max_files; } esp_vfs_fat_conf_t;
enum {
    ESP_OK, ESP_FAIL, ESP_ERR_INVALID_SIZE, ESP_ERR_NO_MEM, ESP_ERR_INVALID_STATE,
    ESP_ERR_INVALID_ARG, ESP_ERR_NOT_FOUND, FR_NO_FILESYSTEM, FR_DISK_ERR, FR_INT_ERR,
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, FR_OK = 0,
    FM_ANY = 7, FM_SFD = 8, MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2,
    WL_INVALID_HANDLE = -1, FF_DRV_NOT_USED = 255, CONFIG_WL_SECTOR_SIZE = 4096
};
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define SECTORS 160U
static struct {
    int initial, final, wl, disk, vfs, reads, fmts, mounts, detached, closes, cleared;
    int missing, fail_open, fail_drive, fail_reg, fail_vfs, fail_mkfs, fail_scratch;
    int bad_byte, fail_read;
} s;
static esp_partition_t part;
static void reset(void) {
    memset(&s, 0, sizeof(s)); s.bad_byte = s.fail_read = -1;
    part = (esp_partition_t){ .size = (SECTORS+4) * CONFIG_WL_SECTOR_SIZE };
}
static const esp_partition_t *esp_partition_find_first(int type, int sub, const char *label) {
    assert(type == ESP_PARTITION_TYPE_DATA && sub == ESP_PARTITION_SUBTYPE_DATA_FAT);
    assert(!strcmp(label,"storage")); return s.missing ? NULL : &part;
}
static int wl_mount(const esp_partition_t *p, int *h) {
    assert(p == &part && !s.wl);
    if (s.fail_open) return ESP_FAIL;
    *h = 5; s.wl = 1; return ESP_OK;
}
static int wl_unmount(int h) {
    assert(h == 5 && s.wl && !s.disk && !s.vfs);
    s.wl = 0; ++s.closes; return ESP_OK;
}
static size_t wl_size(int h){assert(h==5 && s.wl);return SECTORS*CONFIG_WL_SECTOR_SIZE;}
static size_t wl_sector_size(int h){assert(h==5 && s.wl);return CONFIG_WL_SECTOR_SIZE;}
static void vTaskDelay(int n){assert(n == 1);}
static int wl_read(int h,size_t off,uint8_t *dst,size_t n) {
    assert(h == 5 && s.wl && s.disk && !s.vfs && n == CONFIG_WL_SECTOR_SIZE);
    assert(off == (size_t)s.reads*n && off+n <= wl_size(h)); ++s.reads;
    if (s.fail_read == (int)off) return ESP_FAIL;
    memset(dst,0xff,n);
    if (s.bad_byte >= (int)off && s.bad_byte < (int)(off+n)) dst[s.bad_byte-(int)off]=0xfe;
    return ESP_OK;
}
static void *heap_caps_malloc(size_t n,int caps) {
    assert(n == CONFIG_WL_SECTOR_SIZE && caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return s.fail_scratch ? NULL : malloc(n);
}
static int ff_diskio_get_drive(BYTE *p){if (s.fail_drive) return ESP_FAIL; *p=1;return ESP_OK;}
static int ff_diskio_register_wl_partition(BYTE p,int h){
    assert(h==5 && s.wl && p==1 && !s.disk);if(s.fail_reg)return ESP_FAIL;s.disk=1;return ESP_OK;
}
static void ff_diskio_unregister(BYTE p){assert(p==1 && s.disk); s.disk=0;}
static void ff_diskio_clear_pdrv_wl(int h){assert(h==5 && s.wl && !s.disk);++s.cleared;}
static int f_mount(FATFS *fs, const char *drv, int opt){
    assert(!strcmp(drv,"1:") && s.disk);
    if (!fs) {assert(!opt);s.detached++;return FR_OK;}
    assert(opt==1 && s.mounts==s.detached); ++s.mounts;
    return s.vfs ? s.final : s.initial;
}
static int f_mkfs(const char *drv,const MKFS_PARM *p,void *buf,size_t n){
    assert(!strcmp(drv,"1:") && p->n_fat==2 && p->fmt==(FM_ANY|FM_SFD));
    assert(buf && n==CONFIG_WL_SECTOR_SIZE && p->au_size==n);
    assert(!s.vfs && s.wl && s.disk && s.mounts==s.detached && s.reads==SECTORS);
    ++s.fmts; return s.fail_mkfs ? FR_DISK_ERR : FR_OK;
}
static int esp_vfs_fat_register_cfg(const esp_vfs_fat_conf_t *conf,FATFS **fs){
    static FATFS fake;
    assert(!s.vfs && !strcmp(conf->base_path,"/spiffs") && !strcmp(conf->fat_drive,"1:"));
    assert(conf->max_files==16); if(s.fail_vfs)return ESP_FAIL; s.vfs=1;*fs=&fake;return ESP_OK;
}
static int esp_vfs_fat_unregister_path(const char *p){assert(!strcmp(p,"/spiffs")&&s.vfs);s.vfs=0;return ESP_OK;}

#include "storage_mount_under_test.inc"

static void check(bool expected,int confirmed_formats,int read_count) {
    int handle=-43;
    assert((app_storage_mount("/spiffs",&handle)==ESP_OK)==expected);
    assert(s.fmts==confirmed_formats && s.reads==read_count);
    if (expected) assert(handle==5 && s.vfs && s.wl && s.disk && !s.closes);
    else {
        assert(handle==WL_INVALID_HANDLE && !s.vfs && !s.wl && !s.disk);
        assert(s.mounts==s.detached);
        assert(s.closes==(int)(!s.missing && !part.readonly && !s.fail_open));
        assert(s.cleared==(int)(s.closes && !s.fail_drive && !s.fail_reg));
    }
}
int main(void){
    reset(); check(true,0,0); /* Healthy media: no volume scan and no format. */
    reset(); s.initial=FR_NO_FILESYSTEM;check(true,1,SECTORS); /* Factory erased. */
    reset(); s.initial=FR_NO_FILESYSTEM;s.bad_byte=SECTORS*4096-1;check(false,0,SECTORS);
    reset(); s.initial=FR_NO_FILESYSTEM;s.fail_read=(SECTORS-1)*4096;check(false,0,SECTORS);
    reset(); s.initial=FR_NO_FILESYSTEM;s.bad_byte=0;check(false,0,1);
    reset(); s.initial=FR_DISK_ERR; check(false,0,0);
    reset(); s.initial=FR_INT_ERR; check(false,0,0);
    reset(); s.final=FR_NO_FILESYSTEM;check(false,0,0); /* Never retry-and-format a mount failure. */
    reset(); s.missing=1;check(false,0,0);
    reset(); part.readonly=true;check(false,0,0);
    reset(); s.fail_open=1;check(false,0,0);
    reset(); s.fail_drive=1;check(false,0,0);
    reset(); s.fail_reg=1;check(false,0,0);
    reset(); s.fail_vfs=1;check(false,0,0);
    reset(); s.initial=FR_NO_FILESYSTEM;s.fail_mkfs=1;check(false,1,SECTORS);
    reset(); s.initial=FR_NO_FILESYSTEM;s.fail_scratch=1;check(false,0,0);
    reset(); s.initial=FR_NO_FILESYSTEM;part.size=4096;check(false,0,0);
    puts("Storage mount tests passed (healthy, erased, damaged, unreadable, resource cleanup).");
}
