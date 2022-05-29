# VersaTerm
A versatile serial terminal

# Building from source

Requires: 
- CMake 3.12 or later
- GCC (cross-)compiler: arm-none-eabi-gcc

Steps:
```
git clone https://github.com/dhansel/VersaTerm.git
cd VersaTerm/lib
git submodule update --init
cd pico-sdk/lib
git submodule update --init
cd ../../..
mkdir build
cd build
cmake .. -DPICO_SDK_PATH=../lib/pico-sdk -DPICO_COPY_TO_RAM=1
make
```
