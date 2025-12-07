.PHONY: all app dmg zip unpack-comps clean mrproper

all: clean app dmg
app: disk/iaito.app
dmg: iaito.dmg
zip: iaito.zip
unpack-comps: radare2-unpkg extra/r2ai extra/r2ghidra extra/r2ghidra_sleigh

disk/iaito.app: ../../build/iaito.app unpack-comps
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
	cp -a extra/r2ghidra_sleigh \
		disk/iaito.app/Contents/Resources/radare2/share/
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
	touch $@

radare2.pkg:
	echo "Download first radare2.pkg from https://github.com/radareorg/radare2/releases"
	@false

r2ai.tar.gz:
	echo "Download first r2ai.tar.gz (source) from https://github.com/radareorg/r2ai/releases"
	@false

extra/r2ai: r2ai.tar.gz
	mkdir -p extra
	rm -rf extra/r2ai extra/r2ai-* extra/radareorg-r2ai-*
	tar -f $< -xzC extra/
	cd extra && mv *r2ai-* r2ai
	touch $@

r2ghidra.zip:
	echo "Download first r2ghidra.zip (binary) from https://github.com/radareorg/r2ghidra/releases"
	@false

extra/r2ghidra: r2ghidra.zip
	mkdir -p extra
	rm -rf extra/r2ghidra extra/r2ghidra-*-macos-*
	unzip $< -d extra/
	cd extra && mv r2ghidra-*-macos-* r2ghidra
	touch $@

r2ghidra_sleigh.zip:
	echo "Download first r2ghidra_sleigh.zip from https://github.com/radareorg/r2ghidra/releases"
	@false

extra/r2ghidra_sleigh: r2ghidra_sleigh.zip
	mkdir -p extra
	rm -rf extra/r2ghidra_sleigh*
	unzip $< -d extra/
	cd extra && mv r2ghidra_sleigh-* r2ghidra_sleigh
	touch $@

../../build/iaito.app:
	echo "Building iaito..."
	$(MAKE) -C ../.. QMAKE_FLAGS=IAITO_BUNDLE_R2_APPBUNDLE=true

clean:
	rm -rf radare2-unpkg disk iaito.dmg

mrproper: clean
	rm -rf radare2.pkg r2ai.tar.gz r2ghidra*.zip extra
