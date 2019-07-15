#!/bin/bash
# This script downloads and imports libfmt.

set -vxeuo pipefail

JAEGER_GIT_URL="https://github.com/mongodb-forks/jaeger-client-cpp.git"
JAEGER_GIT_REV=v0.4.2
JAEGER_GIT_DIR=$(mktemp -d /tmp/import-jaeger.XXXXXX)
trap "rm -rf $JAEGER_GIT_DIR" EXIT

DIST=$(git rev-parse --show-toplevel)/src/third_party/jaeger-client-cpp/dist
git clone "$JAEGER_GIT_URL" $JAEGER_GIT_DIR
git -C $JAEGER_GIT_DIR checkout $JAEGER_GIT_REV

# Cherry-pick out the subdirectories and files we need
SUBDIR_WHITELIST=(
    src
    LICENSE
)
for subdir in ${SUBDIR_WHITELIST[@]}; do
    [[ -d $JAEGER_GIT_DIR/$subdir ]] && mkdir -p $DIST/$subdir
    cp -rp $JAEGER_GIT_DIR/$subdir $DIST
done
