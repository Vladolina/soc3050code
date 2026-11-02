@echo off
REM ============================================================================
REM  18_Robot_Competition - build
REM
REM  world.c, referee.c, bots.c, student.c and scene.c are compiled because
REM  they sit in this folder; they are pure C, and host\league.c compiles the
REM  same files on a PC.  From _lib\: the kernel (os), the interrupt UART and
REM  frame checksum (uart, proto), retarget's C library stubs, and the app
REM  board: I2C + OLED, joystick/buttons/knob (pad, adc), buzzer (beep).  For
REM  the station on the PC: the pad from its keys (keys, kbd) and the OLED
REM  copied to it (mirror).
REM ============================================================================
set TARGET=c031c6
set LIBS=retarget os uart proto i2c oled pad adc beep keys kbd mirror
call "%~dp0..\_build\build-lesson.bat"
