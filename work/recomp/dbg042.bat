@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
cl /nologo /c /Od /Zi /W0 /std:c11 /I geng /I . /Fd:vc_dbg.pdb /Fo:geng\obj\ geng\gen_funcs_042.c
cl /nologo /c /Od /Zi /W0 /std:c11 /I geng /I . /Fd:vc_dbg.pdb /Fo:obj\ main_game.c
link /nologo /DEBUG /out:ftl_pc.exe obj\*.obj geng\obj\*.obj /STACK:268435456 winmm.lib dbghelp.lib opengl32.lib user32.lib gdi32.lib
