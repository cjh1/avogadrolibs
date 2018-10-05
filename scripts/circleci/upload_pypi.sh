#!/bin/bash
sudo pip install twine
SCRIPT_DIR=$(cd $(dirname $0) || exit 1; pwd)
cd avogadrolibs
source "${SCRIPT_DIR}/../common/upload_pypi.sh"
