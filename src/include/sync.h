#pragma once

#include "types.h"

#include <stdbool.h>

enum Action { ACTION_NONE, ACTION_SYNC, ACTION_UNSYNC, ACTION_RESYNC };
static char *action_str[] = { "none", "sync", "unsync", "resync", "status" };

int do_action_on_browser(struct Browser *browser, enum Action action,
                         bool overlay);
int get_paths(struct Dir *dir, char *backup, char *tmpfs);
int get_overlay_paths(struct Dir *dir, char *tmpfs);

// vim: sw=8 ts=8
