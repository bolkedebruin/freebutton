//
// Created by Bolke de Bruin on 06/08/2025.
//
#include "LittleFS.h"
#include "lvgl.h"
#include "../drivers/DisplayManager.h"
#include "drivers/BusAwareFileSystem.h"

#define FS_CACHE_SIZE 0

static auto TAG = "filesystem";

using namespace FreeButton;

static void * fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode) {
    auto mode_str = "r"; // Default to read mode
    if (mode == LV_FS_MODE_WR)
        mode_str = "w";
    else if (mode == LV_FS_MODE_RD)
        mode_str = "r";
    else if (mode == (LV_FS_MODE_RD | LV_FS_MODE_WR))
        mode_str = "a";

    BusAwareFile file = getBusAwareFileSystem().open(path, mode_str);
    if (!file)
        return NULL;
    else {
        const auto fp = new BusAwareFile(std::move(file));
        return (void *)fp;
    }
}

static lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file_p) {
    const auto file = static_cast<BusAwareFile *>(file_p);
    file->close();
    delete file;
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br) {
    const auto file = static_cast<BusAwareFile *>(file_p);
    *br = file->read(static_cast<uint8_t *>(buf), btr);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_write(lv_fs_drv_t *drv, void *file_p, const void *buf, uint32_t btw, uint32_t *bw) {
    const auto file = static_cast<BusAwareFile *>(file_p);
    *bw = file->write(static_cast<const uint8_t *>(buf), btw);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence) {
    auto *file = static_cast<BusAwareFile *>(file_p);
    bool result = false;

    fs::SeekMode fs_whence = whence == LV_FS_SEEK_SET ? fs::SeekSet
            : whence == LV_FS_SEEK_CUR ? fs::SeekCur
            : whence == LV_FS_SEEK_END ? fs::SeekEnd : fs::SeekSet;

    result = file->seek(pos, fs_whence);
    if (!result)
        return LV_FS_RES_UNKNOWN;

    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p) {
    const auto *file = static_cast<BusAwareFile *>(file_p);
    *pos_p = file->position();
    return LV_FS_RES_OK;
}

// Directory operations
static void * dir_open(lv_fs_drv_t * drv, const char * path) {
    auto dir = getBusAwareFileSystem().open(path);
    if (!dir || !dir.isDirectory()) return nullptr;

    const auto dp = new BusAwareFile(std::move(dir));
    return dp;
}

static lv_fs_res_t dir_read(lv_fs_drv_t * drv, void * dir_p, char * fn, uint32_t fn_len) {
    auto * dir = static_cast<BusAwareFile *>(dir_p);
    const auto entry = dir->openNextFile();
    if (!entry) return LV_FS_RES_FS_ERR;

    const char *entry_name = entry.name();
    if (strlen(entry_name) >= fn_len) {
        return LV_FS_RES_FS_ERR;
    }

    strncpy(fn, entry_name, fn_len - 1);
    fn[fn_len - 1] = '\0'; // Ensure null-termination

    return LV_FS_RES_OK;
}

static lv_fs_res_t dir_close(lv_fs_drv_t * drv, void * dir_p) {
    const auto dir = static_cast<BusAwareFile *>(dir_p);
    dir->close();
    delete dir;
    return LV_FS_RES_OK;
}

void lv_fs_if_init(void) {
    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);

    fs_drv.letter = 'S';
    fs_drv.cache_size = FS_CACHE_SIZE;
    fs_drv.open_cb = fs_open;
    fs_drv.close_cb = fs_close;
    fs_drv.read_cb = fs_read;
    fs_drv.write_cb = fs_write;
    fs_drv.seek_cb = fs_seek;
    fs_drv.tell_cb = fs_tell;

    // Directory callbacks
    fs_drv.dir_open_cb = dir_open;
    fs_drv.dir_read_cb = dir_read;
    fs_drv.dir_close_cb = dir_close;

    lv_fs_drv_register(&fs_drv);
}

