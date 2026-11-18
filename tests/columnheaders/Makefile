include ../../config.mk

.PHONY: all check
all:
	mkdir -p build
	cd build && $(QMAKE) ../columnheaders.pro
	$(MAKE) -C build -j8 > /dev/null

check: all
	QT_QPA_PLATFORM=offscreen ./build/test_columnheaders
