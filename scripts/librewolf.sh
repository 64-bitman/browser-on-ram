echo procname = librewolf

# Based on scripts/firefox.sh
while read -r profileItem; do
    if [[ $(echo "$profileItem" | cut -c1) = "/" ]]; then
        # path is not relative
        echo "profile = $profileItem"
        echo "cache = $XDG_CACHE_HOME/librewolf/$(basename "$profileItem")"
    else
        # we need to append the default path to give a
        # fully qualified path
        echo "profile = $HOME/.librewolf/$profileItem"
        echo "cache = $XDG_CACHE_HOME/librewolf/$profileItem"
    fi
done < <(grep '[Pp]'ath= "$HOME"/.librewolf/profiles.ini | sed 's/[Pp]ath=//')
