@echo off
setlocal
echo This removes audited old UE copies and rebuildable UE caches only.
echo Save and close Unreal Editor first. Unity may remain open.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0CleanUEProject.ps1" %*
if errorlevel 1 (
  echo.
  echo Cleanup stopped. Read the message above; protected project files are retained.
) else (
  echo.
  echo Done. You can reopen the current project with OpenAlphaEditor.cmd.
)
pause
