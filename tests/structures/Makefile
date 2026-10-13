include ../../config.mk

.PHONY: all check
all:
	$(MAKE) -C ../..
	mkdir -p build
	cd build && $(QMAKE) ../structures.pro
	$(MAKE) -C build

check: all
	QT_QPA_PLATFORM=offscreen ./build/test_structures
