@echo off
setlocal
set "BD_EDITOR=%ProgramFiles%\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
if defined UE58_ROOT set "BD_EDITOR=%UE58_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "%BD_EDITOR%" (
  echo Unreal Engine 5.8 was not found. Open BlastDelivery568.uproject with your installed UE 5.8 editor.
  pause
  exit /b 1
)
start "" "%BD_EDITOR%" "%~dp0BlastDelivery568.uproject"
