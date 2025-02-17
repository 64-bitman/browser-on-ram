echo procname = firefox

# https://github.com/graysky2/profile-sync-daemon/blob/master/common/browsers/firefox
while read -r profileItem; do
    if [[ $(echo "$profileItem" | cut -c1) = "/" ]]; then
        # path is not relative
        echo "profile = $profileItem"
        echo "cache = $XDG_CACHE_HOME/mozilla/firefox/$(basename "$profileItem")"
    else
        # we need to append the default path to give a
        # fully qualified path
        echo "profile = $HOME/.mozilla/firefox/$profileItem"
        echo "cache = $XDG_CACHE_HOME/mozilla/firefox/$profileItem"
    fi
done < <(grep '[Pp]'ath= "$HOME"/.mozilla/firefox/profiles.ini | sed 's/[Pp]ath=//')
