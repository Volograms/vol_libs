#!/bin/bash

# -sASYNCIFY allows async operations like emscripten_wget()
# -g includes debug symbols

emcc \
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap']" \
-sASYNCIFY \
-g \
web_vol_geom.c ../src/vol_geom.c \
-I ../src/ \
-o vol_geom.js
