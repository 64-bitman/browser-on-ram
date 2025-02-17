# Browser-on-ram - Sync browser related directories to RAM for Linux

* Speeds up browser
* Reduces disk writes
* Automatically backs up data
* Supports mounting data with an overlay filesystem for reduced memory usage

# Warning

RAM is not persistent! You may lose up to an hour of browsing usage data with the automatic resync timer.
Additionally, before using this program for the first time, make sure to backup your data! I believe this software to
be stable in terms of not randomly wiping your data, and have included safety/integrity checks & repairs, but it's
always better to be safe.

# Supported Browsers

* Firefox
* Chromium
* Google-chrome

# Dependencies

```
gcc
libcap
rsync
```

# Install

```sh
# remove RELEASE=1 for debug builds (which require libasan and libubsan)
RELEASE=1 make

sudo RELEASE=1 make install

# enable overlay filesystem capabilities
sudo make install-cap
```

# Usage

The recommended way is to use the systemd service, you can enable it it via
`systemctl --user enable --now bor.service`, note that any configured browsers
that are running will not be synced (you should close them first). This will
also start the hourly resync timer `bor-resync.timer`. If you want to resync on
sleep, then enable run `systemctl enable bor-sleep@$(whoami).service` and
`systemctl --user enable bor-sleep-resync.service`.

# Config
Sample config file, in ini format (in $XDG_CACHE_HOME/bor/bor.conf)
```ini
[config]
# sync cache directories
enable_cache = <boolean>

# resync them (will be resynced when unsynced however)
resync_cache = <boolean>

# enable overlay filesystem
enable_overlay = <boolean>
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
# process name of the browser
procname = mybrowser

# cache directories (usually in $HOME/.cache)
cache = /home/user/.cache/mycache

# profile directory (such as an individual profile or a single monolithic one)
profile = /home/user/.config/mybrowser

# ... <additional cache/profiles>
```
These should be placed in `$XDG_CONFIG_HOME/bor/scripts`, `/usr/local/share/bor/scripts`, `/usr/share/bor/scripts`. The first one found in that order is
used. Please also make a pull request too!

# Projects Used
* [inih](https://github.com/benhoyt/inih)
* [teeny-sha1](https://github.com/CTrabant/teeny-sha1)

# License
[MIT License](LICENSE)
