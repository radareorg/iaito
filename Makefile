include config.mk

ifeq ($(shell uname),Darwin)
BIN=build/iaito.app/Contents/MacOS/iaito
else
BIN=build/iaito
endif

ifeq ($(WANT_PYTHON),1)
QMAKE_FLAGS+=IAITO_ENABLE_PYTHON=true
endif
ifeq ($(WANT_PYTHON_BINDINGS),1)
QMAKE_FLAGS+=IAITO_ENABLE_PYTHON_BINDINGS=true
endif

all: iaito

clean:
	rm -rf build

mrproper: clean
	git clean -xdf

.PHONY: install build run user-install clean mrproper translations

iaito: translations
	$(MAKE) -C build -j4

translations: build src/translations/README.md
	$(MAKE) -C src/translations build

src/translations/README.md:
	git submodule update --init

# force qt5 build when QtCreator is installed in user's home
ifeq ($(shell test -x ~/Qt/5.12.3/clang_64/bin/qmake || echo err),)
QMAKE=~/Qt/5.12.3/clang_64/bin/qmake
endif

build:
	mkdir -p build
	cd build && $(QMAKE) ../src/Iaito.pro $(QMAKE_FLAGS)

install: translations
ifeq ($(shell uname),Darwin)
	rm -rf $(DESTDIR)/Applications/iaito.app
	mkdir -p $(DESTDIR)/Applications
	cp -rf build/iaito.app $(DESTDIR)/Applications/iaito.app
	mkdir -p $(DESTDIR)/usr/local/bin
	ln -fs  '/Applications/iaito.app/Contents/MacOS/iaito' $(DESTDIR)'/usr/local/bin/iaito'
else
	$(MAKE) -C build install INSTALL_ROOT=$(DESTDIR)
endif
	$(MAKE) -C src/translations install PREFIX="$(DESTDIR)/$(PREFIX)"

uninstall:
ifeq ($(shell uname),Darwin)
	rm -rf $(DESTDIR)/Applications/iaito.app
	mkdir -p $(DESTDIR)/Applications
	rm -rf "$(DESTDIR)/usr/local/bin/iaito"
else
	rm -rf "$(DESTDIR)/$(PREFIX)/share/iaito"
	rm -rf "$(DESTDIR)/$(PREFIX)/bin/iaito"
endif

user-install:

run:
	rarun2 libpath=$(shell r2 -H R2_LIBDIR) program=$(BIN)
