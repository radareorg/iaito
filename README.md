<img width="150" height="150" align="left" style="float: left; margin: 0 10px 0 0;" alt="Iaito logo" src="https://raw.githubusercontent.com/radareorg/iaito/master/src/img/iaito-circle.svg?sanitize=true">

# iaito

iaito is the official graphical interface for radare2, a libre reverse engineering framework.


It is the continuation of [Cutter](https://cutter.re) before the [fork](https://github.com/rizinorg/cutter) to keep [radare2](https://github.com/radareorg/radare2) as backend.

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


![Screenshot](https://raw.githubusercontent.com/radareorg/iaito/master/docs/source/images/screenshot.png)

## Downloading a release

iaito is available for Linux, macOS and Windows.
Get the builds from the [releases](https://github.com/radareorg/iaito/releases) page in Github.

On Windows, you will need to have the corresponding radare2 Windows [release](https://github.com/radareorg/radare2/releases/) to make iaito work: Copy the `share/` directory and the contents of the `bin/` directory of the radare2 release into the root directory of the iaito release which contains `iaito.exe`. For example, assuming you have both radare2 and iaito releases extracted in the same directory, run the powershell commands:
```
cp .\radare2-5.3.1-w64\bin\* -Destination .\iaito-w64\
cp .\radare2-5.3.1-w64\share\ -Destination .\iaito-w64\
```


## Installing dependencies

iaito depends on r2, and you should install it from git:

```
$ git clone https://github.com/radareorg/radare2
$ cd radare2 ; sys/install.sh
```

Extra dependencies are needed for macOS, see the .github/workflows/ci.yml for more details

```
brew install qt@5
```

On Ubuntu/Debian

```
sudo apt install qttools5-dev-tools qt5-default libqt5svg5-dev make
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
