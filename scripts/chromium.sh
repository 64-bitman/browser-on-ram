if [[ -z "$XDG_CACHE_HOME" ]]; then
    export XDG_CACHE_HOME="$HOME/.cache"
fi
if [[ -z "$XDG_CONFIG_HOME" ]]; then
    export XDG_CONFIG_HOME="$HOME/.config"
fi

echo procname = chromium

echo profile  = "$XDG_CONFIG_HOME/chromium"
echo cache = "XDG_CACHE_HOME/chromium"
