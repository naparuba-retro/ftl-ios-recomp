@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\SOURCES\ftl\work\recomp
cl /nologo /O1 /W0 /std:c11 /I gen_zlib /I . /Fe:test_zlib.exe test_zlib.c rt.c gen_zlib\gen_funcs_000.c gen_zlib\gen_tab.c /link /out:test_zlib.exe
