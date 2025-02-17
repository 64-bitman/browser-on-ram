#pragma once

#include <limits.h>
#include <stddef.h>

#define BROWSER_NAME_SIZE 100
#define PROCNAME_SIZE 16
#define MAX_DIRS 100 // max dirs per browser

enum DirType { DIR_CACHE, DIR_PROFILE };

struct Dir {
        char path[PATH_MAX];
        char parent_path[PATH_MAX];
        char dirname[NAME_MAX];
        enum DirType type;
        struct Browser *browser;
};

struct Browser {
        char name[BROWSER_NAME_SIZE];
        char procname[PROCNAME_SIZE];
        struct Dir *dirs[MAX_DIRS];
        size_t dirs_num;
};

struct Dir *new_dir(const char *path, enum DirType type,
                    struct Browser *browser);
void free_dir(struct Dir *dir);
struct Browser *new_browser(const char *name, const char *procname);
void add_dir_to_browser(struct Browser *browser, struct Dir *dir);
void free_browser(struct Browser *browser);

// vim: sw=8 ts=8
