// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <switch.h>

Result fsdev_wrapMountSdmc(void);
Result fsdev_wrapMountImage(const char* path, FsImageDirectoryId id);
Result fsdev_wrapMountContent(const char* path, FsContentStorageId id);
Result fsdev_wrapMountBis(const char* path, FsBisPartitionId id);
Result fsdev_wrapMountSave(const char* path, u64 id, AccountUid uid);
Result fsdev_wrapMountSaveBcat(const char* path, u64 id);

int fsdev_wrapTranslatePath(const char *path, FsFileSystem** device, char *outpath);
int fsdev_wrapMountDevice(const char *name, FsFileSystem fs);
void fsdev_wrapUnmountAll(void);

#ifdef __cplusplus
}
#endif
