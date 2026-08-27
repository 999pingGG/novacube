#pragma once
#ifndef NOVACUBE_ANDROID_ASSET_VFS_H_
#define NOVACUBE_ANDROID_ASSET_VFS_H_

#include <stdbool.h>

#define NC_ANDROID_ASSET_VFS_NAME "apk-asset"
#define NC_ANDROID_ASSET_MMAP_SIZE 2147418112

// Registers a process-lifetime, non-default SQLite VFS for read-only databases stored in the APK assets directory.
bool nc_android_asset_vfs_register(void);

#endif
