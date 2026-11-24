@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
if not exist genj\obj_od mkdir genj\obj_od
if not exist obj_od mkdir obj_od
cl /nologo /c /MP /Od /W0 /std:c11 /I genj /I . /Fo:genj\obj_od\ genj\gen_funcs_*.c genj\gen_tab.c genj\gen_names.c
cl /nologo /c /MP /Od /W0 /std:c11 /I genj /I . /Fo:obj_od\ rt.c hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c neon.c gl_host.c pvrtc.c stubs.c main_game.c
link /nologo /out:ftl_ref.exe obj_od\*.obj genj\obj_od\*.obj /STACK:268435456 winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib
echo REFFINI
