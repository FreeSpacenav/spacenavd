# change the prefix to install elsewhere
PREFIX = /usr/local

obj = spnavd.o cfgfile.o
bin = spacenavd
ctl = spnavd_ctl

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
	$(INSTALL) -m 755 $(ctl) $(PREFIX)/bin/$(ctl)
	./setup_init

# -- this is not done automatically any more, see README.
#	$(INSTALL) -m 755 init_script /etc/init.d/$(bin)
#	cd /etc/rc2.d && rm -f S99$(bin) && ln -s ../init.d/$(bin) S99$(bin)
#	if [ -z "`grep $(ctl) /etc/X11/Xsession`" ]; then echo "$(PREFIX)/bin/$(ctl) x11 start" >>/etc/X11/Xsession; fi

.PHONY: uninstall
uninstall:
	rm -f $(PREFIX)/bin/$(bin)
	rm -f $(PREFIX)/bin/$(ctl)
	./setup_init remove

#	rm -f /etc/init.d/$(bin)
#	rm -f /etc/rc2.d/S99$(bin)
