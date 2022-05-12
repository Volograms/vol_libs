#!/bin/bash
mkdir -p geom_inputs/
#cp ../samples/*.vol* geom_inputs/
cat ../samples/cone_hdr.vol ../samples/cone_seq.vol > geom_inputs/cone.cvol
cat ../samples/cube_hdr.vol ../samples/cube_seq.vol > geom_inputs/cube.cvol
cat ../samples/quad_hdr.vol ../samples/quad_seq.vol > geom_inputs/quad.cvol

# compile
# $(CC) $(FLAGS) $(DEBUG) $(SANS) -o tests/geom_test$(BIN_EXT) tests/geom_test.c src/vol_geom.c $(INC_DIR)
BINARY=fuzz_geom_test.bin
INC_DIR="-I ../src/ -I ../thirdparty/ -I ../thirdparty/apg/ -I ../thirdparty/glad/include/"
afl-gcc -g -o ${BINARY} ../tests/geom_test.c ../src/vol_geom.c ${INC_DIR} 

# run fuzzer
AFL_EXIT_WHEN_DONE=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 AFL_SKIP_CPUFREQ=1 \
strace -f -o strace.log afl-fuzz -i geom_inputs/ -o geom_outputs/ -- ./${BINARY} @@
