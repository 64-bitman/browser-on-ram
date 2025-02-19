#include "overlay.h"
#include "config.h"
#include "util.h"
#include <sys/capability.h>
#include <sys/mount.h>
#include <unistd.h>
#include <sys/statfs.h>
#include <linux/magic.h>

#ifndef NOOVERLAY

// creates overlay mounted on tmpfs
// does not check enable_overlay config option or if we have required caps
int mount_overlay(void)
{
        plog(LOG_INFO, "mounting overlay");

        if (create_dir(PATHS.overlay_upper, 0755) == -1 ||
            create_dir(PATHS.overlay_work, 0755) == -1) {
                plog(LOG_ERROR, "cannt create require directories");
                PERROR();
                return -1;
        }
        struct stat sb;

        // I believe symlinks aren't resolved in the data string, but
        // always better to be save
        if (SYMEXISTS(PATHS.backups) || SYMEXISTS(PATHS.overlay_upper) ||
            SYMEXISTS(PATHS.overlay_work)) {
                plog(LOG_ERROR,
                     "either the backup directory or upper/work directory is a"
                     "symlink, cannot proceed");
                return -1;
        }

        unsigned long mountflags = MS_NOSUID | MS_NODEV | MS_NOATIME;
        char data[PATH_MAX * 2 + 100];

        snprintf(data, sizeof(data),
                 "index=off,lowerdir=%s,upperdir=%s,workdir=%s", PATHS.backups,
                 PATHS.overlay_upper, PATHS.overlay_work);

        // elevate permissions
        set_caps(CAP_EFFECTIVE, CAP_SET, 2, CAP_SYS_ADMIN, CAP_DAC_OVERRIDE);

        int err = mount("overlay", PATHS.tmpfs, "overlay", mountflags, data);

        // drop permissions
        set_caps(CAP_EFFECTIVE, CAP_CLEAR, 2, CAP_SYS_ADMIN, CAP_DAC_OVERRIDE);

        if (err == -1) {
                plog(LOG_ERROR, "failed mounting overlay");
                PERROR();
                return -1;
        }

        return 0;
}

int unmount_overlay(void)
{
        plog(LOG_INFO, "unmounting overlay");

        set_caps(CAP_EFFECTIVE, CAP_SET, 1, CAP_SYS_ADMIN);

        int err = umount2(PATHS.tmpfs, MNT_DETACH | UMOUNT_NOFOLLOW);

        set_caps(CAP_EFFECTIVE, CAP_CLEAR, 1, CAP_SYS_ADMIN);

        if (err == -1) {
                plog(LOG_ERROR, "failed unmounting overlay");
                PERROR();
                return -1;
        }
        // in case there are multiple mounts on tmpfs
        if (overlay_mounted()) {
                plog(LOG_ERROR,
                     "tmpfs is still mounted; multiple mounts on it?");
                return -1;
        }

        // delete required dirs
        err = remove_path(PATHS.overlay_upper);

        set_caps(CAP_EFFECTIVE, CAP_SET, 1, CAP_DAC_OVERRIDE);
        err = remove_path(PATHS.overlay_work); // work dir is owned by root
        set_caps(CAP_EFFECTIVE, CAP_CLEAR, 1, CAP_DAC_OVERRIDE);

        if (err == -1) {
                plog(LOG_WARN, "could not delete leftover directories");
                PERROR();
                return -1;
        }

        return 0;
}

// check if overlay is mounted
bool overlay_mounted(void)
{
        struct stat sb, sb2;

        if (stat(PATHS.runtime, &sb) == -1 || stat(PATHS.tmpfs, &sb2) == -1) {
                return false;
        }

        // check if runtime and tmpfs is on different devices
        if (sb.st_dev == sb2.st_dev) {
                // same filesystem
                return false;
        }

        return true;
}

#endif

// vim: sw=8 ts=8
