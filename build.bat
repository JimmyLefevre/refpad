@echo off

call "../../Programs/msvc/msvc/setup_x64.bat"

REM set CompileFlags=/Od /Zi
set CompileFlags=/O2

cl /nologo /Wall /wd4820 /wd4201 /wd5045 /wd4711 /wd4100 %CompileFlags% /Febin/refpad.exe /Ilib refpad_sdl3.c lib/SDL3/SDL3.lib /link /subsystem:windows /incremental:no