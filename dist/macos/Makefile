.PHONY: all app clean

all: app iaito.dmg

app: ../../build/iaito.app
	mkdir disk
	cp -a ../../build/iaito.app disk/
	macdeployqt disk/iaito.app -verbose=2

iaito.dmg: app
	cp doc/README.txt disk/READ_THIS_FIRST.txt
	ln -fs /Applications disk/
	hdiutil create -format UDZO -fs APFS -volname iaito -srcfolder disk iaito

../../build/iaito.app:
	echo "Building iaito..."
	$(MAKE) -C ../..

clean:
	rm -rf disk iaito.dmg
