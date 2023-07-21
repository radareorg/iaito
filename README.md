Just a fork to create latest builds, as building locally on windows is a pain
<img width="150" height="150" align="left" style="float: left; margin: 0 10px 0 0;" alt="Iaito logo" src="https://raw.githubusercontent.com/radareorg/iaito/master/src/img/iaito-circle.svg?sanitize=true">

# iaito

iaito is the official graphical interface for radare2, a libre reverse engineering framework.

* Requires radare2 and Qt-5/6
* Iaito was the original name of this GUI before being forked as Cutter.
* Use r2 plugins (f.ex: no need for r2ghidra-iaito plugin if r2ghidra is installed)
* Focus on simplicity, parity with commands, keybindings and r2-style workflows.
* Help with translations [contributed](https://crowdin.com/project/iaito)!
* Aims to cover other radare2 features, not just a disassembler
  * forensics, networking, bindiffing, solvers, ...

[![Crowdin](https://badges.crowdin.net/iaito/localized.svg)](https://crowdin.com/project/iaito)
[![iaito CI](https://github.com/radareorg/iaito/workflows/iaito%20CI/badge.svg)](https://github.com/radareorg/iaito/actions)
[![Linux packages](https://repology.org/badge/vertical-allrepos/iaito.svg?columns=4)](https://repology.org/project/iaito/versions)

![Screenshot](https://raw.githubusercontent.com/radareorg/iaito/master/screenshot.png)

## Help

Get help, updates, meet the community or discuss about development in these channels:

- **Telegram:** https://t.me/radare
- **IRC:** #radare on irc.freenode.net
- **Twitter:** [@radareorg](https://twitter.com/radareorg)

## Installation

There are automated CI builds and [releases](https://github.com/radareorg/iaito/releases) of iaito for Linux, macOS and Windows.

On WSL or Linux you can use these steps to install it with Flatpak:

```sh
sudo flatpak remote-add flathub https://dl.flathub.org/repo/flathub.flatpakrepo
sudo flatpak install flathub org.radare.iaito
export QT_QPA_PLATFORM=wayland   # only mandatory on windows
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

