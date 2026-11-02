@echo off
REM ============================================================================
REM  15_Robot_Line_Follower - build
REM
REM  Every .c in this folder is compiled: Main.c (the RTOS tasks), world.c and
REM  track.c (the simulated robot and its tracks), control.c (the controller -
REM  the file students edit) and view.c (the OLED picture).  From _lib\: the
REM  kernel, lesson 08's interrupt UART and frame format, the I2C driver and
REM  the OLED framebuffer, and the app board's pad (joystick, knob, buttons -
REM  which needs the ADC) and buzzer.  retarget supplies the C library stubs.
REM  keys + kbd turn the station's $KEY lines (or a terminal's keys) into the
REM  same pad; mirror copies the OLED to the station as $FB frames.
REM
REM  host\run.bat builds the same world.c / control.c on the PC and races them.
REM ============================================================================
set TARGET=c031c6
set LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror
call "%~dp0..\_build\build-lesson.bat"
