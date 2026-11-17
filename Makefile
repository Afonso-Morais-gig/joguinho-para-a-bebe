CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lm

SRC = src/main.c
TARGET = joguinho

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(SRC) -o bin/$(TARGET) $(LDFLAGS)

clean:
	rm -rf bin/$(TARGET)

run: $(TARGET)
	./bin/$(TARGET)

# Android: fetch SDL2 + Java glue, then build the debug APK
android-setup:
	./scripts/setup_android.sh

android: android-setup
	cd android && gradle wrapper && ./gradlew assembleDebug

.PHONY: all clean run android-setup android
