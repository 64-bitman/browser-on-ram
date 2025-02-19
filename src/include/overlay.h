#pragma once

#include <stdbool.h>

#ifndef NOOVERLAY

int mount_overlay(void);
int unmount_overlay(void);
bool overlay_mounted(void);

#endif

#include <stdbool.h>
