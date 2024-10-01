/* Copied from
 * https://gist.github.com/JonathonReinhart/8c0d90191c38af2dcadb102c4e202950 */
/* thanks to Jonathon Reinhart <3 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* mkdir(2) */

/* Make a directory; already existing dir okay */
static int maybe_mkdir (const char *path, mode_t mode) {
    struct stat st;
    errno = 0;

    /* Try to make the directory */
    if (mkdir (path, mode) == 0) return 0;

    /* If it fails for any reason but EEXIST, fail */
    if (errno != EEXIST) return -1;

    /* Check if the existing path is a directory */
    if (stat (path, &st) != 0) return -1;

    /* If not, fail with ENOTDIR */
    if (!S_ISDIR (st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    errno = 0;
    return 0;
}

int mkdir_p (const char *path) {
    /* Adapted from http://stackoverflow.com/a/2336245/119527 */
    char *_path = NULL;
    char *p;
    int result = -1;
    mode_t mode = 0777;

    errno = 0;

    /* Copy string so it's mutable */
    _path = strdup (path);
    if (_path == NULL) goto out;

    /* Iterate the string */
    for (p = _path + 1; *p; p++) {
        if (*p == '/') {
            /* Temporarily truncate */
            *p = '\0';

            if (maybe_mkdir (_path, mode) != 0) goto out;

            *p = '/';
        }
    }

    if (maybe_mkdir (_path, mode) != 0) goto out;

    result = 0;

out:
    free (_path);
    return result;
}
