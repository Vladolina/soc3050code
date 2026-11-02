#!/usr/bin/env bash
# Build and run the lesson 12 host test with plain gcc.  From anywhere:
#   bash 12_Game/host/run.sh
# Then, to refresh the deck's screenshots:  python 12_Game/host/embed_shots.py
set -e
cd "$(dirname "$0")"
mkdir -p out
gcc -std=c11 -O2 -Wall -Wextra -I.. -I../../_lib -o out/test_games \
    test_games.c ../engine.c ../arcade.c ../snake.c ../breakout.c ../flappy.c ../doom.c ../prince.c \
    ../../_lib/oled.c ../../_lib/proto.c
    test_games.c ../engine.c ../arcade.c ../snake.c ../breakout.c ../flappy.c \
    ../../_lib/oled.c ../../_lib/proto.c ../../_lib/keys.c ../../_lib/kbd.c ../../_lib/mirror.c
./out/test_games
