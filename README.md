# tasmota2wb
Convert tasmota mqtt notation to wirenboard notation

## How to build

- git clone git@github.com:avp-avp/tasmota2wb.git
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

- scp tasmota2wb.service root@wirenboard:/etc/systemd/system/
- scp tasmota2wb/zigbee2wb root@wirenboard:/usr/bin/tasmota2wb
- scp tasmota2wb.json root@wirenboard:/etc/tasmota2wb.conf
- ssh root@wirenboard "service tasmota2wb start"
