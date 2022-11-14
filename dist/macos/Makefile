all:
	rm -f ../../build/iaito.dmg
	cd ../../build && macdeployqt iaito.app -dmg && cp -f iaito.dmg ../dist/macos

app:
	$(SHELL) make-macos-app.sh

pkg:
	$(SHELL) make-macos-pkg.sh

clean:
	rm -rf macos-pkg tmp
