include config.mk

ifeq ($(shell uname),Darwin)
BIN=build/r2cutter.app/Contents/MacOS/r2cutter
else
BIN=build/r2cutter
endif

# linux or win
CUTTER_OS=macos
CUTTER_DEPS_URL=https://github.com/rizinorg/cutter-deps/releases/download/v13/cutter-deps-$(CUTTER_OS).tar.gz

ifeq ($(WANT_PYTHON),1)
QMAKE_FLAGS+=CUTTER_ENABLE_PYTHON=true

# cutter deps should provide deps.mk
CMAKE_FLAGS+=-DPYTHON_LIBRARY="$(CUTTER_DEPS)/python/lib/libpython3.6m.so.1.0"
CMAKE_FLAGS+=-DPYTHON_INCLUDE_DIR="$(CUTTER_DEPS)/include/python3.6m"
CMAKE_FLAGS+=-DPYTHON_EXECUTABLE="$(CUTTER_DEPS)/bin/python3"
CMAKE_FLAGS+=-DCUTTER_ENABLE_PYTHON=ON
CMAKE_FLAGS+=-DCUTTER_ENABLE_PYTHON_BINDINGS=ON
endif
ifeq ($(WANT_PYTHON_BINDINGS),1)
QMAKE_FLAGS+=CUTTER_ENABLE_PYTHON_BINDINGS=true
endif
ifeq ($(WANT_CRASH_REPORTS),1)
QMAKE_FLAGS+=CUTTER_ENABLE_CRASH_REPORTS=true
endif

all: r2cutter

cbuild:
	mkdir -p cbuild
	cd cbuild && cmake $(CMAKE_FLAGS) \
	-DCMAKE_BUILD_TYPE=Release \
	-DCUTTER_ENABLE_CRASH_REPORTS=ON \
	-DCUTTER_USE_BUNDLED_RADARE2=OFF \
	-DCUTTER_APPIMAGE_BUILD=ON \
	-DCMAKE_INSTALL_PREFIX=appdir/usr \
	../src

clean:

mrproper: clean
	rm -rf build

.PHONY: cmake install build run user-install clean mrproper

cmake: cbuild
	$(MAKE)

r2cutter: translations
	$(MAKE) -C build -j4

translations: build src/translations/README.md
	lrelease src/Cutter.pro

src/translations/README.md:
	git submodule update --init

# force qt5 build when QtCreator is installed in user's home
ifeq ($(shell test -x ~/Qt/5.12.3/clang_64/bin/qmake || echo err),)
QMAKE=~/Qt/5.12.3/clang_64/bin/qmake
endif

build:
	mkdir -p build
	cd build && $(QMAKE) ../src/Cutter.pro $(QMAKE_FLAGS)

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
