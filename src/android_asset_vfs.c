#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#define LOG_TAG "novacube_android_asset_vfs"
#include <android/log_macros.h>
#include <assert.h>
#include <errno.h>
#include <jni.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <sqlite3.h>

#include <novacube/android_asset_vfs.h>
#include <novacube/error_handling.h>
#include <novacube/standard_functions.h>

typedef struct nc__android_asset_file_t {
    sqlite3_file base;
    void* mapping;
    size_t mapping_size;
    const uint8_t* data;
    sqlite3_int64 size;
    sqlite3_int64 mmap_limit;
} nc__android_asset_file_t;

typedef struct nc__android_asset_vfs_t {
    sqlite3_vfs base;
    sqlite3_vfs* fallback;
    AAssetManager* asset_manager;
    jobject java_asset_manager;
} nc__android_asset_vfs_t;

static_assert(sizeof(off_t) >= sizeof(off64_t), "The APK asset VFS requires 64-bit file offsets.");

static nc__android_asset_vfs_t nc__android_asset_vfs;

static int nc__android_asset_close(sqlite3_file* sqlite_file) {
    nc__android_asset_file_t* file = (nc__android_asset_file_t*)sqlite_file;
    if (file->mapping) {
        const int unmap_result = munmap(file->mapping, file->mapping_size);
        NC_ASSERT(unmap_result == 0);
    }
    *file = (nc__android_asset_file_t){ 0 };
    return SQLITE_OK;
}

