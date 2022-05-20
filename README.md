<img width="150" height="150" align="left" style="float: left; margin: 0 10px 0 0;" alt="Iaito logo" src="https://raw.githubusercontent.com/radareorg/iaito/master/src/img/iaito-circle.svg?sanitize=true">

# iaito

iaito is the official graphical interface for radare2, a libre reverse engineering framework.

* Iaito was the original name of this GUI before being forked as Cutter.
* It's written in Qt/C++ (qt5 for now). No Qt6 support yet
* Support latest versions and features of radare2
* Use r2 plugins (f.ex: no need for r2ghidra-iaito plugin if r2ghidra is installed)
* Focus on parity of commands and r2-style workflows.
* Translations are in the early steps, please [contribute](https://crowdin.com/project/iaito)!
* Aims to support all the features from the core, not just disassembler-based ones
  * forensics, networking, bindiffing, solvers, ...

[![Crowdin](https://badges.crowdin.net/iaito/localized.svg)](https://crowdin.com/project/iaito)
[![iaito CI](https://github.com/radareorg/iaito/workflows/iaito%20CI/badge.svg)](https://github.com/radareorg/iaito/actions)
[![Linux packages](https://repology.org/badge/vertical-allrepos/iaito.svg?columns=4)](https://repology.org/project/iaito/versions)


![Screenshot](https://raw.githubusercontent.com/radareorg/iaito/master/screenshot.png)

## Installation

There are automated CI builds and [releases](https://github.com/radareorg/iaito/releases) of iaito for Linux, macOS and Windows.

## Source Builds

### Dependencies

iaito requires [radare2](https://github.com/radareorg/radare2) and qt5:

```
$ git clone https://github.com/radareorg/radare2
$ radare2/sys/install.sh
```

Install QT on macOS like this:

```
brew install qt@5
echo 'export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"' >> ~/.profile
```

On Ubuntu/Debian

```
sudo apt install qttools5-dev qttools5-dev-tools qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools libqt5svg5-dev make pkg-config build-essential
```

## Building from sources

```
./configure
make
make run
```

To install the app and the translations in your home:

```
make install
```

## Plugins
iaito supports both Python and Native C++ plugins. For now the api is compatible with Iaito. Read the [Plugins](https://cutter.re/docs/plugins) section on their documentation.

## Help

Get help, updates, meet the community or discuss about development in these channels:

- **Telegram:** https://t.me/radare
- **IRC:** #radare on irc.freenode.net
- **Twitter:** [@radareorg](https://twitter.com/radareorg)
