@echo off
setlocal
cd /d "%~dp0"
if exist build rmdir /s /q build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
if not exist "..\bin" mkdir "..\bin"
copy /Y "build\mojib_tts.exe" "..\bin\mojib_tts.exe" >nul || exit /b 1
echo Built ..\bin\mojib_tts.exe
