# Browser-on-ram - Sync browser related directories to RAM for Linux

* Speeds up browser
* Reduces disk writes
* Automatically backs up data
* Supports mounting data with an overlay filesystem for reduced memory usage

# Warning

RAM is not persistent! You may lose up to an hour of browsing usage data with the automatic resync timer if your system suddenly exits/crashes.
Additionally, before using this program for the first time, make sure to backup your data! I believe this software to
be stable in terms of not randomly wiping your data, and have included safety/integrity checks & repairs, but it's
always better to be safe. Additionally, make sure you have enough space in your
runtime directory before running, depending on how big your browser directories are.

# Supported Browsers

In brackets are the names to be used in the config file

* Firefox (firefox)
* Chromium (chromium)
* Google-chrome (google-chrome-stable, google-chrome-beta, google-chrome-unstable)
* Vivaldi (vivaldi, vivaldi-snapshot)
* Opera (opera)
* Brave (brave)
* Falkon (falkon)

# Dependencies

```
gcc
libcap # only if NOOVERLAY=1 is not specified
rsync
```

# Install

[AUR package](https://aur.archlinux.org/packages/browser-on-ram-git)

```sh
# remove RELEASE=1 for debug builds (which require libasan and libubsan)
# add NOSYSTEMD=1 to not compile systemd integration
# add NOOVERLAY=1 to not compile the overlay feature (removes dependency on libcap)
RELEASE=1 make

sudo RELEASE=1 make install

# enable overlay filesystem capabilities (if overlay support is compiled)
sudo make install-cap
```

# Usage

The recommended way is to use the systemd service, you can enable it it via
`systemctl --user enable --now bor.service`, note that any configured browsers
that are running will not be synced (you should close them first). This will
also start the hourly resync timer `bor-resync.timer`. If you want to resync on
sleep, then enable run `systemctl enable bor-sleep@$(whoami).service` and
`systemctl --user enable bor-sleep-resync.service`.

The executable name is `bor`. To see the current status, run `bor --status`. Use
`bor --help` for additional info.

# Config
Sample config file with defaults, in ini format (in $XDG_CACHE_HOME/bor/bor.conf)
```ini
# <boolean> = true or false

[config]
# sync cache directories (if the browser has ones)
enable_cache = false

# resync them (will be resynced when unsynced however)
resync_cache = true

# enable overlay filesystem
enable_overlay = false

# default is no browser
[browsers]
mybrowser
myotherbrowser
# ...
```

# Overlay

Browser-on-ram can mount your data on an overlay filesystem, which can significantly reduce memory usage, as only
changed data needs to be stored on RAM. To do this, it uses Linux capabilities (specifically SYS_ADMIN_CAP and
SYS_DAC_OVERRIDE), which are unfortunately very broad (But I supposed better than setuid). By default they are in permitted mode (doesn't actually affect the program),
and are only raised to effective mode when mounting the overlay filesystem and deleting the root owned work directory needed by the
overlay filesystem. If anything related to interacting with capabilties fails, the program immediately exits.

#

# Adding Browsers

Browser-on-ram uses shell scripts that output the information needed to sync them. You can use `echo` for this. You
can see existing scripts in this repository for reference in the `scripts` directory. In short they are in the format
of an ini file, parsed line by line:
```sh
# browser-on-ram will automatically set XDG_CONFIG_HOME, XDG_CACHE_HOME, and
# XDG_DATA_HOME environment variables when calling the script

# process name of the browser
procname = mybrowser

# cache directories (usually in $HOME/.cache)
cache = /home/user/.cache/mycache

# profile directory (such as an individual profile or a single monolithic one)
profile = /home/user/.config/mybrowser

# ... <additional cache/profiles>
```
These should be placed in `$XDG_CONFIG_HOME/bor/scripts`, `/usr/local/share/bor/scripts`, `/usr/share/bor/scripts` with `.sh` extension.
The first one found in that order is used. Please also make a pull request too!

# Design

Browser-on-ram first parses the output from the shell script for each browser,
and gets a list of directories to sync. It then copies each directory to the
tmpfs, each prefixed with a SHA1 hash of the original path. Then, the directory
is moved to the backup location and a symlink is created to the tmpfs. The
reason for a singular location for backups is to allow for one single overlay
filesystem to store all directories instead of per directory such as PSD.

# Rationale and difference from profile-sync-daemon

Browser-on-ram supports syncing cache directories. Another reason is that is that I was dismayed with the security issues of the overlay
feature of PSD. I'm not saying browser-on-ram is completely secure, but it shouldn't have any blatant security holes. Addtionally,
You can also add your own browsers in $XDG_CONFIG_HOME/bor/scripts too.

# Projects Used
* [inih](https://github.com/benhoyt/inih) - ini format parsing
* [teeny-sha1](https://github.com/CTrabant/teeny-sha1) - creating hashes for directories
* [profile-sync-daemon](https://github.com/graysky2/profile-sync-daemon) - inspiration

# License
[MIT License](LICENSE)
