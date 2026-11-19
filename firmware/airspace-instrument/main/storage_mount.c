// AirESP32ace
// Created by VM, August 2026

#include "storage_mount.h"
#include "storage_policy.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "diskio_impl.h"
#include "diskio_wl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static size_t read_sector(void *ctx, size_t offset, uint8_t *out, size_t len)
{
    if (offset && offset % (128U * 1024U) == 0) vTaskDelay(1);
    return wl_read(*(const wl_handle_t *)ctx, offset, out, len) == ESP_OK ? len : 0;
}

static esp_err_t initialize_erased_volume(const char *drive, wl_handle_t wl, size_t part_size)
{
    const size_t bytes = wl_size(wl), sector = wl_sector_size(wl);
    /* wl_size excludes the WL mapping/configuration areas and spare sector.
     * This board uses a large wear-levelled FAT volume with 4 KiB sectors. */
    if (sector != CONFIG_WL_SECTOR_SIZE || sector != 4096 || bytes >= part_size ||
        bytes / sector <= 128 || bytes % sector) return ESP_ERR_INVALID_SIZE;
    uint8_t *scratch = heap_caps_malloc(sector, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!scratch) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (app_storage_is_erased(bytes, scratch, sector, read_sector, &wl)) {
        ESP_LOGI("storage", "Initializing verified erased storage");
        const MKFS_PARM options = {.fmt = FM_ANY | FM_SFD, .n_fat = 2, .au_size = sector};
        err = f_mkfs(drive, &options, scratch, sector) == FR_OK ? ESP_OK : ESP_FAIL;
    } else {
        ESP_LOGE("storage", "Volume not proved erased; refusing to format");
    }
    free(scratch);
    return err;
}

esp_err_t app_storage_mount(const char *base_path, wl_handle_t *mounted_handle)
{
    if (!base_path || !mounted_handle) return ESP_ERR_INVALID_ARG;
    *mounted_handle = WL_INVALID_HANDLE;
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    if (!part) return ESP_ERR_NOT_FOUND;
    if (part->readonly) return ESP_ERR_INVALID_STATE;

    wl_handle_t wl = WL_INVALID_HANDLE;
    esp_err_t err = wl_mount(part, &wl);
    if (err != ESP_OK) return err;
    BYTE pdrv = FF_DRV_NOT_USED;
    bool has_driver = false;
    err = ff_diskio_get_drive(&pdrv);
    if (err != ESP_OK) goto fail;
    err = ff_diskio_register_wl_partition(pdrv, wl);
    if (err != ESP_OK) goto fail;
    has_driver = true;
    const char drive[3] = {(char)('0' + pdrv), ':', 0};

    /* The IDF mount-and-optionally-format helper hides the FatFs error.
     * Probe first, without publishing the VFS: clients cannot write between
     * the blank check and the factory-only initialization. Healthy volumes
     * take only the ordinary metadata mount path (no scan of file contents). */
    FATFS *probe = calloc(1, sizeof(*probe));
    if (!probe) { err = ESP_ERR_NO_MEM; goto fail; }
    FRESULT initial = f_mount(probe, drive, 1);
    /* A failed f_mount can still have registered its FATFS object. */
    f_mount(NULL, drive, 0);
    free(probe);
    if (initial == FR_NO_FILESYSTEM) {
        err = initialize_erased_volume(drive, wl, part->size);
        if (err != ESP_OK) goto fail;
    } else if (initial != FR_OK) {
        ESP_LOGE("storage", "Volume mount failed (%u); refusing to format", (unsigned)initial);
        err = ESP_FAIL;
        goto fail;
    }

    FATFS *fs = NULL;
    /* Rendering, networking and USB sync can overlap; keep 16 descriptors. */
    const esp_vfs_fat_conf_t conf = {.base_path = base_path, .fat_drive = drive, .max_files = 16};
    err = esp_vfs_fat_register_cfg(&conf, &fs);
    if (err != ESP_OK) goto fail;
    if (f_mount(fs, drive, 1) != FR_OK) {
        f_mount(NULL, drive, 0);
        esp_vfs_fat_unregister_path(base_path);
        err = ESP_FAIL;
        goto fail;
    }
    *mounted_handle = wl;
    return ESP_OK;

fail:
    if (has_driver) {
        ff_diskio_unregister(pdrv);
        ff_diskio_clear_pdrv_wl(wl);
    }
    const esp_err_t close_err = wl_unmount(wl);
    return close_err != ESP_OK ? close_err : err;
}
