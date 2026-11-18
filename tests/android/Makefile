-include ../../config.mk
QMAKE ?= qmake6

.PHONY: check
check:
	mkdir -p build
	cd build && $(QMAKE) ../android.pro
	$(MAKE) -C build -j8 > /dev/null
	./build/test_android
	python3 test_package.py
