@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
cl /nologo /c /W0 /std:c11 /I geng /I . /Fo:ts_main.obj test_stream.c
for %%f in (obj\*.obj) do if /I not "%%f"=="obj\main_game.obj" echo %%f>>objlist.tmp
link /nologo /out:test_stream.exe ts_main.obj @objlist.tmp geng\obj\*.obj opengl32.lib user32.lib gdi32.lib
del objlist.tmp
