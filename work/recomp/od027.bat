@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
cl /nologo /c /Od /W0 /std:c11 /I geni /I . /Fo:geni\obj\ geni\gen_funcs_027.c
link /nologo /out:ftl_pc.exe obj\*.obj geni\obj\*.obj /STACK:268435456 /MAP:ftl_pc.map winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib
