.PHONY: all app dmg clean mrproper

all: clean app dmg

app: ../../build/iaito.app radare2-unpkg extra/r2ai
	mkdir disk
	cp -a ../../build/iaito.app disk/
	scripts/embed-radare2.sh radare2-unpkg disk/iaito.app
	cp -a extra/r2ai/decai/decai.r2.js disk/iaito.app/Contents/Resources/radare2/lib/radare2/last/
	macdeployqt disk/iaito.app -verbose=2

dmg: app
	cp doc/README.txt disk/READ_THIS_FIRST.txt
	ln -fs /Applications disk/
	hdiutil create -format UDZO -fs APFS -volname iaito -srcfolder disk iaito

radare2-unpkg: radare2.pkg
	pkgutil --expand-full $< $@

radare2.pkg:
	echo "Download first radare2.pkg from https://github.com/radareorg/radare2/releases"
	@false

extra/r2ai:
	mkdir -p extra
	curl -L "$(shell curl -s https://api.github.com/repos/radareorg/r2ai/releases/latest | jq -r .tarball_url)" | tar -xzC extra/
	cd extra && mv radareorg-r2ai-* r2ai

../../build/iaito.app:
	echo "Building iaito..."
	$(MAKE) -C ../.. QMAKE_FLAGS=IAITO_BUNDLE_R2_APPBUNDLE=true

clean:
	rm -rf radare2-unpkg disk iaito.dmg

mrproper: clean
	rm -rf radare2.pkg extra
