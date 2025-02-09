#pragma once

#include "types.h"

#include <stdbool.h>
#include <limits.h>

#define MAX_BROWSERS 100

struct ConfigSkel {
        bool enable_overlay;
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
};

extern struct ConfigSkel CONFIG;
extern struct PathsSkel PATHS;

int init_paths(void);
int init_config(void);

// vim: sw=8 ts=8
