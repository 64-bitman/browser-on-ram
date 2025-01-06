# Browser-on-ram

Syncs browser related directories such as your profile and cache to RAM via tmpfs.
This results in increased performance and reduced disk writes. This is done by
copying the directory to a tmpfs and creating a backup on disk that is regulary
synced to via the systemd timer. Additionally using an overlay filesystem is
supported, see [here](#overlay-filesystem).

!!! This is program is still fresh, and as such
backups are advised; don't use this program if you want to keep your
browser data safe 100%.

# Usage
It is recommended to use this program with systemd for the best experience.
```
BROWSER-ON-RAM v1.0

Usage: bor [OPTION]
-s, --sync             sync browsers to memory
-u, --unsync           unsync browsers
-r, --resync           resync browsers
-p, --status           show current status and configuration
-x, --clear            clear recovery directories
-i, --ignore           ignore safety & lock checks
-c, --config           override config directory location
-d, --sharedir         override data/share directory location
-t, --runtimedir       override runtime directory location
-v, --verbose          enable debug logs
-V, --version          show program version
-h, --help             show this message

It is not recommended to use sync, unsync, or resync standalone.
Please use the systemd user service instead
```
```sh
# start & enable systemd service
# resync timer will also start
systemctl enable --now bor

# show current status
bor --status
```

# Build & Install

```sh
git clone https://github.com/64-bitman/browser-on-ram.git
cd browser-on-ram
sudo make

# NOTE: user installs are not supported
sudo make install

# optional: enable overlay filesystem capability via setting setuid bit
sudo make install_setuid
```

# Browsers Supported
* Firefox

# Overlay Filesystem
Browser-on-ram (bor) supports using an overlay filesystem as the "tmpfs." This results
in only changes being written into the tmpfs, significantly reducing memory
usage. NOTE that this uses the setuid bit to enable the user to run the program
as root. Bor drops permissions immediately upon the start of the
program, and permissions are only escalated when mounting and removing root owned
directories created by bor. If you want to be 100% safe, then don't use this
feature.

# License
[MIT](https://github.com/64-bitman/browser-on-ram/blob/main/LICENSE)
