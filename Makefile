# change the prefix to install elsewhere
PREFIX = /usr/local

obj = spnavd.o
bin = spacenavd

CC = gcc
INSTALL = install
CFLAGS = -pedantic -Wall -g -DUSE_X11
LDFLAGS = -lX11 -lm

$(bin): $(obj)
	$(CC) $(CFLAGS) -o $@ $(obj) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)

.PHONY: install
install: $(bin)
	$(INSTALL) -d $(PREFIX)/bin
	$(INSTALL) -m 755 $(bin) $(PREFIX)/bin/$(bin)
	$(INSTALL) -m 755 init_script /etc/init.d/$(bin)
	cd /etc/rc2.d && rm -f S99$(bin) && ln -s ../init.d/$(bin) S99$(bin)

.PHONY: uninstall
uninstall:
	rm -f $(PREFIX)/bin/$(bin)
	rm -f /etc/init.d/$(bin)
	rm -f /etc/rc2.d/S99$(bin)
