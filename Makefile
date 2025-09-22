# Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c23 -D_GNU_SOURCE
LIBS = -lcurl -ljansson -lwebsockets -lpthread

# Source files
SOURCES = discord.c src/bot_example.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = discord_bot

.PHONY: all clean install-deps-ubuntu install-deps-fedora install-deps-arch

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LIBS)

%.o: %.c discord.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install-deps-ubuntu:
	sudo apt-get update
	sudo apt-get install libcurl4-openssl-dev libjansson-dev libwebsockets-dev

install-deps-fedora:
	sudo dnf install libcurl-devel jansson-devel libwebsockets-devel

install-deps-arch:
	sudo pacman -S curl jansson libwebsockets
