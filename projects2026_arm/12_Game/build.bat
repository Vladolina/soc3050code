@echo off
REM ============================================================================
REM  12_Game - build
REM
REM  Main.c, engine.c, arcade.c, snake.c, breakout.c and flappy.c are compiled
REM  because they sit in this folder.  From _lib\: printf to the serial port
REM  (retarget), the I2C master (i2c) and the SSD1306 driver on top of it
REM  (oled), the app board's controls (pad, which needs adc), the buzzer
REM  (beep), and lesson 08's frame checksum (proto) and key decoder (keys).
REM
REM  No `os`: this lesson is one loop and owns SysTick itself (slide 18).
REM ============================================================================
set TARGET=c031c6
set LIBS=retarget i2c oled adc pad beep proto keys
call "%~dp0..\_build\build-lesson.bat"
