#!/bin/bash
# One real Claws Mail session in a container; see claws.sh.
set -u
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq --no-install-recommends claws-mail xvfb xauth \
    dbus-x11 >/dev/null 2>&1
export HOME=/work/home
config=$HOME/.claws-mail
mkdir -p "$HOME/Mail/inbox" "$config"
if [ "$1" = seed ]; then
    for i in 1 2 3; do
        printf 'From: =?UTF-8?Q?Ana_=C4=8C?= <ana@x.org>\nTo: me@x.org\nSubject: Claws message %s\nMessage-ID: <c%s@x>\nDate: Sat, 07 Nov 2026 10:00:0%s +0000\n\nBody %s.\n' \
            "$i" "$i" "$i" "$i" >"$HOME/Mail/inbox/$i"
    done
    cat >"$config/folderlist.xml" <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<folderlist config_version="4">
    <folder type="mh" name="Mailbox" path="/work/home/Mail">
        <folderitem type="inbox" name="inbox" path="inbox"/>
    </folder>
</folderlist>
XML
    printf '[Account: 1]\naccount_name=me\nis_default=1\nname=Me\naddress=me@x.org\nprotocol=4\nconfig_version=1\n' \
        >"$config/accountrc"
fi
Xvfb :77 >/dev/null 2>&1 &
export DISPLAY=:77
sleep 1
timeout 25 dbus-launch --exit-with-session claws-mail \
    --alternate-config-dir "$config" --select '#mh/Mailbox/inbox' \
    >/dev/null 2>&1 &
sleep 12
[ "$1" = check ] && timeout 8 claws-mail --alternate-config-dir "$config" \
    --status-full
timeout 8 claws-mail --alternate-config-dir "$config" --exit
sleep 3
if [ "$1" = check ]; then
    printf 'marks: '
    od -A n -v -t u4 -w8 -j4 "$HOME/Mail/inbox/.claws_mark" | tr -s ' ' '\n' \
        | paste -sd' '
fi
chmod -R a+rwX /work/home
