@echo off
REM Build and run the lesson 12 host test with plain gcc (MinGW on PATH).
REM The real game files and the real oled.c; only the I2C bus is a stub.
REM Then, to refresh the deck's screenshots:  python embed_shots.py
pushd "%~dp0"
if not exist out mkdir out
gcc -std=c11 -O2 -Wall -Wextra -I.. -I..\..\_lib -o out\test_games.exe test_games.c ..\engine.c ..\arcade.c ..\snake.c ..\breakout.c ..\flappy.c ..\doom.c ..\prince.c ..\..\_lib\oled.c ..\..\_lib\proto.c
if errorlevel 1 (popd & exit /b 1)
out\test_games.exe
set RC=%ERRORLEVEL%
popd
exit /b %RC%
