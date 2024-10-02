#ifndef SRC_INCLUDE_MAIN_H
#define SRC_INCLUDE_MAIN_H

enum GState_Members {
    HOMEDIR,
    TMPDIR,
    BROWSERTYPE,
    CONFDIR,
    TARGETSDIR,
    BACKUPSDIR,
    TARGETS,
    GSTATE_LEN
};

struct gitem {
    char *str;
};

#endif
