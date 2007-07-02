obj = spnavd.o
bin = spacenavd

CC = gcc
CFLAGS = -pedantic -Wall -g -DUSE_X11
LDFLAGS = -lX11

$(bin): $(obj)
	$(CC) $(CFLAGS) -o $@ $(obj) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)
