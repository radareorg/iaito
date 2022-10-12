PACKAGE_DIR=$(shell pwd)
include ./CONFIG
DEPENDS=

SUDO=fakeroot
SUDO=sudo
SUDO=

all:
	$(SUDO) rm -rf control data
	$(MAKE) clean
	$(MAKE) root
	mkdir -p data
	cp -aRf root/* data
	rm -rf data$(LIBDIR)/pkgconfig
	# rm -rf data$(LIBDIR)/*.a
	# rm -rf data$(INCLUDEDIR)
	$(MAKE) control
	$(MAKE) deb

root:
	cd ../../.. && $(MAKE) && $(MAKE) install install-translations PREFIX=/usr DESTDIR=/tmp/iaito-deb
	mv /tmp/iaito-deb root

include ../deb_hand.mak
