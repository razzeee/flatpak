#!/bin/bash

set -euo pipefail

. "$(dirname "$0")"/libtest.sh

skip_without_bwrap
skip_revokefs_without_fuse
skip_without_system_helper

echo "1..6"

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

# Scenario: the remote tracking ref in the local OSTree repo is missing.
#
# repo_pull() resolves the current installed commit by calling
# flatpak_repo_resolve_rev() on the child repo, which walks up the parent
# chain to the real installation repo and looks up the remote tracking refspec
# "${remote}:${ref}" (e.g. "test-repo:app/org.test.Hello/x86_64/master").
#
# That ref can be absent when, for example:
#   - the remote was removed and re-added without re-installing the app
#   - the app was installed from a bundle (no remote tracking ref ever written)
#   - the ref was deleted by ostree-prune or flatpak-repair

INSTALLED_COMMIT2=$(${FLATPAK} ${U} info --show-commit org.test.Hello)

# Delete the remote tracking ref to trigger the bug.
REMOTE_REF_FILE="${FL_DIR}/repo/refs/remotes/test-repo/app/org.test.Hello/${ARCH}/master"
assert_has_file "${REMOTE_REF_FILE}"
cp "${REMOTE_REF_FILE}" "${REMOTE_REF_FILE}.bak"
rm -f "${REMOTE_REF_FILE}"

make_updated_app test "" master UPDATED2
truncate -s 0 httpd-headers-log

${FLATPAK} ${U} update -y org.test.Hello >&2

# Restore the ref so cleanup does not trip over a partially-inconsistent repo.
mv "${REMOTE_REF_FILE}.bak" "${REMOTE_REF_FILE}"

assert_file_has_content httpd-headers-log "Flatpak-Upgrade-From: ${INSTALLED_COMMIT2}"

ok "Flatpak-Upgrade-From header sent when remote tracking ref is missing"

assert_file_has_content httpd-headers-log "Flatpak-Ref: ${APP_REF}"

ok "Flatpak-Ref header sent when remote tracking ref is missing"
