profile="$HOME/.mozilla/firefox/profiles.ini"

paths="$(cat $profile | grep -oP '(?<=Path=).+')"
procname="firefox"

while read -r line; do
    echo "$procname profile $HOME/.mozilla/firefox/$line"
done <<< "$paths"
