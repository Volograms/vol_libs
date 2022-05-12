#!/bin/bash
# This script runs the tests (but doesn't build them first).
# And prints the pass/fail state in green or red, respectively.

abort() {
    echo >&2 -e '\033[0;31m
********************
*** TESTS FAILED ***
********************
\033[0m'
    echo "Exiting with error..." >&2
    exit 1
}
trap 'abort' 0

set -e
cd tests/
./geom_test.bin
./av_test.bin
cd ..

trap : 0
echo >&2 -e '\033[0;32m
********************
*** TESTS PASSED ***
********************
\033[0m'
