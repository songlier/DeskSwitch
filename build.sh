windres icon.rc -o icon.o

g++ DeskSwitch.c icon.o -o .\DeskSwitch\DeskSwitch.exe -O2 -Wall -mwindows -luser32 -lkernel32 -lwinmm -lstdc++