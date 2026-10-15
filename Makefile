PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

CC = cc
PKG_CONFIG = pkg-config
PYTHON = python3

IMAGE = assets/void-logo.png
PKGS = pam
CFLAGS = -O2 -std=c11 -Wall -Wextra -pedantic -fstack-protector-strong -MMD -MP $(shell $(PKG_CONFIG) --cflags $(PKGS))
CPPFLAGS += -D_GNU_SOURCE -D_FORTIFY_SOURCE=3 -I. -Ibuild
LDFLAGS = -Wl,-z,relro,-z,now
LDLIBS = $(shell $(PKG_CONFIG) --libs $(PKGS))
OBJECTS = build/main.o build/platform.o build/draw.o build/session.o

.PHONY: all install uninstall clean
all: build/green-login

build:
	mkdir -p $@

build/logo.h: tools/embed-image.py $(IMAGE) | build
	$(PYTHON) tools/embed-image.py "$(IMAGE)" $@.tmp
	cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@

build/draw.o: build/logo.h

build/%.o: %.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/green-login: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

install: build/green-login
	install -d "$(DESTDIR)$(BINDIR)"
	install -s -m 755 build/green-login "$(DESTDIR)$(BINDIR)/green-login"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/green-login"

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
