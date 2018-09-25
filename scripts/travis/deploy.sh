#!/bin/bash
set -e

SCRIPT_DIR=$(cd $(dirname $0) || exit 1; pwd)

curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py
sudo python get-pip.py
${SCRIPT_DIR}/macpython-build-wheels.sh 3.5 
echo 'deploy'
echo $DEPLOY
