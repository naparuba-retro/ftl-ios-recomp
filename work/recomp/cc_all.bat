@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
if not exist gen\obj mkdir gen\obj
cl /nologo /c /MP /Od /W0 /std:c11 /I gen /I . /Fo:gen\obj\ gen\gen_funcs_*.c gen\gen_tab.c
