# Gemini
after the American space program Project Gemini. This project works as a sandbox for game related programming with current experiments concerning a lockless load balancing task scheduler for a game engine framework.

## Build
Compiles with gcc 4.8.1. Targets x86-64 and currently requires SSE2, SSSE3 and SSE4.1.
...
$ g++ -std=c++11 -Wall -Wextra -march=native -Wl,--no-as-needed -pthread -O2 gemini.cpp -o gemini
...