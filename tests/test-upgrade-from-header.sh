#!/bin/bash

set -euo pipefail

. "$(dirname "$0")"/libtest.sh

skip_without_bwrap
skip_revokefs_without_fuse
skip_without_system_helper

echo "1..4"

# Override the httpd function to enable header logging before setup_repo calls
# it. The web-server.py will log Flatpak-Ref and Flatpak-Upgrade-From headers
# to httpd-headers-log.
httpd () {
    if [ $# -eq 0 ] ; then
        set web-server.py repos "$(pwd)"/httpd-headers-log
    fi

    COMMAND=$1
    shift

    rm -f httpd-pipe
    mkfifo httpd-pipe
    PYTHONUNBUFFERED=1 "$(dirname "$0")"/$COMMAND "$@" 3> httpd-pipe 2>&1 | tee -a httpd-log >&2 &
    read < httpd-pipe
}

touch httpd-headers-log

setup_repo

# Ensure the headers log is clean before the first install
truncate -s 0 httpd-headers-log

install_repo

APP_REF="app/org.test.Hello/${ARCH}/master"

# After a fresh install, Flatpak-Upgrade-From should NOT have been sent for
# the app commit objects (no previous local commit to upgrade from).
assert_not_file_has_content httpd-headers-log "Flatpak-Upgrade-From"

ok "no Flatpak-Upgrade-From header on fresh install"

# Flatpak-Ref must be sent on every pull, including fresh installs.
assert_file_has_content httpd-headers-log "Flatpak-Ref: ${APP_REF}"

ok "Flatpak-Ref header sent on fresh install"

# Record the commit hash that was just installed; the next update must send
# this value in the Flatpak-Upgrade-From header so the server can distinguish
# an update from a fresh install.
INSTALLED_COMMIT=$(${FLATPAK} ${U} info --show-commit org.test.Hello)

# Now prepare an updated app and do an update
make_updated_app
truncate -s 0 httpd-headers-log

${FLATPAK} ${U} update -y org.test.Hello >&2

# After an update, the Flatpak-Upgrade-From header MUST carry the commit hash
# of the previously installed version so the server can count the update
# distinctly from a new install.
assert_file_has_content httpd-headers-log "Flatpak-Upgrade-From: ${INSTALLED_COMMIT}"

ok "Flatpak-Upgrade-From header sent with correct commit hash on update"

# Flatpak-Ref must still be sent on update pulls.
assert_file_has_content httpd-headers-log "Flatpak-Ref: ${APP_REF}"

ok "Flatpak-Ref header sent on update"
