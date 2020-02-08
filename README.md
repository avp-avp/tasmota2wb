# tasmota2wb
Convert tasmota mqtt notation to wirenboard notation

### Required for compilation: 
libjsoncpp-dev
libmosquittopp-dev

## How to build

- git clone git@github.com:avp-avp/tasmota2wb.git
- cd tasmota2wb
- git submodule init  
- git submodule update
- autoreconf -fvi
- ./configure
- make

## How to build for wirenboard

You need Wirenboard development env from https://github.com/contactless/wirenboard 

Run bould command with devenv chroot
- export WBDEV_TARGET=wb6
- ../wbdev chroot autoreconf -fvi
- ../wbdev chroot ./configure
- ../wbdev chroot make

## How to run on wirenboard

- scp tasmota2wb.service root@<ip of wirenboard>:/etc/systemd/system/
- scp tasmota2wb/tasmota2wb root@<ip of wirenboard>:/usr/bin/tasmota2wb
- scp tasmota2wb.json root@<ip of wirenboard>:/etc/tasmota2wb.conf
- ssh root@<ip of wirenboard> "service tasmota2wb start"
