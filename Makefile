CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic

.PHONY: all clean ui
all: demo

demo: main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread main.c $(LDFLAGS) $(LDLIBS) -o $@

clean:
	rm -f demo

ui: demo
	python3 server.py