static int nc__android_asset_read(
    sqlite3_file* sqlite_file,
    void* output,
    const int amount,
    const sqlite3_int64 offset
) {
    nc__android_asset_file_t* file = (nc__android_asset_file_t*)sqlite_file;
    if (amount < 0 || offset < 0) {
        return SQLITE_IOERR_READ;
    }

    const sqlite3_int64 available = offset < file->size ? file->size - offset : 0;
    const int copied = available < amount ? (int)available : amount;
    if (copied > 0) {
        memcpy(output, file->data + offset, (size_t)copied);
    }
    if (copied < amount) {
        memset((uint8_t*)output + copied, 0, (size_t)(amount - copied));
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int nc__android_asset_write(
    sqlite3_file* sqlite_file,
    const void* data,
    const int amount,
    const sqlite3_int64 offset
) {
    (void)sqlite_file;
    (void)data;
    (void)amount;
    (void)offset;
    return SQLITE_IOERR_WRITE;
}

static int nc__android_asset_truncate(sqlite3_file* sqlite_file, const sqlite3_int64 size) {
    (void)sqlite_file;
    (void)size;
    return SQLITE_IOERR_TRUNCATE;
}

static int nc__android_asset_sync(sqlite3_file* sqlite_file, const int flags) {
    (void)sqlite_file;
    (void)flags;
    return SQLITE_IOERR_FSYNC;
}

static int nc__android_asset_file_size(sqlite3_file* sqlite_file, sqlite3_int64* size) {
    const nc__android_asset_file_t* file = (const nc__android_asset_file_t*)sqlite_file;
    *size = file->size;
    return SQLITE_OK;
}

static int nc__android_asset_lock(sqlite3_file* sqlite_file, const int lock) {
    (void)sqlite_file;
    (void)lock;
    // Well, this is technically a no-op, but if we need to lock the database it's because we're
    // about to perform a write operation, which will fail anyway.
    return SQLITE_OK;
}

static int nc__android_asset_unlock(sqlite3_file* sqlite_file, const int lock) {
    (void)sqlite_file;
    (void)lock;
    // ditto
    return SQLITE_OK;
}

static int nc__android_asset_check_reserved_lock(sqlite3_file* sqlite_file, int* result) {
    (void)sqlite_file;
    *result = 0;
    return SQLITE_OK;
}

static int nc__android_asset_file_control(sqlite3_file* sqlite_file, const int operation, void* argument) {
    nc__android_asset_file_t* file = (nc__android_asset_file_t*)sqlite_file;
    if (operation == SQLITE_FCNTL_MMAP_SIZE) {
        sqlite3_int64* limit = argument;
        const sqlite3_int64 requested_limit = *limit;
        *limit = file->mmap_limit;

        if (requested_limit < 0) {
            return SQLITE_OK;
        }
        file->mmap_limit = requested_limit < file->size ? requested_limit : file->size;
        return SQLITE_OK;
    }
    if (operation == SQLITE_FCNTL_VFS_POINTER) {
        *(sqlite3_vfs**)argument = &nc__android_asset_vfs.base;
        return SQLITE_OK;
    }
    return SQLITE_NOTFOUND;
}

static int nc__android_asset_sector_size(sqlite3_file* sqlite_file) {
    (void)sqlite_file;
    return 4096;
}

static int nc__android_asset_device_characteristics(sqlite3_file* sqlite_file) {
    (void)sqlite_file;
    return SQLITE_IOCAP_IMMUTABLE;
}

static int nc__android_asset_shm_map(
    sqlite3_file* sqlite_file,
    const int page,
    const int page_size,
    const int extend,
    void volatile** output
) {
    (void)sqlite_file;
    (void)page;
    (void)page_size;
    (void)extend;
    *output = NULL;
    NC_ASSERT(false && "WAL is unavailable because apk assets are read-only.");
    return SQLITE_IOERR_SHMMAP;
}

static int nc__android_asset_shm_lock(sqlite3_file* sqlite_file, const int offset, const int count, const int flags) {
    (void)sqlite_file;
    (void)offset;
    (void)count;
    (void)flags;
    NC_ASSERT(false && "WAL is unavailable because apk assets are read-only.");
    return SQLITE_IOERR_SHMLOCK;
}

static void nc__android_asset_shm_barrier(sqlite3_file* sqlite_file) {
    (void)sqlite_file;
    NC_ASSERT(false && "WAL is unavailable because apk assets are read-only.");
}

static int nc__android_asset_shm_unmap(sqlite3_file* sqlite_file, const int delete_flag) {
    (void)sqlite_file;
    (void)delete_flag;
    return SQLITE_OK;
}

static int nc__android_asset_fetch(
    sqlite3_file* sqlite_file,
    const sqlite3_int64 offset,
    const int amount,
    void** output
) {
    const nc__android_asset_file_t* file = (const nc__android_asset_file_t*)sqlite_file;
    *output = NULL;
    if (amount < 0 || offset < 0) {
        NC_ASSERT(false && "SQLite requested an APK asset range outside the mapped database.");
        return SQLITE_IOERR_MMAP;
    }
    if (offset > file->mmap_limit || amount > file->mmap_limit - offset) {
        return SQLITE_OK;
    }
    const sqlite3_int64 trailing_bytes = file->mmap_limit - offset - amount;
    if (trailing_bytes < 256) {
        return SQLITE_OK;
    }

    *output = (void*)(file->data + offset);
    return SQLITE_OK;
}

static int nc__android_asset_unfetch(sqlite3_file* sqlite_file, const sqlite3_int64 offset, void* pointer) {
    (void)sqlite_file;
    (void)offset;
    (void)pointer;
    return SQLITE_OK;
}

static const sqlite3_io_methods nc__android_asset_io_methods = {
    .iVersion = 3,
    .xClose = nc__android_asset_close,
    .xRead = nc__android_asset_read,
    .xWrite = nc__android_asset_write,
    .xTruncate = nc__android_asset_truncate,
    .xSync = nc__android_asset_sync,
    .xFileSize = nc__android_asset_file_size,
    .xLock = nc__android_asset_lock,
    .xUnlock = nc__android_asset_unlock,
    .xCheckReservedLock = nc__android_asset_check_reserved_lock,
    .xFileControl = nc__android_asset_file_control,
    .xSectorSize = nc__android_asset_sector_size,
    .xDeviceCharacteristics = nc__android_asset_device_characteristics,
    .xShmMap = nc__android_asset_shm_map,
    .xShmLock = nc__android_asset_shm_lock,
    .xShmBarrier = nc__android_asset_shm_barrier,
    .xShmUnmap = nc__android_asset_shm_unmap,
    .xFetch = nc__android_asset_fetch,
    .xUnfetch = nc__android_asset_unfetch,
};

static int nc__android_asset_open(
    sqlite3_vfs* sqlite_vfs,
    const char* path,
    sqlite3_file* sqlite_file,
    const int flags,
    int* output_flags
) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    nc__android_asset_file_t* file = (nc__android_asset_file_t*)sqlite_file;
    *file = (nc__android_asset_file_t){ 0 };

    const int required_flags = SQLITE_OPEN_MAIN_DB | SQLITE_OPEN_READONLY;
    if ((flags & required_flags) != required_flags) {
        ALOGE("Tried to open database with flags 0x%x. Required flags are 0x%x.", flags, required_flags);
        return SQLITE_CANTOPEN;
    }
    const int forbidden_flags = SQLITE_OPEN_READWRITE
            | SQLITE_OPEN_CREATE
            | SQLITE_OPEN_DELETEONCLOSE
            | SQLITE_OPEN_EXCLUSIVE;
    if (flags & forbidden_flags) {
        ALOGE("Tried to open database with flags 0x%x. Forbidden flags are 0x%x.", flags, forbidden_flags);
        return SQLITE_CANTOPEN;
    }

    NC_ASSERT(path);
    AAsset* asset = AAssetManager_open(vfs->asset_manager, path, AASSET_MODE_RANDOM);
    if (!asset) {
        ALOGE("Failed to open APK asset database: %s", path);
        return SQLITE_CANTOPEN;
    }

    off64_t asset_start;
    off64_t descriptor_size;
    const int descriptor = AAsset_openFileDescriptor64(asset, &asset_start, &descriptor_size);
    const off64_t asset_size = AAsset_getLength64(asset);
    AAsset_close(asset);
    if (descriptor < 0) {
        ALOGE("APK asset database %s is compressed. Asset database files must be stored uncompressed.", path);
        return SQLITE_CANTOPEN;
    }
    if (asset_start < 0
            || asset_size <= 0
            || descriptor_size != asset_size
            || asset_size > NC_ANDROID_ASSET_MMAP_SIZE) {
        ALOGE(  "APK asset database %s has an invalid direct-access range: start %lld, size %lld, descriptor %lld.",
                path,
                (long long)asset_start,
                (long long)asset_size,
                (long long)descriptor_size);
        close(descriptor);
        return SQLITE_CANTOPEN;
    }

    const long page_size = sysconf(_SC_PAGESIZE);
    NC_ASSERT(page_size > 0 && (page_size & (page_size - 1)) == 0);
    if (page_size <= 0 || (page_size & (page_size - 1)) != 0) {
        close(descriptor);
        return SQLITE_CANTOPEN;
    }

    const off64_t mapping_offset = asset_start & ~((off64_t)page_size - 1);
    const size_t data_offset = (size_t)(asset_start - mapping_offset);
    if ((uint64_t)asset_size > SIZE_MAX - data_offset) {
        close(descriptor);
        return SQLITE_CANTOPEN;
    }
    const size_t mapping_size = data_offset + (size_t)asset_size;
    void* mapping = mmap(NULL, mapping_size, PROT_READ, MAP_PRIVATE, descriptor, (off_t)mapping_offset);
    close(descriptor);
    if (mapping == MAP_FAILED) {
        return SQLITE_CANTOPEN;
    }

    *file = (nc__android_asset_file_t){
        .base = { .pMethods = &nc__android_asset_io_methods },
        .mapping = mapping,
        .mapping_size = mapping_size,
        .data = (const uint8_t*)mapping + data_offset,
        .size = asset_size,
        .mmap_limit = asset_size,
    };
    if (output_flags) {
        *output_flags = flags;
    }
    return SQLITE_OK;
}

static int nc__android_asset_delete(sqlite3_vfs* vfs, const char* path, const int sync_directory) {
    (void)vfs;
    (void)path;
    (void)sync_directory;
    return SQLITE_IOERR_DELETE;
}

static int nc__android_asset_access(sqlite3_vfs* sqlite_vfs, const char* path, const int flags, int* result) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    *result = 0;
    if (flags == SQLITE_ACCESS_READWRITE) {
        return SQLITE_OK;
    }
    if (flags != SQLITE_ACCESS_EXISTS && flags != SQLITE_ACCESS_READ) {
        return SQLITE_IOERR_ACCESS;
    }

    AAsset* asset = AAssetManager_open(vfs->asset_manager, path, AASSET_MODE_UNKNOWN);
    if (asset) {
        *result = 1;
        AAsset_close(asset);
    }
    return SQLITE_OK;
}

