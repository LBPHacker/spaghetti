#!/usr/bin/env bash

set -euo pipefail
IFS=$'\t\n'

ninja
(cd .. && ./examples/ks.lua) | tee /dev/stderr | ./main
