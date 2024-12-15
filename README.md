# Browser-on-ram

What the title says, it syncs browser related directories such as your profile and cache to RAM via tmpfs. This results in increased performance and reduced disk writes (which some browsers have a habit of doing, A LOT). This is program is currently a work in progress, and as such backups are advised; don't use this program if you want to keep your browser data safe 100%.

# Build & Install

```sh
git clone https://github.com/64-bitman/browser-on-ram.git
cd browser-on-ram
make
make install # doesn't exist, I will create that soon
```

# Browsers Supported
* Firefox

# License
[MIT](https://github.com/64-bitman/browser-on-ram/blob/main/LICENSE)
