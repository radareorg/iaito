include config.mk

ifeq ($(shell uname),Darwin)
BIN=build/iaito.app/Contents/MacOS/iaito
else
BIN=build/iaito
endif

#QMAKE_FLAGS+=IAITO_ENABLE_PYTHON=false
QMAKE_FLAGS+=-config release

#install_name_tool -change /path/to/Qt/lib/QtCore.framework/Versions/4.0/QtCore
#        @executable_path/../Frameworks/QtCore.framework/Versions/4.0/QtCore
#       plugandpaint.app/Contents/plugins/libpnp_basictools.dylib

ifeq ($(WANT_PYTHON),1)
QMAKE_FLAGS+=IAITO_ENABLE_PYTHON=true
endif
ifeq ($(WANT_PYTHON_BINDINGS),1)
QMAKE_FLAGS+=IAITO_ENABLE_PYTHON_BINDINGS=true
endif

all: iaito

clean:
	rm -rf build

dist:
ifeq ($(shell uname),Darwin)
	$(MAKE) -C dist/macos
else
	$(MAKE) -C dist/debian
endif

mrproper: clean
	git clean -xdf

.PHONY: install run user-install dist macos clean mrproper install-translations

# force qt5 build when QtCreator is installed in user's home
ifeq ($(shell test -x ~/Qt/5.*/clang_64/bin/qmake || echo err),)
QMAKE=~/Qt/5.*/clang_64/bin/qmake
endif

iaito: build
	$(MAKE) -C build -j4

build:
	mkdir -p build
	cd build && $(QMAKE) ../src/Iaito.pro $(QMAKE_FLAGS)

install-man:
	mkdir -p "${DESTDIR}${MANDIR}/man1"
	for FILE in src/*.1 ; do ${INSTALL_MAN} "$$FILE" "${DESTDIR}${MANDIR}/man1" ; done

install: build
ifeq ($(shell uname),Darwin)
	rm -rf $(DESTDIR)/Applications/iaito.app
	mkdir -p $(DESTDIR)/Applications
	cp -rf build/iaito.app $(DESTDIR)/Applications/iaito.app
	mkdir -p $(DESTDIR)/$(PREFIX)/bin
	ln -fs  '/Applications/iaito.app/Contents/MacOS/iaito' $(DESTDIR)/$(PREFIX)'/bin/iaito'
else
	$(MAKE) -C build install INSTALL_ROOT=$(DESTDIR)
endif
	$(MAKE) install-man

uninstall:
ifeq ($(shell uname),Darwin)
	rm -rf $(DESTDIR)/Applications/iaito.app
	rm -rf "$(DESTDIR)/$(PREFIX)/bin/iaito"
else
	rm -rf "$(DESTDIR)/$(PREFIX)/share/iaito"
	rm -rf "$(DESTDIR)/$(PREFIX)/bin/iaito"
endif
	rm -f "${DESTDIR}$(MANDIR)/man1/iaito.1"

user-install: build
ifeq ($(shell uname),Darwin)
	rm -rf ${HOME}/Applications/iaito.app
	mkdir -p ${HOME}/Applications
	cp -rf build/iaito.app ${HOME}/Applications/iaito.app
else
	$(MAKE) -C build install INSTALL_ROOT=/ PREFIX=${HOME}/.local
endif
	$(MAKE) install-man DESTDIR=/ PREFIX=${HOME}/.local MANDIR=${HOME}/.local/share/man

user-uninstall:
	$(MAKE) uninstall DESTDIR=/ PREFIX=${HOME}/.local MANDIR=${HOME}/.local/share/man

run:
	rarun2 libpath=$(shell r2 -H R2_LIBDIR) program=$(BIN)

src/translations/README.md:
	git clone https://github.com/radareorg/iaito-translations.git src/translations

install-translations: src/translations/README.md
	$(MAKE) -C src/translations install PREFIX="$(DESTDIR)/$(PREFIX)"

user-install-translations: src/translations/README.md
	$(MAKE) -C src/translations user-install
