#!/bin/bash
emcc \
-s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap']" \
web_vol_geom.c ../src/vol_geom.c \
-I ../src/ \
-o test.html
