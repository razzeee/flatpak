#!/bin/bash
# SPDX-License-Identifier: LGPL-2.0-or-later

set -euo pipefail

. "$(dirname "$0")/libtest.sh"

skip_without_bwrap

setup_repo
${FLATPAK} ${U} install -y test-repo org.test.Platform >&2
${FLATPAK} build-init app org.test.Readonly org.test.Platform org.test.Platform >&2

for path in /app /var/lib /var/tmp; do
    case "$path" in
        /app) directory=app/files ;;
        *) directory=app$path ;;
    esac

    ${FLATPAK} build app bash -c 'echo original > "$1/marker"' bash "$path"
    assert_streq "$(cat "$directory/marker")" original
    assert_streq "$(${FLATPAK} build app cat "$path/marker")" original

    ${FLATPAK} build app bash -c 'echo appended >> "$1/marker"' bash "$path"
    expected=$'original\nappended'
    assert_streq "$(cat "$directory/marker")" "$expected"
    assert_streq "$(${FLATPAK} build app cat "$path/marker")" "$expected"
    ok "build writes to $path persist"

    assert_streq "$(${FLATPAK} build --readonly app cat "$path/marker")" "$expected"
    if ${FLATPAK} build --readonly app bash -c 'echo changed > "$1/marker"' bash "$path"; then
        assert_not_reached "build --readonly allowed writing to $path"
    fi
    assert_streq "$(cat "$directory/marker")" "$expected"
    assert_streq "$(${FLATPAK} build --readonly app cat "$path/marker")" "$expected"
    ok "build --readonly preserves $path"
done

done_testing
