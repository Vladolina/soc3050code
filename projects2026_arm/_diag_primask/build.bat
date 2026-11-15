@echo off
REM _diag_primask - a diagnostic, not a lesson: does the simulator honour PRIMASK?
set TARGET=c031c6
set LIBS=retarget
call "%~dp0..\_build\build-lesson.bat"
