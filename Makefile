# Kiosque 32 : terminal Minitel pour Windows 95 à 11 (i686, sans bibliothèque C).
# Les fichiers produits vont hors de ~/Documents (synchronisé par iCloud).
BUILD  ?= $(HOME)/Library/Caches/kiosque32/build
DIST   ?= $(HOME)/Documents/dev/livrables/kiosque32
CROSS  ?= i686-w64-mingw32-
CC      = $(CROSS)gcc
WINDRES = $(CROSS)windres

SRC     = win32.c vdt.c fb.c font.c proto.c mini.c
HDR     = fb.h font.h ids.h mini.h proto.h vdt.h
CFLAGS  = -Os -march=i486 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns \
          -fno-stack-protector -fno-asynchronous-unwind-tables -fno-ident \
          -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 -Wall -Wextra -Wno-unused-parameter \
          -Wno-cast-function-type
# Sous-système et système « 4.0 » : sans cela, Windows 95, 98, NT 4 et 2000
# refusent de lancer le programme (« n'est pas une application Win32 valide »).
LDFLAGS = -nostdlib -s -Wl,-e,_entry -Wl,--subsystem,windows:4.0 \
          -Wl,--major-os-version,4 -Wl,--minor-os-version,0 \
          -Wl,--disable-dynamicbase -Wl,--disable-nxcompat -Wl,--no-insert-timestamp
LIBS    = -lkernel32 -luser32 -lgdi32 -lwsock32 -lgcc

EXE = $(BUILD)/kiosque32.exe

all: $(EXE)

# Les sources sont en UTF-8 ; Windows 9x/2000 attend du Windows-1252.
$(BUILD)/src/%: src/%
	@mkdir -p $(dir $@)
	iconv -f UTF-8 -t CP1252 $< > $@

src/font.c: tools/mkfont.py fonts/unscii-8.hex
	python3 tools/mkfont.py fonts/unscii-8.hex $@

src/kiosque.ico: tools/mkicon.py
	python3 tools/mkicon.py $@

$(BUILD)/src/kiosque.ico: src/kiosque.ico
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILD)/%.o: $(BUILD)/src/%.c $(addprefix $(BUILD)/src/,$(HDR))
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/res.o: $(BUILD)/src/kiosque.rc $(BUILD)/src/ids.h $(BUILD)/src/kiosque.ico
	cd $(BUILD)/src && $(WINDRES) -c 1252 -i kiosque.rc -o ../res.o

$(EXE): $(patsubst %.c,$(BUILD)/%.o,$(SRC)) $(BUILD)/res.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	@ls -l $@

# Liste des fonctions Windows importées (toutes doivent exister sous Windows 95).
imports: $(EXE)
	$(CROSS)objdump -p $(EXE) | grep -E "DLL Name|^\s+[0-9a-f]+\s+[0-9]+\s+\w" 

# Banc d'essai sur Mac : test/headless hôte port telnet|ws chemin sortie.ppm secondes
test: $(BUILD)/../test/headless
$(BUILD)/../test/headless: tests/headless.c src/vdt.c src/fb.c src/font.c src/proto.c
	@mkdir -p $(dir $@)
	cc -O1 -Wall -o $@ $^

dist: $(EXE)
	@mkdir -p $(DIST)
	cp $(EXE) $(DIST)/KIOSQUE32.EXE

clean:
	rm -rf $(BUILD)

.PHONY: all imports test dist clean

.SECONDARY:
