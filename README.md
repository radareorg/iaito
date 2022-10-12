<img width="150" height="150" align="left" style="float: left; margin: 0 10px 0 0;" alt="Iaito logo" src="https://raw.githubusercontent.com/radareorg/iaito/master/src/img/iaito-circle.svg?sanitize=true">

# iaito

iaito is the official graphical interface for radare2, a libre reverse engineering framework.

* Iaito was the original name of this GUI before being forked as Cutter.
* It's written in Qt/C++ (qt5 for now). No Qt6 support yet (contribs are welcome)
* Support latest versions, plugins and features of radare2
* Use r2 plugins (f.ex: no need for r2ghidra-iaito plugin if r2ghidra is installed)
* Focus on parity of commands, keybindings and r2-style workflows.
* Translations are community [contributed](https://crowdin.com/project/iaito)!
* Aims to support all the features from the core, not just disassembler-based ones
  * forensics, networking, bindiffing, solvers, ...

[![Crowdin](https://badges.crowdin.net/iaito/localized.svg)](https://crowdin.com/project/iaito)
[![iaito CI](https://github.com/radareorg/iaito/workflows/iaito%20CI/badge.svg)](https://github.com/radareorg/iaito/actions)
[![Linux packages](https://repology.org/badge/vertical-allrepos/iaito.svg?columns=4)](https://repology.org/project/iaito/versions)

![Screenshot](https://raw.githubusercontent.com/radareorg/iaito/master/screenshot.png)

## Installation

There are automated CI builds and [releases](https://github.com/radareorg/iaito/releases) of iaito for Linux, macOS and Windows.

<a href='https://flathub.org/apps/details/org.radare.iaito'><img width='120' alt='Download on Flathub' src='https://flathub.org/assets/badges/flathub-badge-en.png'/></a>

### Translations

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

## Source Builds

### Dependencies

iaito requires [radare2](https://github.com/radareorg/radare2) and qt5:

```sh
$ git clone https://github.com/radareorg/radare2
$ radare2/sys/install.sh
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

## Building from sources

```sh
./configure
make
make run
```

To install the app in your home:

```sh
make install
```

## Help

Get help, updates, meet the community or discuss about development in these channels:

- **Telegram:** https://t.me/radare
- **IRC:** #radare on irc.freenode.net
- **Twitter:** [@radareorg](https://twitter.com/radareorg)