static int nc__android_asset_full_pathname(sqlite3_vfs* vfs, const char* path, const int output_size, char* output) {
    (void)vfs;
    const size_t path_size = strlen(path) + 1;
    if (path_size > (size_t)output_size) {
        return SQLITE_CANTOPEN;
    }
    memcpy(output, path, path_size);
    return SQLITE_OK;
}

static void* nc__android_asset_dl_open(sqlite3_vfs* sqlite_vfs, const char* path) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    return vfs->fallback->xDlOpen(vfs->fallback, path);
}

static void nc__android_asset_dl_error(sqlite3_vfs* sqlite_vfs, const int size, char* message) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    vfs->fallback->xDlError(vfs->fallback, size, message);
}

static void (*nc__android_asset_dl_sym(sqlite3_vfs* sqlite_vfs, void* handle, const char* symbol))(void) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    return vfs->fallback->xDlSym(vfs->fallback, handle, symbol);
}

static void nc__android_asset_dl_close(sqlite3_vfs* sqlite_vfs, void* handle) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    vfs->fallback->xDlClose(vfs->fallback, handle);
}

static int nc__android_asset_randomness(sqlite3_vfs* sqlite_vfs, const int size, char* output) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    return vfs->fallback->xRandomness(vfs->fallback, size, output);
}

