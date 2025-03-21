#pragma once

#include "types.h"

#include <stdbool.h>

#define BOR_CRASH_PREFIX "bor-crash_"

enum Action {
        ACTION_NONE,
        ACTION_SYNC,
        ACTION_UNSYNC,
        ACTION_RESYNC,
        ACTION_STATUS,
        ACTION_RMRECOVERY,
        ACTION_RMCACHE
};
static char *action_str[] = { "none",   "sync",     "unsync",     "resync",
                              "status", "recovery", "clear cache" };

int do_action_on_browser(struct Browser *browser, enum Action action,
                         bool overlay);
#ifndef NOOVERLAY
int reset_overlay(void);
#endif
int get_paths(struct Dir *dir, char *backup, char *tmpfs);
int get_overlay_paths(struct Dir *dir, char *tmpfs);

// vim: sw=8 ts=8
