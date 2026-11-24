@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
link /nologo /out:ftl_m.exe obj\*.obj v_d\*.obj /STACK:268435456 /MAP:ftl_m.map winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib
