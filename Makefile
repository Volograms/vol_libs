# Makefile - vol_av - ffmpeg wrapper
# Copyright: 2021, Volograms (http://volograms.com/)
# Author:    Anton Gerdelan <anton@volograms.com>
# to build with GCC instead (eg on a server):
# make -e SANS="" -e CC=gcc -e "FLAGS=-std=c99 -m64 -Wfatal-errors -Wextra -Wall"

CC          = clang
# -Werror is useful when upgrading the API as it upgrades deprecation warnings to errors.
# NB -std=gnu99 includes time spec struct used for timing in test code.
FLAGS       = -std=gnu99 -m64 -Wfatal-errors -pedantic -Wextra -Wall
DEBUG       = -g -DVOL_AV_DEBUG
#SANS        = -fsanitize=address -fsanitize=undefined 
INC_DIR     = -I src/ -I thirdparty/ -I thirdparty/apg/ -I thirdparty/glad/include/
SRC_AV      = src/vol_av.c
SRC_GEOM    = src/vol_geom.c
STA_LIB_AV  = 
STA_LIB_GL  = 
DYN_LIB_AV  = -lavcodec -lavdevice -lavformat -lavutil -lswscale
DYN_LIB_GL  = -lGL -ldl -lglfw
LIB_DIR     = -L ./
BIN_EXT     = .bin
POSTBLD_AV  = 
CLEAN_CMD   = rm -f examples/*.bin examples/frame*.ppm examples/frame*.pgm tests/*.bin tools/*.bin

ifeq ($(OS),Windows_NT)
	CC         = GCC
	SANS       =
	FLAGS      = -std=c99 -m64 -Wfatal-errors -Wextra -Wall
	FLAGS     += -D _CRT_SECURE_NO_WARNINGS
  BIN_EXT    = .exe
	INC_DIR   += -I thirdparty/ffmpeg/include/ -I thirdparty/GLFW/include/
	LIB_DIR_AV = ./thirdparty/ffmpeg/lib/vs/x64/
	LIB_DIR_GL = ./thirdparty/GLFW/bin/
	LIB_DIR   += -L $(LIB_DIR_AV) -L $(LIB_DIR_GL)
	DYN_LIB_GL = -lOpenGL32 -lglfw3
	STA_LIB_AV = $(LIB_DIR_AV)avcodec.lib $(LIB_DIR_AV)avdevice.lib $(LIB_DIR_AV)avformat.lib $(LIB_DIR_AV)avutil.lib $(LIB_DIR_AV)swscale.lib 
	STA_LIB_GL = $(LIB_DIR_GL)libglfw3dll.a
	POSTBLD_AV = copy "thirdparty\ffmpeg\bin\vs\x64\*.dll" "examples\" & copy "thirdparty\ffmpeg\bin\vs\x64\*.dll" "tests\" & copy "thirdparty\ffmpeg\bin\vs\x64\*.dll" "tools\"
	POSTBLD_GL = copy "thirdparty\GLFW\bin\msvc\*.dll" "examples\" & copy "thirdparty\GLFW\bin\msvc\*.dll" "examples\"
	CLEAN_CMD  = del /q examples\*.exe examples\frame*.ppm examples\frame*.pgm examples\*.dll examples\*.lib tests\*.dll examples\*.pdb examples\*.exp tests\*.exe tools\*.exe
else
	DYN_LIB_AV  += -lm
	DYN_LIB_GL  += -lm
	UNAME_S      = $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
	endif
	ifeq ($(UNAME_S),Darwin)
		DYN_LIB_GL = -framework OpenGL -lglfw
		#DYN_LIB_AV += -framework Cocoa -framework IOKit -framework CoreVideo
	endif
endif

all: example_programs test_programs tool_programs

example_programs:
	$(POSTBLD_AV)
	$(POSTBLD_GL)
	$(CC) $(FLAGS) $(DEBUG) $(SANS) -o examples/get_images$(BIN_EXT) examples/00_get_images/main.c $(SRC_AV) $(INC_DIR) $(STA_LIB_AV) $(LIB_DIR) $(DYN_LIB_AV)
	$(CC) $(FLAGS) $(DEBUG) $(SANS) -o examples/dump_images$(BIN_EXT) examples/01_dump_images/main.c $(SRC_AV) $(INC_DIR) $(STA_LIB_AV) $(LIB_DIR) $(DYN_LIB_AV)
	$(CC) $(FLAGS) $(DEBUG) $(SANS) -o examples/vol_geom_opengl$(BIN_EXT) examples/02_vol_geom_opengl/main.c $(SRC_GEOM) thirdparty/apg/apg_maths.c thirdparty/apg/gfx.c thirdparty/glad/src/glad.c $(INC_DIR) $(STA_LIB_GL) $(LIB_DIR) $(DYN_LIB_GL)
	$(CC) $(FLAGS) $(DEBUG) $(SANS) -o examples/vol_geom_av_opengl$(BIN_EXT) examples/03_vol_geom_av_opengl/main.c $(SRC_AV) $(SRC_GEOM) thirdparty/apg/apg_maths.c thirdparty/apg/gfx.c thirdparty/glad/src/glad.c $(INC_DIR) $(STA_LIB_GL) $(LIB_DIR) $(DYN_LIB_GL) $(DYN_LIB_AV)

test_programs:
	$(CC) $(FLAGS) $(DEBUG) $(SANS) -o tests/geom_test$(BIN_EXT) tests/geom_test.c $(SRC_GEOM) $(INC_DIR)
	$(CC) $(FLAGS) $(DEBUG) $(SANS) -o tests/av_test$(BIN_EXT) tests/av_test.c $(SRC_AV) $(INC_DIR) $(LIB_DIR) $(DYN_LIB_AV)
	$(CC) $(FLAGS) -O2 -o tests/av_timed_test$(BIN_EXT) tests/av_timed_test.c $(SRC_AV) $(INC_DIR) $(LIB_DIR) $(DYN_LIB_AV)

tool_programs:
	$(CC) $(FLAGS) $(DEBUG) $(SANS) -o tools/obj2vol$(BIN_EXT) tools/obj2vol/main.c
	$(CC) $(FLAGS) $(DEBUG) $(SANS) -o tools/vol2obj$(BIN_EXT) tools/vol2obj/main.c $(INC_DIR) $(SRC_GEOM) $(SRC_AV) $(LIB_DIR) $(DYN_LIB_AV)

.PHONY : clean
clean:
	$(CLEAN_CMD)
