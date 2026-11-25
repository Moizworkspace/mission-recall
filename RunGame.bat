@echo off
setlocal
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0MissionRecall.uproject" -game
exit /b %errorlevel%

