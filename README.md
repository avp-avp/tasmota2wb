# tasmota2wb
Convert tasmota mqtt notation to wirenboard notation

### Required for compilation: 
libjsoncpp-dev
libmosquittopp-dev

## How to build

- git clone git@github.com:avp-avp/tasmota2wb.git
- cd tasmota2wb
- git submodule update --init --recursive
- autoreconf -fvi
- ./configure
- make
- dpkg-buildpackage -us -uc -b

## How to build for wirenboard

You need Wirenboard development env from https://github.com/contactless/wirenboard 

The recommended way to build the Debian package in the Wiren Board container is:

- git submodule update --init --recursive
- export WBDEV_TARGET=wb6
- ../wbdev ./debian/build-package.sh

This helper builds the package in a temporary copy of the source tree inside the
mounted repository, so `dpkg-buildpackage` artifacts are preserved after the
container exits. Resulting files are written to `dist/`:

- `dist/tasmota2wb_*.deb`
- `dist/tasmota2wb_*.changes`
- `dist/tasmota2wb_*.buildinfo`

If you need to run the Debian package build without `wbdev`, use:

- git submodule update --init --recursive
- dpkg-buildpackage -us -uc -b

## How to run on wirenboard

- scp dist/tasmota2wb_*.deb root@\<ip of wirenboard>:/tmp/
- ssh root@\<ip of wirenboard> "apt install /tmp/tasmota2wb_*.deb && systemctl enable --now tasmota2wb"
