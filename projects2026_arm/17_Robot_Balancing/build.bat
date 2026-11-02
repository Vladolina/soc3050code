@echo off
REM ============================================================================
REM  17_Robot_Balancing - build
REM
REM  world.c, control.c and view.c are compiled because they sit in this
REM  folder - they are the subject of the lesson, and host\sitl.c compiles the
REM  same three files on the PC.  From _lib\: the kernel (os), lesson 08's
REM  interrupt UART (uart) and frame checksum (proto), retarget's C library
REM  stubs, and the app board: OLED on I2C, joystick/knob/buttons, buzzer.
REM  For the station on the PC (tools\station\station.py): keys and kbd turn
REM  its $KEY lines into the same pad the buttons fill, and mirror copies the
REM  OLED to it as $FB frames.
REM ============================================================================
set TARGET=c031c6
set LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror
call "%~dp0..\_build\build-lesson.bat"
