if [[ -z "$XDG_CACHE_HOME" ]]; then
    export XDG_CACHE_HOME="$HOME/.cache"
fi
if [[ -z "$XDG_CONFIG_HOME" ]]; then
    export XDG_CONFIG_HOME="$HOME/.config"
fi

echo procname = chrome

if [[ -n "$CHROME_CONFIG_HOME" ]]; then
    echo "profile = $CHROME_CONFIG_HOME/browser"
else
    echo "profile = $XDG_CONFIG_HOME/google-chrome"
fi
echo "cache = $XDG_CACHE_HOME/google-chrome"
