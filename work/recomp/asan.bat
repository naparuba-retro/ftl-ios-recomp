@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
if not exist obj_asan mkdir obj_asan
cl /nologo /c /MP /Od /Zi /fsanitize=address /W0 /std:c11 /I genj /I . /Fo:obj_asan\ rt.c hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c neon.c gl_host.c pvrtc.c stubs.c main_game.c
cl /nologo /fsanitize=address /Zi /Fe:ftl_asan.exe obj_asan\*.obj genj\obj_od\*.obj /link /STACK:268435456 winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib
