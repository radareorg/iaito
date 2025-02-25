IMPORTANT!

To install iaito you can drag and drop the application to /Applications.
But then you are required to run this commands from Terminal before you can execute for the first time.

sudo xattr -c /Applications/iaito.app
sudo codesign --force --deep --sign - /Applications/iaito.app
