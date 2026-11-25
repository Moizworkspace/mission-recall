@echo off
setlocal
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" MissionRecallEditor Win64 Development -Project="%~dp0MissionRecall.uproject" -WaitMutex
exit /b %errorlevel%

