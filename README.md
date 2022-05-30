# VersaTerm
A versatile serial terminal

# Building from source

## Requirements
- CMake 3.12 or later
- GCC (cross-)compiler: arm-none-eabi-gcc

## Getting and building the source

```
git clone https://github.com/dhansel/VersaTerm.git
cd VersaTerm/software/lib
git submodule update --init
cd pico-sdk/lib
git submodule update --init
cd ../../..
mkdir build
cd build
cmake .. -DPICO_SDK_PATH=../lib/pico-sdk -DPICO_COPY_TO_RAM=1
make
```

This should create file VersaTerm/software/build/src/VersaTerm.uf2

## TinyUSB updates

The version of TinyUSB currently (May 2022) included with the Pico SDK appears to have problems 
with USB hubs. These issues seem to be resolved in the latest updates though.
To update TinyUSB to the latest version, do the following:
```
cd VersaTerm/lib/pico-sdk/lib/tinyusb
git fetch
git merge origin/master
```
Then just "cd" back to VersaTerm/software/build and type "make" to re-build.
