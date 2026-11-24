@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
if not exist genj\obj_o2 mkdir genj\obj_o2
cl /nologo /c /MP /O2 /W0 /std:c11 /I genj /I . /Fo:genj\obj_o2\ genj\gen_funcs_*.c genj\gen_tab.c
echo GENFINI
