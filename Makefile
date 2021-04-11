include config.mk

ifeq ($(shell uname),Darwin)
BIN=build/iaito.app/Contents/MacOS/iaito
else
BIN=build/iaito
endif

# linux or win
IAITO_OS=macos
IAITO_DEPS_URL=https://github.com/rizinorg/cutter-deps/releases/download/v13/cutter-deps-$(IAITO_OS).tar.gz

ifeq ($(WANT_PYTHON),1)
QMAKE_FLAGS+=IAITO_ENABLE_PYTHON=true

# cutter deps should provide deps.mk
CMAKE_FLAGS+=-DPYTHON_LIBRARY="$(IAITO_DEPS)/python/lib/libpython3.6m.so.1.0"
CMAKE_FLAGS+=-DPYTHON_INCLUDE_DIR="$(IAITO_DEPS)/include/python3.6m"
CMAKE_FLAGS+=-DPYTHON_EXECUTABLE="$(IAITO_DEPS)/bin/python3"
CMAKE_FLAGS+=-DIAITO_ENABLE_PYTHON=ON
CMAKE_FLAGS+=-DIAITO_ENABLE_PYTHON_BINDINGS=ON
endif
ifeq ($(WANT_PYTHON_BINDINGS),1)
QMAKE_FLAGS+=IAITO_ENABLE_PYTHON_BINDINGS=true
endif
ifeq ($(WANT_CRASH_REPORTS),1)
QMAKE_FLAGS+=IAITO_ENABLE_CRASH_REPORTS=true
endif

all: iaito

cbuild:
	mkdir -p cbuild
	cd cbuild && cmake $(CMAKE_FLAGS) \
	-DCMAKE_BUILD_TYPE=Release \
	-DIAITO_ENABLE_CRASH_REPORTS=ON \
	-DIAITO_USE_BUNDLED_RADARE2=OFF \
	-DIAITO_APPIMAGE_BUILD=ON \
	-DCMAKE_INSTALL_PREFIX=appdir/usr \
	../src

clean:

mrproper: clean
	rm -rf build

.PHONY: cmake install build run user-install clean mrproper translations

cmake: cbuild
	$(MAKE)

iaito: translations
	$(MAKE) -C build -j4

translations: build src/translations/README.md
	$(MAKE) -C src/translations
#	lrelease src/Iaito.pro

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
	$(MAKE) -C src/translations install

user-install:

run:
	$(BIN)
