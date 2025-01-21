IMPORTANT!

Steps to install iaito:

1. First go to radare2 releases:
    https://github.com/radareorg/radare2/releases/latest

2. Download the corresponding pkg file:
    Apple Silicon (all models): radare2-m1-*.pkg
    Intel CPU: radare2-x64-*.pkg

3. Run the pkg file either by double-click or in Terminal by running:
    sudo installer -pkg radare2-*.pkg -target /

4. Drag and drop the iaito icon to /Applications.

5. Run this commands from Terminal:
    sudo xattr -c /Applications/iaito.app
    sudo codesign --force --deep --sign - /Applications/iaito.app
