#pragma once

#include "types.h"

enum Action { ACTION_NONE, ACTION_SYNC, ACTION_UNSYNC, ACTION_RESYNC };
static char *action_str[] = { "none", "sync", "unsync", "resync", "status" };

int do_action_on_browser(struct Browser *browser, enum Action action);

// vim: sw=8 ts=8
