@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
if exist v_p rmdir /s /q v_p
mkdir v_p
xcopy /q /y genj\obj\*.obj v_p\ >nul
del v_p\gen_funcs_027.obj
cl /nologo /c /W0 /std:c11 /I genj /I . /Fo:v_p\ /O2 genj\gen_funcs_027p.c
link /nologo /out:ftl_p.exe obj\*.obj v_p\*.obj /STACK:268435456 winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib
echo VARFINI_p
