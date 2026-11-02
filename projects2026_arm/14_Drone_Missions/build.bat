@echo off
REM ============================================================================
REM  14_Drone_Missions - build
REM
REM  Compiled because they sit in this folder: Main.c (the RTOS tasks) and the
REM  four pure-C modules the host test also flies - world.c (the simulated
REM  drone), flight.c (the flight computer), mavlite.c (the mission protocol)
REM  and ui.c (the OLED map).
REM
REM  From _lib\: the kernel (os), lesson 08's interrupt UART and frame format
REM  (uart proto), retarget's C library stubs, and the app board: I2C + OLED
REM  (i2c oled), joystick/buttons/knob (pad adc) and the buzzer (beep).
REM  For the station on the PC (tools\station\station.py): every control as a
REM  key (keys kbd - lesson 08's key decoder, lesson 12's pad from a keyboard)
REM  and the OLED copied out as $FB frames (mirror).
REM ============================================================================
set TARGET=c031c6
set LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror
call "%~dp0..\_build\build-lesson.bat"
