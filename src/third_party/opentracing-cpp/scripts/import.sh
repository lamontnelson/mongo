#!/bin/bash
# This script downloads and imports libfmt.

set -vxeuo pipefail

OT_GIT_URL="https://github.com/mongodb-forks/opentracing-cpp.git"
OT_GIT_REV=v1.5.1
OT_GIT_DIR=$(mktemp -d /tmp/import-ot.XXXXXX)
trap "rm -rf $OT_GIT_DIR" EXIT

DIST=$(git rev-parse --show-toplevel)/src/third_party/opentracing-cpp/dist
git clone "$OT_GIT_URL" $OT_GIT_DIR
git -C $OT_GIT_DIR checkout $OT_GIT_REV

# Cherry-pick out the subdirectories and files we need
SUBDIR_WHITELIST=(
    3rd_party
    include
    src
    LICENSE
    config.h.in
    version.h.in
)
for subdir in ${SUBDIR_WHITELIST[@]}; do
    [[ -d $OT_GIT_DIR/$subdir ]] && mkdir -p $DIST/$subdir
    cp -rp $OT_GIT_DIR/$subdir $DIST
done
