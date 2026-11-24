@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
if not exist obj_rtc3 mkdir obj_rtc3
cl /nologo /c /MP /Od /RTCu /Z7 /W0 /DRTC_BUILD /std:c11 /I genj /I . /Fo:obj_rtc3\ rt.c hle_libc.c hle_string.c hle_sys.c hle_sound.c hle_stream.c hle_thread.c hle_rbtree.c neon.c gl_host.c pvrtc.c stubs.c main_game.c
link /nologo /DEBUG /out:ftl_rtc3.exe obj_rtc3\*.obj genj\obj_od\*.obj /STACK:268435456 winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib
