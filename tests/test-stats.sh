#!/bin/bash

set -euo pipefail

. $(dirname $0)/libtest.sh

# Setup
setup_repo
install_repo

# Create a shared file (1MB)
dd if=/dev/zero of=shared-file bs=1024 count=1024
# App1 unique file (512KB)
dd if=/dev/zero of=app1-unique bs=1024 count=512
# App2 unique file (256KB)
dd if=/dev/zero of=app2-unique bs=1024 count=256

# Build App1
mkdir -p build-app1/files
cp app1-unique build-app1/files/unique
cp shared-file build-app1/files/shared
${FLATPAK} build-export repos/test build-app1 org.test.App1 master

# Build App2
mkdir -p build-app2/files
cp app2-unique build-app2/files/unique
cp shared-file build-app2/files/shared
${FLATPAK} build-export repos/test build-app2 org.test.App2 master

update_repo test

${FLATPAK} ${U} install -y test-repo org.test.App1
${FLATPAK} ${U} install -y test-repo org.test.App2

echo "1..3"

# 1. Check flatpak stats
${FLATPAK} stats ${U} > stats_out
assert_file_has_content stats_out "Total size on disk:"
assert_file_has_content stats_out "Exclusive space:"
assert_file_has_content stats_out "Shared space:"
ok "stats command"

# 2. Check flatpak list --columns
${FLATPAK} list ${U} --columns=name,size,exclusive > list_out
assert_file_has_content list_out "Exclusive size"
assert_file_has_content list_out "App1"
assert_file_has_content list_out "App2"
ok "list columns"

# 3. Check flatpak info
${FLATPAK} info ${U} org.test.App1 > info_out
assert_file_has_content info_out "Exclusive size:"
ok "info output"
