@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
cl /nologo /c /O2 /W0 /std:c11 /I genj /I . /Fo:genj\obj_o2\ %1
