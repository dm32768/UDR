# UDR for Linux. By default it builds UDT from the submodule in udt/ and
# links it statically, so a recursive clone builds on any Linux with
# OpenSSL (libssl-dev) and pkg-config. With SYSTEM_UDT=1 it links the
# system's libudt (libudt-dev, 4.13 or later) instead, which is how the
# Debian package is built. `make install` honours DESTDIR and prefix.

VERSION  := $(shell cat VERSION)

CXX      ?= g++
CXXFLAGS ?= -O2 -g
CPPFLAGS += $(shell pkg-config --cflags openssl) -DLINUX -Isrc
CXXFLAGS += -Wall
LDLIBS   += $(shell pkg-config --libs openssl) -lpthread

SYSTEM_UDT ?=
ifeq ($(SYSTEM_UDT),)
UDT_DIR     = udt
UDT_LIB     = $(UDT_DIR)/libudt.a
CPPFLAGS   += -I$(UDT_DIR)/src
UDT_LDLIBS  = $(UDT_LIB)
else
UDT_LIB     =
CPPFLAGS   += -I/usr/include/udt
UDT_LDLIBS  = -ludt
endif

prefix   ?= /usr
bindir   ?= $(prefix)/bin
mandir   ?= $(prefix)/share/man

OBJS = src/udr.o src/udr_threads.o src/udr_util.o src/udr_options.o

all: udr

src/version.h: VERSION
	printf '#ifndef VERSION_H\n#define VERSION_H\nstatic const char * version = "%s";\n#endif\n' '$(VERSION)' > $@

src/%.o: src/%.cpp src/version.h $(wildcard src/*.h) | udt-present
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

# The submodule is empty after a plain clone.
udt-present:
ifeq ($(SYSTEM_UDT),)
	@test -f $(UDT_DIR)/src/udt.h || { \
	    echo "udt/ is empty: run 'git submodule update --init', or build with SYSTEM_UDT=1 against libudt-dev" >&2; \
	    exit 1; }
endif

$(UDT_DIR)/libudt.a: udt-present
	$(MAKE) -C $(UDT_DIR) libudt.a

udr: $(OBJS) $(UDT_LIB)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(UDT_LDLIBS) $(LDLIBS)

install: udr
	install -D -m 0755 udr $(DESTDIR)$(bindir)/udr
	install -D -m 0644 udr.1 $(DESTDIR)$(mandir)/man1/udr.1

check: udr
	tests/smoke.sh ./udr

clean:
	rm -f $(OBJS) src/version.h udr
	-test -f $(UDT_DIR)/Makefile && $(MAKE) -C $(UDT_DIR) clean

.PHONY: all install check clean udt-present
