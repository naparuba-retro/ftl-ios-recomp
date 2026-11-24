@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
cl /nologo /c /Od /RTCu /Z7 /W0 /DRTC_BUILD /std:c11 /I genj /I . /Fo:obj_rtc\ main_game.c
link /nologo /DEBUG /out:ftl_rtc.exe obj_rtc\*.obj genj\obj_rtc\*.obj /STACK:268435456 winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib
