CC := gcc
CFLAGS := -g -Wall -Werror -Wno-unused-function -Wno-unused-variable

all: p2psnap

clean:
	rm -f p2psnap

p2psnap: p2psnap.c ui.c ui.h
	$(CC) $(CFLAGS) -o p2psnap p2psnap.c ui.c -lform -lncurses -lpthread -O `GraphicsMagick-config --cppflags --ldflags --libs`