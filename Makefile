# UDR for Linux, against the system's libudt (libudt-dev, 4.13 or later) and
# OpenSSL (libssl-dev). `make install` honours DESTDIR and prefix.

VERSION  := $(shell cat VERSION)

CXX      ?= g++
CXXFLAGS ?= -O2 -g
CPPFLAGS += -I/usr/include/udt $(shell pkg-config --cflags openssl) -DLINUX -Isrc
CXXFLAGS += -Wall
LDLIBS   += -ludt $(shell pkg-config --libs openssl) -lpthread

prefix   ?= /usr
bindir   ?= $(prefix)/bin
mandir   ?= $(prefix)/share/man

OBJS = src/udr.o src/udr_threads.o src/udr_util.o src/udr_options.o

all: udr

src/version.h: VERSION
	printf '#ifndef VERSION_H\n#define VERSION_H\nstatic const char * version = "%s";\n#endif\n' '$(VERSION)' > $@

src/%.o: src/%.cpp src/version.h $(wildcard src/*.h)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

udr: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

install: udr
	install -D -m 0755 udr $(DESTDIR)$(bindir)/udr
	install -D -m 0644 udr.1 $(DESTDIR)$(mandir)/man1/udr.1

check: udr
	tests/smoke.sh ./udr

clean:
	rm -f $(OBJS) src/version.h udr

.PHONY: all install check clean
