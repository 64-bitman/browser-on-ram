#pragma once

#include "types.h"

#include <stdbool.h>
#include <limits.h>

#define MAX_BROWSERS 100

struct ConfigSkel {
#ifndef NOOVERLAY
        bool enable_overlay;
#endif
        bool enable_cache;
        bool resync_cache;
        struct Browser *browsers[MAX_BROWSERS];
        size_t browsers_num;
};

struct PathsSkel {
        char runtime[PATH_MAX];
        char tmpfs[PATH_MAX];
        char config[PATH_MAX];
        char backups[PATH_MAX];
        char share_dir[PATH_MAX];
        char share_dir_local[PATH_MAX];

#ifndef NOOVERLAY
        char overlay_upper[PATH_MAX];
        char overlay_work[PATH_MAX];
#endif
};

extern struct ConfigSkel CONFIG;
extern struct PathsSkel PATHS;

int init_paths(void);
int init_config(bool save_config);

// vim: sw=8 ts=8
