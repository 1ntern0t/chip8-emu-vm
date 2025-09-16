What this project is...

A clean, single file CHIP8 emulator built for readability and teachability,

a tiny virtual machine (Chip8VM) that implements the CHIP8 spec,

a display that draws 64x32 monochrome pixels via SDL2,

a keypad that maps pc keys to the 16key CHIP8 hex keypad,

an App loop that ties input, VM, and rendering together and ticks timers 60 Hz,

no external ROMs or BIOS, no hidden macros,

Build (Linux/macOS with SDL2):

g++ -std=c++20 -O2 -Wall -Wextra -pedantic main.cpp -lSDL2 -lSDL2_image -o chip8

Run with a ROM:

./chip8 path/to/rom


 
