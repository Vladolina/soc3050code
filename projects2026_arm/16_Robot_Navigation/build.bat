@echo off
REM ============================================================================
REM  16_Robot_Navigation - build
REM
REM  Compiled because they sit in this folder: Main.c (the harness and its
REM  RTOS tasks), world.c (the simulated arena and robot body), nav.c (the
REM  robot's software), astar.c (the planner), levels.c (the maps) and view.c
REM  (the OLED picture).  From _lib\: the kernel, lesson 08's interrupt UART
REM  and frame checksums, the I2C master and OLED, the app board's pad (and
REM  the ADC under it) and the buzzer.  For the station on the PC
REM  (tools\station\station.py): keys and kbd, its keys as the pad, and
REM  mirror, the OLED copied to it as $FB frames.
REM
REM  host\run.bat compiles world.c, nav.c, astar.c, levels.c and view.c again
REM  with a PC compiler and runs every level without the board.
REM ============================================================================
set TARGET=c031c6
set LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror
call "%~dp0..\_build\build-lesson.bat"
