#!/bin/bash
# This script downloads and imports libfmt.

set -vxeuo pipefail

THRIFT_GIT_URL="https://github.com/mongodb-forks/thrift.git"
THRIFT_GIT_REV=v0.12.0
THRIFT_GIT_DIR=$(mktemp -d /tmp/import-thrift.XXXXXX)
trap "rm -rf $THRIFT_GIT_DIR" EXIT

DIST=$(git rev-parse --show-toplevel)/src/third_party/thrift/dist
git clone "$THRIFT_GIT_URL" $THRIFT_GIT_DIR
git -C $THRIFT_GIT_DIR checkout $THRIFT_GIT_REV

# Cherry-pick out the subdirectories and files we need
SUBDIR_WHITELIST=(
    'build/cmake'
    'lib/cpp/src'
    LICENSE
)

for subdir in ${SUBDIR_WHITELIST[@]}; do
    [[ -d $THRIFT_GIT_DIR/$subdir ]] && mkdir -p $(dirname $DIST/$subdir)
    cp -rp $THRIFT_GIT_DIR/$subdir $DIST/$subdir
done
