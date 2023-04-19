#!/bin/bash

g++ -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=0 \
-o basisu_transcoder.o -c ../../thirdparty/basis_universal/transcoder/basisu_transcoder.cpp -I ../../src/ -I ../../thirdparty/
gcc -o glad.o -c ../../thirdparty/glad/src/glad.c  -I ../../thirdparty/glad/include/
g++ -g -o vol_basis.o -c ../../src/vol_basis.cpp -I ../../src/ -I ../../thirdparty/
gcc -g -o vol_geom.o -c ../../src/vol_geom.c
gcc -g -o gfx.o -c ../../thirdparty/apg/gfx.c -I ../../thirdparty/glad/include/
gcc -g -o apg_maths.o -c ../../thirdparty/apg/apg_maths.c
gcc -g -o main.o -c main.c -I ../../thirdparty/apg/ -I ../../thirdparty/glad/include/ -I ../../src/
g++ -g -Wall -Werror -pedantic -o vol_geom_combined.bin \
main.o apg_maths.o gfx.o vol_geom.o vol_basis.o glad.o basisu_transcoder.o \
-lglfw -lm
