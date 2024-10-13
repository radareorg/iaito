# iaito: use radare2 with one hand!

iaito is the official graphical interface for radare2, a libre reverse engineering framework.

<img width="150" height="150" align="left" style="float: left; margin: 0 30px 0 0;" alt="Iaito logo" src="https://raw.githubusercontent.com/radareorg/iaito/master/src/img/iaito.svg?sanitize=true">

* Based on radare2 and Qt-5/6
* Iaito was the original name before being forked as Cutter.
* Use all your favourite r2 plugins and scripts (nothing is specific to iaito)
* Focus on simplicity, parity with commands, features, keybindings
* Forensics, bindiffing, binary patching, .. not just a static disassembler

[![Crowdin](https://badges.crowdin.net/iaito/localized.svg)](https://crowdin.com/project/iaito)
[![iaito CI](https://github.com/radareorg/iaito/workflows/iaito%20CI/badge.svg)](https://github.com/radareorg/iaito/actions)

![Screenshot](https://raw.githubusercontent.com/radareorg/iaito/master/screenshots/macos-panels.png)

## Help

Get help, updates, meet the community or discuss about development in these channels:

- **Discord:** [https://discord.gg/6RwDEJFr](https://discord.gg/6RwDEJFr)
- **Telegram:** https://t.me/radare
- **Mastodon:** [@radareorg](https://infosec.exchange/@radareorg)
- **Translations:** [crowdin](https://crowdin.com/project/iaito)!


## Installation

There are automated CI builds and [releases](https://github.com/radareorg/iaito/releases) of iaito for Linux, macOS and Windows.

On WSL or Linux you can use these steps to install it with Flatpak:

```sh
sudo flatpak remote-add flathub https://dl.flathub.org/repo/flathub.flatpakrepo
sudo flatpak install flathub org.radare.iaito
flatpak run org.radare.iaito
```

<a href='https://flathub.org/apps/details/org.radare.iaito'><img width='120' alt='Download on Flathub' src='https://flathub.org/assets/badges/flathub-badge-en.png'/></a>

## Source Builds

### Dependencies

iaito requires [radare2](https://github.com/radareorg/radare2) and qt5 (or qt6):

```sh
$ git clone https://github.com/radareorg/radare2 && radare2/sys/install.sh
```

Install QT on macOS like this:

```sh
brew install qt@5
echo 'export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"' >> ~/.profile
```

On Ubuntu/Debian

```sh
sudo apt install qttools5-dev qttools5-dev-tools qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools libqt5svg5-dev make pkg-config build-essential
```

For QT6:

```sh
sudo apt install qt6-svg-dev
```

### Compilation

```sh
./configure
make
make run
```

To install the app in your home:

```sh
make install
```

### Building iaito as an r2 plugin

It is also possible to launch iaito from the r2 shell reusing the Core instance:

```
make plugin
```

The previous command will build iaito, the iaito plugin and copy it it into your home plugin directory.

```
$ r2 /bin/ls
> iaito
...
```

### Translations (Optional)

Note: The flatpak/flathub version already ships the translations as an optional extension.

To install translations please download latest version from [iaito-translations](https://github.com/radareorg/iaito-translations) repo.

To install in your home directory, run the following:

```sh
$ git clone https://github.com/radareorg/iaito-translations.git
$ cd iaito-translations
$ make user-install
```

Alternatively, it this project makefile can be used to install them:

```sh
$ make user-install-translations
OR
$ make install-translations # to install to system
```

### Binaries

Most distros may Checkout the [Releases](https://github.com/radareorg/iaito/releases) page to download the build for Linux, macOS or Windows, and read the notes in case of problems!

As shown below, most distros are packaging iaito already, pick the build you like the most!

[![Linux packages](https://repology.org/badge/vertical-allrepos/iaito.svg?columns=4)](https://repology.org/project/iaito/versions)

Snap:

[![iaito snap](https://snapcraft.io/iaito/badge.svg)](https://snapcraft.io/iaito)
