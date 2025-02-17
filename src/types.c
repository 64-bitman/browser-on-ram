#include "types.h"
#include "util.h"

#include <libgen.h>
#include <stdlib.h>

#include <stdarg.h>

// returns NULL if path doesn't exist
// will still work if basename of path does not exist
// uses realpath (3) to expand path
struct Dir *new_dir(const char *path, enum DirType type,
                    struct Browser *browser)
{
        struct stat sb;

        char buf[PATH_MAX], buf2[PATH_MAX], rlpath[PATH_MAX];
        char *bn = NULL, *parent_dir = NULL;

        snprintf(buf, PATH_MAX, "%s", path);
        trim(buf);
        bn = basename(buf);

        snprintf(buf2, PATH_MAX, "%s", path);
        trim(buf);
        parent_dir = dirname(buf2);

        // only expand path excluding basename
        if (realpath(parent_dir, rlpath) == NULL || !DIREXISTS(rlpath)) {
                plog(LOG_ERROR, "'%s' does not exist or is not a directory",
                     rlpath);
                return NULL;
        }

        if (STR_EQUAL(bn, ".")) {
                plog(LOG_ERROR,
                     "basename returned '.' when creating dir struct");
                return NULL;
        }
        struct Dir *new = malloc(sizeof(*new));

        if (new == NULL) {
                PERROR();
                return NULL;
        }
        new->type = type;
        new->browser = browser;

        snprintf(new->path, PATH_MAX, "%s/%s", rlpath, bn);
        snprintf(new->dirname, NAME_MAX, "%s", bn);
        snprintf(new->parent_path, PATH_MAX, "%s", rlpath);

        return new;
}

void free_dir(struct Dir *dir)
{
        free(dir);
}

struct Browser *new_browser(const char *name, const char *procname)
{
        struct Browser *new = calloc(1, sizeof(*new));

        if (new == NULL) {
                PERROR();
                return NULL;
        }
        if (name != NULL) {
                snprintf(new->name, BROWSER_NAME_SIZE, "%s", name);
        }
        if (procname != NULL) {
                snprintf(new->procname, BROWSER_NAME_SIZE, "%s", procname);
        }

        return new;
}

void add_dir_to_browser(struct Browser *browser, struct Dir *dir)
{
        if (dir != NULL) {
                browser->dirs[browser->dirs_num] = dir;
                (browser->dirs_num)++;
        }
}

void free_browser(struct Browser *browser)
{
        if (browser != NULL) {
                for (size_t i = 0; i < browser->dirs_num; i++) {
                        free_dir(browser->dirs[i]);
                }
                free(browser);
        }
}

// vim: sw=8 ts=8