static int nc__android_asset_sleep(sqlite3_vfs* sqlite_vfs, const int microseconds) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    return vfs->fallback->xSleep(vfs->fallback, microseconds);
}

static int nc__android_asset_current_time(sqlite3_vfs* sqlite_vfs, double* time) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    return vfs->fallback->xCurrentTime(vfs->fallback, time);
}

static int nc__android_asset_get_last_error(sqlite3_vfs* sqlite_vfs, const int size, char* message) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    return vfs->fallback->xGetLastError(vfs->fallback, size, message);
}

static int nc__android_asset_current_time_int64(sqlite3_vfs* sqlite_vfs, sqlite3_int64* time) {
    nc__android_asset_vfs_t* vfs = (nc__android_asset_vfs_t*)sqlite_vfs;
    return vfs->fallback->xCurrentTimeInt64(vfs->fallback, time);
}

static bool nc__android_asset_get_manager(JNIEnv* environment) {
    bool success = false;
    jobject activity = SDL_GetAndroidActivity();
    jclass activity_class = NULL;
    jobject local_asset_manager = NULL;
    if (!activity || (*environment)->ExceptionCheck(environment)) {
        (*environment)->ExceptionClear(environment);
        NC_SET_ERROR("Failed to obtain the Android activity.");
        goto error;
    }

    activity_class = (*environment)->GetObjectClass(environment, activity);
    if (!activity_class || (*environment)->ExceptionCheck(environment)) {
        (*environment)->ExceptionClear(environment);
        NC_SET_ERROR("Failed to obtain the Android activity class.");
        goto error;
    }
    jmethodID get_assets = (*environment)->GetMethodID(
            environment,
            activity_class,
            "getAssets",
            "()Landroid/content/res/AssetManager;");
    if (!get_assets || (*environment)->ExceptionCheck(environment)) {
        (*environment)->ExceptionClear(environment);
        NC_SET_ERROR("Failed to resolve Activity.getAssets().");
        goto error;
    }

    local_asset_manager = (*environment)->CallObjectMethod(environment, activity, get_assets);
    if (!local_asset_manager || (*environment)->ExceptionCheck(environment)) {
        (*environment)->ExceptionClear(environment);
        NC_SET_ERROR("Failed to call Activity.getAssets().");
        goto error;
    }
    nc__android_asset_vfs.java_asset_manager = (*environment)->NewGlobalRef(environment, local_asset_manager);
    if (!nc__android_asset_vfs.java_asset_manager || (*environment)->ExceptionCheck(environment)) {
        (*environment)->ExceptionClear(environment);
        NC_SET_ERROR("Failed to retain the Android AssetManager.");
        goto error;
    }
    nc__android_asset_vfs.asset_manager = AAssetManager_fromJava(
            environment,
            nc__android_asset_vfs.java_asset_manager);
    if (!nc__android_asset_vfs.asset_manager || (*environment)->ExceptionCheck(environment)) {
        (*environment)->ExceptionClear(environment);
        NC_SET_ERROR("Failed to obtain the NDK AAssetManager.");
        goto error;
    }

    success = true;

error:
    if (local_asset_manager) {
        (*environment)->DeleteLocalRef(environment, local_asset_manager);
    }
    if (activity_class) {
        (*environment)->DeleteLocalRef(environment, activity_class);
    }
    if (activity) {
        (*environment)->DeleteLocalRef(environment, activity);
    }
    if (!success && nc__android_asset_vfs.java_asset_manager) {
        (*environment)->DeleteGlobalRef(environment, nc__android_asset_vfs.java_asset_manager);
        nc__android_asset_vfs.java_asset_manager = NULL;
    }
    if (!success) {
        nc__android_asset_vfs.asset_manager = NULL;
    }
    return success;
}

