# Browser-on-ram

Syncs browser related directories such as your profile and cache to RAM via tmpfs.
This results in increased performance and reduced disk writes. This is done by
copying the directory to a tmpfs and creating a backup on disk that is regulary
synced to via the systemd timer. Additionally using an overlay filesystem is
supported, see [here](#overlay-filesystem).

**!!! This is program is still fresh, and as such backups are advised;
don't use this program if you want to keep your browser data 100% safe.**

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

# Configuration

The main configuration directory is located at `$XDG_CONFIG_HOME/bor`, which
defaults to `$HOME/.config/bor`. Below are config files that are located in the
bor directory.

### `bor.conf` (optional)
```conf
# use hashtags at the start of a line to create a comment
# currently implemented options and their defaults:

# enable overlay feature
enable_overlay = false

# toggle if bor is able to resync cache directories
resync_cache = true
```

### `browsers.conf` (automatically created with no browsers configured)
List of browsers to sync, one per line.
```conf
# example
firefox
chromium
```

### `exclude.conf` (optional)
List of directories, one per line, to not sync. Note that shell expansion (ex. "~")
is not supported as of now.
```
# example
/home/<username>/.mozilla/firefox/<directory>
```

# Build & Install

```sh
git clone https://github.com/64-bitman/browser-on-ram.git
cd browser-on-ram
RELEASE=1 make

# NOTE: user installs are not supported
# optional: set PREFIX var to configure base directory
sudo make install
# install systemd files
sudo make install-systemd

# optional: enable overlay filesystem capability via setting setuid bit
sudo make install_setuid
```

# Browsers Supported
* Firefox

# Overlay Filesystem
NOTE: Only the "overlay" version of the kernel module is supported. Older
versions use a kernel module named "overlayfs", and are not supported.

Browser-on-ram (bor) supports using an overlay filesystem as the "tmpfs." This results
in only changes being written into the tmpfs, significantly reducing memory
usage. NOTE that this uses the setuid bit to enable the user to run the program
as root. Bor drops permissions immediately upon the start of the
program, and permissions are only escalated when mounting and removing root owned
directories created by bor. If you want to be 100% safe, then don't use this
feature.

# Difference from Profile-sync-daemon
I originally started this project because I was dismayed of the security issues with the
overlay feature in profile-sync-daemon. Then it became more as a hobby project.
Note, I am not saying this program is necessarily more secure than psd
(dependent on my programming skills), but using a setuid program allows for more
flexibility with preventing unsafe privileges escalations.

# License
[MIT](https://github.com/64-bitman/browser-on-ram/blob/main/LICENSE)
