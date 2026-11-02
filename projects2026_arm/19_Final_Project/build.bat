@echo off
REM ============================================================================
REM  19_Final_Project - build
REM
REM  Every *.c in this folder is compiled: Main.c and shell.c (the template),
REM  app.c (YOUR project), health.c, wdog.c, prof.c.  From _lib\:
REM    retarget  the C library stubs (its weak polled _write is replaced by uart's)
REM    os        the kernel          uart + proto   interrupt UART, $frames
REM    i2c oled  the display         adc pad        the controls
REM    beep      the buzzer, TIM3
REM    keys kbd  the station's keys ($KEY) into the pad    mirror  the OLED as $FB
REM  Add a library here when your project needs one; remove what it does not.
REM ============================================================================
set TARGET=c031c6
set LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror
call "%~dp0..\_build\build-lesson.bat"
