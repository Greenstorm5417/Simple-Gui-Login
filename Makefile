PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
IMAGE = assets/void-logo.png
PYTHON = python3
CFLAGS = -Os
CPPFLAGS += -D_GNU_SOURCE -D_FORTIFY_SOURCE=3 -I. -Ibuild
BUILD_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -fstack-protector-strong -MMD -MP
BUILD_LDFLAGS = -Wl,-z,relro,-z,now
OBJECTS = build/main.o build/platform.o build/draw.o build/session.o

.PHONY: all check install uninstall clean FORCE
all: build/green-login

build:
	mkdir -p $@

build/logo.h: tools/embed-image.py $(IMAGE) FORCE | build
	$(PYTHON) tools/embed-image.py "$(IMAGE)" $@.tmp
	cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@

build/draw.o: build/logo.h

build/%.o: %.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(BUILD_CFLAGS) -c $< -o $@

build/green-login: $(OBJECTS)
	$(CC) $(LDFLAGS) $(BUILD_LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS) -lpam

build/check-keys: tests/keys.c platform.c login.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(BUILD_CFLAGS) $(LDFLAGS) $(BUILD_LDFLAGS) -o $@ tests/keys.c platform.c $(LDLIBS)

check: build/check-keys
	./build/check-keys

install: build/green-login
	install -d "$(DESTDIR)$(BINDIR)"
	install -s -m 755 build/green-login "$(DESTDIR)$(BINDIR)/green-login"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/green-login"

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)
