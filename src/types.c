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

        struct Dir *new = calloc(1, sizeof(*new));

        char *tmp = strdup(path), *tmp2 = strdup(path);
        char *bn = basename(tmp2);
        char *parent_dir = dirname(tmp);
        // only expand path excluding basename
        char *rlpath = realpath(parent_dir, NULL);

        if (rlpath != NULL && !DIREXISTS(rlpath)) {
                plog(LOG_ERROR, "'%s' does not exist", rlpath);
                return NULL;
        }

        if (new == NULL || tmp == NULL || tmp2 == NULL || rlpath == NULL) {
                PERROR();
                free(new);
                free(tmp);
                free(tmp2);
                free(rlpath);
                return NULL;
        }

        snprintf(new->path, PATH_MAX, "%s/%s", rlpath, bn);
        free(tmp);
        free(rlpath);

        new->type = type;
        new->browser = browser;

        if (STR_EQUAL(bn, ".")) {
                plog(LOG_ERROR,
                     "basename returned '.' when creating dir struct");
                free(new);
                free(tmp2);
                PERROR();
                return NULL;
        }

        snprintf(new->dirname, NAME_MAX, "%s", bn);

        free(tmp2);

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
