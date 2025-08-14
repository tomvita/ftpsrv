#include "ftpsrv_vfs.h"
#include "vfs_nx_cheats.h"
#include "../custom_commands.h"
#include "../utils.h"
#include "log/log.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define QLAUNCH_TID 0x0100000000001000ULL

static int vfs_cheats_opendir(void* user, const char* path) {
    return 0;
}

static const char* vfs_cheats_readdir(void* user, void* user_entry) {
    // This is a dummy implementation that returns the three cheat directories.
    // A proper implementation would need to get the current game's title ID and name.
    static int index = 0;
    const char* cheats[] = {
        "breeze_tid_cheats",
        "breeze_name_cheats",
        "atmosphere_tid_cheats"
    };

    if (index < 3) {
        return cheats[index++];
    } else {
        index = 0;
        return NULL;
    }
}

static int vfs_cheats_dirlstat(void* user, const void* user_entry, const char* path, struct stat* st) {
    return -1;
}

static int vfs_cheats_closedir(void* user) {
    return 0;
}

static int vfs_cheats_isdir_open(void* user) {
    return 1;
}

static int vfs_cheats_stat(const char* path, struct stat* st) {
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
    return 0;
}

const FtpVfs g_vfs_cheats = {
    .open = NULL,
    .read = NULL,
    .write = NULL,
    .seek = NULL,
    .close = NULL,
    .isfile_open = NULL,
    .opendir = vfs_cheats_opendir,
    .readdir = vfs_cheats_readdir,
    .dirlstat = vfs_cheats_dirlstat,
    .closedir = vfs_cheats_closedir,
    .isdir_open = vfs_cheats_isdir_open,
    .stat = vfs_cheats_stat,
    .lstat = vfs_cheats_stat,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL,
    .rename = NULL,
};