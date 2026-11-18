include ../../config.mk

.PHONY: all check
all:
	mkdir -p build
	cd build && $(QMAKE) ../package_manager.pro
	$(MAKE) -C build

check: all
	QT_QPA_PLATFORM=offscreen ./build/test_package_manager
