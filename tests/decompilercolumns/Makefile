include ../../config.mk

.PHONY: all check
all:
	$(MAKE) -C ../.. -j8 > /dev/null
	mkdir -p build
	cd build && $(QMAKE) ../decompilercolumns.pro
	$(MAKE) -C build -j8 > /dev/null

check: all
	QT_QPA_PLATFORM=offscreen ./build/test_decompilercolumns
