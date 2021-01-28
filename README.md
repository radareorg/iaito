<img width="150" height="150" align="left" style="float: left; margin: 0 10px 0 0;" alt="Cutter logo" src="https://raw.githubusercontent.com/radareorg/r2cutter/master/src/img/r2cutter.svg?sanitize=true">

# r2cutter

r2cutter is the continuation of [Cutter](https://cutter.re) before the fork to keep [radare2](https://github.com/radareorg/radare2) as backend.

* Focus on supporting latest version of radare2
* Recommend the use of system installed
* Closer integration between r2 and the UI

![r2cutter CI](https://github.com/radareorg/r2cutter/workflows/r2cutter%20CI/badge.svg)

![Screenshot](https://raw.githubusercontent.com/radareorg/r2cutter/master/docs/source/images/screenshot.png)

## Downloading a release

r2cutter is available for Linux, macOS and Windows.
Get the builds from the [releases](https://github.com/radareorg/r2cutter/releases) page in Github.

## Installing dependencies

r2cutter depends on r2, you should install it

```
$ git clone https://github.com/radareorg/radare2
$ cd radare2 ; sys/install.sh
```

Extra dependencies are needed for macOS

```
brew install qt5
```

On Ubuntu/Debian

```
sudo apt install qttools5-dev-tools make 
```

## Building from sources

```
./configure
make
make run
```

## Docker

To deploy *cutter* using a pre-built `Dockerfile`, it's possible to use the [provided configuration](docker). The corresponding `README.md` file also contains instructions on how to get started using the docker image with minimal effort.

```
make -C docker
```

## Plugins
r2cutter supports both Python and Native C++ plugins. For now the api is compatible with Cutter. Read the [Plugins](https://cutter.re/docs/plugins) section on their documentation.

## Help

Get help, updates, meet the community or discuss about development in these channels:

- **Telegram:** https://t.me/radare
- **IRC:** #radare and #cutter on irc.freenode.net
- **Twitter:** [@radareorg](https://twitter.com/radareorg)
