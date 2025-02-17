echo procname = chrome

if [[ -n "$CHROME_CONFIG_HOME" ]]; then
    echo "profile = $CHROME_CONFIG_HOME"
else
    echo "profile = $XDG_CONFIG_HOME/google-chrome-unstable"
fi
echo "cache = $XDG_CACHE_HOME/google-chrome-unstable"
