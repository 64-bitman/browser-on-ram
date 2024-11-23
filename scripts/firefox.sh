# profile="$HOME/.mozilla/firefox/profiles.ini"

# paths="$(cat $profile | grep -oP '(?<=Path=).+')"

# while read -r line; do
#     echo "profile $HOME/.mozilla/firefox/$line"
# done <<< "$paths"