bool nc_android_asset_vfs_register(void) {
    if (nc__android_asset_vfs.base.zName) {
        return true;
    }

    nc__android_asset_vfs.fallback = sqlite3_vfs_find(NULL);
    NC_ASSERT(nc__android_asset_vfs.fallback);

    JNIEnv* environment = SDL_GetAndroidJNIEnv();
    if (!environment) {
        return false;
    }
    NC_ASSERT(!(*environment)->ExceptionCheck(environment));
    if (!nc__android_asset_get_manager(environment)) {
        return false;
    }

    nc__android_asset_vfs.base = (sqlite3_vfs){
        .iVersion = 2,
        .szOsFile = sizeof(nc__android_asset_file_t),
        .mxPathname = FILENAME_MAX,
        .zName = NC_ANDROID_ASSET_VFS_NAME,
        .xOpen = nc__android_asset_open,
        .xDelete = nc__android_asset_delete,
        .xAccess = nc__android_asset_access,
        .xFullPathname = nc__android_asset_full_pathname,
        .xDlOpen = nc__android_asset_dl_open,
        .xDlError = nc__android_asset_dl_error,
        .xDlSym = nc__android_asset_dl_sym,
        .xDlClose = nc__android_asset_dl_close,
        .xRandomness = nc__android_asset_randomness,
        .xSleep = nc__android_asset_sleep,
        .xCurrentTime = nc__android_asset_current_time,
        .xGetLastError = nc__android_asset_vfs.fallback->xGetLastError
                ? nc__android_asset_get_last_error
                : NULL,
        .xCurrentTimeInt64 = nc__android_asset_vfs.fallback->iVersion >= 2
                && nc__android_asset_vfs.fallback->xCurrentTimeInt64
                ? nc__android_asset_current_time_int64
                : NULL,
    };

    const int sqlite_result = sqlite3_vfs_register(&nc__android_asset_vfs.base, 0);
    if (sqlite_result != SQLITE_OK) {
        NC_SET_ERROR("Failed to register the Android asset SQLite VFS: %s", sqlite3_errstr(sqlite_result));
        (*environment)->DeleteGlobalRef(environment, nc__android_asset_vfs.java_asset_manager);
        nc__android_asset_vfs = (nc__android_asset_vfs_t){ 0 };
        return false;
    }
    return true;
}
