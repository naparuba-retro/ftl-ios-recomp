@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
python -I make_stubs.py genj\gen_ext.h hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c gl_host.c > stubs.c
if not exist obj_o2 mkdir obj_o2
cl /nologo /c /MP /O2 /W0 /std:c11 /I genj /I . /Fo:obj_o2\ rt.c hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c neon.c gl_host.c pvrtc.c stubs.c main_game.c
if errorlevel 1 exit /b 1
link /nologo /out:ftl_pc.exe obj_o2\*.obj genj\obj_o2\*.obj /STACK:268435456 /MAP:ftl_pc.map winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib
