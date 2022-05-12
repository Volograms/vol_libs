#!/bin/bash
mkdir -p av_inputs/
cp ../samples/out.mp4 av_inputs/
cp ../samples/out.webm av_inputs/

# compile
# $(CC) $(FLAGS) $(DEBUG) $(SANS) -o tests/av_test$(BIN_EXT) tests/av_test.c src/vol_av.c $(INC_DIR)
BINARY=fuzz_av_test.bin
INC_DIR="-I ../src/ -I ../thirdparty/ -I ../thirdparty/apg/ -I ../thirdparty/glad/include/"
DYN_LIB_AV="-lavcodec -lavdevice -lavformat -lavutil -lswscale"
afl-gcc -g -o ${BINARY} ../tests/av_test.c ../src/vol_av.c ${INC_DIR} ${DYN_LIB_AV}

# run fuzzer
AFL_EXIT_WHEN_DONE=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 AFL_SKIP_CPUFREQ=1 \
strace -f -o strace.log afl-fuzz -m 512 -i av_inputs/ -o av_outputs/ -- ./${BINARY} @@
