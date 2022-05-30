REM Volograms build for 1 demo for Visual Studio. Run periodically to check for errors.
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"

REM "we recommend you compile by using either the /W3 or /W4 warning level"
REM C4221 is nonstandard extension used in struct literals.
set COMPILER_FLAGS=/W4 /D_CRT_SECURE_NO_WARNINGS /wd4221
set LINKER_FLAGS=/out:vol_vs_gl.exe
set LIBS=..\thirdparty\GLFW\bin\msvc\glfw3dll.lib OpenGL32.lib ws2_32.lib ^
..\thirdparty\ffmpeg\lib\vs\x64\avcodec.lib ^
..\thirdparty\ffmpeg\lib\vs\x64\avdevice.lib ^
..\thirdparty\ffmpeg\lib\vs\x64\avformat.lib ^
..\thirdparty\ffmpeg\lib\vs\x64\avutil.lib ^
..\thirdparty\ffmpeg\lib\vs\x64\swscale.lib

set BUILD_DIR=".\build"
if not exist %BUILD_DIR% mkdir %BUILD_DIR%
pushd %BUILD_DIR%

set I=/I ..\src\ ^
/I ..\thirdparty\ ^
/I ..\thirdparty\apg\ ^
/I ..\thirdparty\glad\include\ ^
/I ..\thirdparty\GLFW\include\ ^
/I ..\thirdparty\ffmpeg\include\

set SRC=..\examples\03_vol_geom_av_opengl\main.c ^
..\thirdparty\apg\apg_maths.c ^
..\thirdparty\apg\gfx.c ^
..\thirdparty\glad\src\glad.c ^
..\src\vol_av.c ^
..\src\vol_geom.c

cl %COMPILER_FLAGS% %SRC% %I% /link %LINKER_FLAGS% %LIBS%
copy vol_vs_gl.exe ..\examples\

pause
