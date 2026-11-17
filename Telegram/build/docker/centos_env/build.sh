#!/bin/bash
set -e

python3 -c 'from xml.etree import ElementTree; ElementTree.fromstring("<check/>")'
cd Telegram
./configure.sh -D Python3_EXECUTABLE="$(command -v python3)" "$@"
# KEEP_GOING=1 lets ninja report every failing translation unit in one
# run instead of stopping at the first; the exit status still fails.
cmake --build ../out --config "${CONFIG:-Release}" ${KEEP_GOING:+-- -k 0}
