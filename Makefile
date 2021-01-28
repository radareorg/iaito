include config.mk

ifeq ($(shell uname),Darwin)
BIN=build/r2cutter.app/Contents/MacOS/r2cutter
else
BIN=build/r2cutter
endif

all: r2cutter

r2cutter: translations
	$(MAKE) -C build -j4

translations: build
	lrelease src/Cutter.pro

build:
	mkdir -p build
	cd build && $(QMAKE) ../src/Cutter.pro

install:
ifeq ($(shell uname),Darwin)
	mkdir -p $(DESTDIR)/Applications
	cp -rf build/r2cutter.app $(DESTDIR)/Applications/r2cutter.app
	mkdir -p $(DESTDIR)/usr/local/bin
	ln -fs  '/Applications/r2cutter.app/Contents/MacOS/r2cutter' $(DESTDIR)'/usr/local/bin/r2cutter'
else
	$(MAKE) -C build install INSTALL_ROOT=$(DESTDIR)
endif

user-install:

run:
	$(BIN)
