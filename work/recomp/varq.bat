@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
set N=%1
if exist v_%N% rmdir /s /q v_%N%
mkdir v_%N%
xcopy /q /y genj\obj\*.obj v_%N%\ >nul
del v_%N%\gen_funcs_027.obj
cl /nologo /c /W0 /std:c11 /I genj /I . /Fo:v_%N%\ /O2 genj\gen_funcs_027_%N%.c
link /nologo /out:ftl_%N%.exe obj\*.obj v_%N%\*.obj /STACK:268435456 winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib
echo VARFINI_%N%
