#!/bin/bash

set -euo pipefail

. "$(dirname "$0")/libtest.sh"

# This test looks for specific localized strings.
export LC_ALL=C

echo "1..1"

# We want to start with a system installation
export USE_SYSTEMDIR=yes
. "$(dirname "$0")/libtest.sh"

setup_repo
install_repo

echo "Checking if app is in system"
${FLATPAK} list --system | grep org.test.Hello

echo "Moving app from system to user"
${FLATPAK} move -y --user org.test.Hello

echo "Checking if app is in user"
${FLATPAK} list --user | grep org.test.Hello

echo "Checking if app is no longer in system"
if ${FLATPAK} list --system | grep org.test.Hello; then
    assert_not_reached "App still in system after move"
fi

# Try to run it from user
${FLATPAK} run org.test.Hello > run_out
assert_file_has_content run_out "Hello world"

# Move it back to system
echo "Moving app from user to system"
${FLATPAK} move -y --system org.test.Hello

echo "Checking if app is in system"
${FLATPAK} list --system | grep org.test.Hello

echo "Checking if app is no longer in user"
if ${FLATPAK} list --user | grep org.test.Hello; then
    assert_not_reached "App still in user after move back to system"
fi

ok "move system to user and back"
