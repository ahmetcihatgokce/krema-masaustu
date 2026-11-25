# GNU make >= 4.3, Python with Pillow, and the x86_64 MinGW-w64 toolchain.
PREFIX ?= x86_64-w64-mingw32-
CC := $(PREFIX)gcc
RC := $(PREFIX)windres
PYTHON ?= python
CFLAGS := -std=c11 -O2 -Wall -Wextra
LDLIBS := -luser32 -lgdi32 -lshell32 -lwinmm -lm
MEDIA := $(wildcard media/*.png media/*.wav)

Krema.exe: src/winmain.c src/pet.c src/pet.h src/art.h src/media_data.h src/assets.S assets/krema.bin Krema.res.o
	$(CC) $(CFLAGS) -mwindows -municode -static-libgcc -Wl,--nxcompat,--dynamicbase,--high-entropy-va -o $@ src/winmain.c src/pet.c src/assets.S Krema.res.o $(LDLIBS)

src/media_data.h assets/krema.bin assets/Krema.ico &: tools/pack.py $(MEDIA)
	$(PYTHON) tools/pack.py

Krema.res.o: src/Krema.rc src/Krema.manifest assets/Krema.ico
	$(RC) -I . -i src/Krema.rc -o $@

interactions.exe: tests/interactions.c src/pet.c src/pet.h src/art.h src/media_data.h src/assets.S assets/krema.bin
	$(CC) -std=c11 -O2 -Wall -Wextra -o $@ tests/interactions.c src/pet.c src/assets.S -lm

.PHONY: test
test: interactions.exe
	./interactions.exe
