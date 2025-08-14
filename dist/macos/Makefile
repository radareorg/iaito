.PHONY: all app dmg zip extra clean mrproper

all: clean app dmg
app: disk/iaito.app
dmg: iaito.dmg
zip: iaito.zip
extra: extra/r2ai extra/r2ghidra

ARCH:=$(shell uname -m)

disk/iaito.app: ../../build/iaito.app radare2-unpkg extra/r2ai extra/r2ghidra
	mkdir disk
# Copy base bundle app
	cp -a ../../build/iaito.app disk/
# Copy r2 binary plugins
	mkdir -p disk/iaito.app/Contents/PlugIns/radare2
	install -p -m 0755 \
		extra/r2ghidra/core_ghidra.dylib \
		disk/iaito.app/Contents/PlugIns/radare2/
# Copy r2 pkg files and update linking path for all binary files
	scripts/embed-radare2.sh radare2-unpkg disk/iaito.app
# Copy r2 non-binary plugins
	install -p -m 0644 \
		extra/r2ai/decai/decai.r2.js \
		disk/iaito.app/Contents/Resources/radare2/lib/radare2/last/
# 	cp -a extra/r2ghidra_sleigh \
# 		disk/iaito.app/Contents/Resources/radare2/share/
# Copy QT frameworks
	macdeployqt disk/iaito.app -verbose=2

iaito.dmg: disk/iaito.app
	cp doc/README.txt disk/READ_THIS_FIRST.txt
	ln -fs /Applications disk/
	hdiutil create -format UDZO -fs APFS -volname iaito -srcfolder disk iaito

iaito.zip:
	cp doc/README.txt disk/READ_THIS_FIRST.txt
	ditto -c -k --rsrc --extattr --sequesterRsrc disk iaito.zip

radare2-unpkg: radare2.pkg
	pkgutil --expand-full $< $@

radare2.pkg:
	echo "Download first radare2.pkg from https://github.com/radareorg/radare2/releases"
	@false

extra/r2ai:
	mkdir -p extra
	curl -L "$(shell gh api /repos/radareorg/r2ai/releases/latest --jq .tarball_url)" | tar -xzC extra/
	cd extra && mv radareorg-r2ai-* r2ai

extra/r2ghidra:
	mkdir -p extra
	curl -Lo r2ghidra-latest.zip "$(shell gh api /repos/radareorg/r2ghidra/releases/latest --jq '.assets[] | select(.name | endswith("-macos-$(ARCH).zip")) | .browser_download_url')"
	unzip r2ghidra-latest.zip
	rm r2ghidra-latest.zip
	unzip r2ghidra-*-macos-$(ARCH).zip -d extra/
	cd extra && mv r2ghidra-*-macos-* r2ghidra

../../build/iaito.app:
	echo "Building iaito..."
	$(MAKE) -C ../.. QMAKE_FLAGS=IAITO_BUNDLE_R2_APPBUNDLE=true

clean:
	rm -rf radare2-unpkg disk iaito.dmg

mrproper: clean
	rm -rf radare2.pkg r2ghidra-*.zip extra
