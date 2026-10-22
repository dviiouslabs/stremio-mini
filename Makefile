# Host build:   make            (needs SDL 1.2 + SDL_image + SDL_ttf dev packages)
# Device build: inside the Miyoo Mini toolchain container, just run `make`
#               (it provides CROSS_COMPILE and sdl-config for the target).
CC := $(CROSS_COMPILE)gcc
SDL_CFLAGS := $(shell sdl-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl-config --libs 2>/dev/null || echo -lSDL) -lSDL_image -lSDL_ttf
CFLAGS += -O2 -Wall -Wextra -std=gnu99 $(SDL_CFLAGS)
OBJS := src/main.o src/addon.o src/json.o src/net.o src/account.o src/torrent.o
OUT  := App/Stremio/stremio-mini

all: $(OUT)

$(OUT): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) $(SDL_LIBS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: test/test_addon.c src/addon.c src/json.c src/net.c src/account.c src/torrent.c
	$(CC) -std=gnu99 -g -fsanitize=address,undefined -o test/test_addon $^
	python3 test/mock_addon.py 8765 & SRV=$$!; sleep 1; \
	NO_PROXY=127.0.0.1 ./test/test_addon; RC=$$?; kill $$SRV; exit $$RC

# Zip for the SD card: unzip into <SD>/App/
dist: all
	./package.sh

clean:
	rm -f $(OBJS) $(OUT) test/test_addon

.PHONY: all test dist clean
