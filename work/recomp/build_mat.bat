@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
cl /nologo /c /W0 /std:c11 /I geng /I . /Fo:obj\ test_mat.c
if errorlevel 1 exit /b 1
for %%f in (obj\*.obj) do if /I not "%%f"=="obj\main_game.obj" if /I not "%%f"=="obj\test_mat.obj" echo %%f>>objlist.tmp
link /nologo /out:test_mat.exe obj\test_mat.obj @objlist.tmp geng\obj\*.obj opengl32.lib user32.lib gdi32.lib
del objlist.tmp
