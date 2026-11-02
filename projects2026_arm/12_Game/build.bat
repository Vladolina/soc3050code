@echo off
REM ============================================================================
REM  12_Game - build
REM
REM  Main.c, engine.c, arcade.c and the five games (snake.c, breakout.c,
REM  flappy.c, doom.c, prince.c) are compiled because they sit in this folder.
REM  From _lib\: printf to the serial port (retarget), the I2C master (i2c) and
REM  the SSD1306 driver on top of it (oled), the app board's controls (pad,
REM  which needs adc), the buzzer (beep) and lesson 08's frame checksum (proto).
REM
REM  No `os`: this lesson is one loop and owns SysTick itself (slide 18).
REM ============================================================================
set TARGET=c031c6
set LIBS=retarget i2c oled adc pad beep proto
call "%~dp0..\_build\build-lesson.bat"
