## Uploading the VersaTerm firmware to the Raspberry Pi Pico

Uploading firmware to the Raspberry Pi Pico is easy:
- Press and hold the button on the Raspberry Pi Pico (there is only one) 
- While holding the button, connect the Raspberry Pi Pico via its micro-USB port to your computer
- Release the button
- Your computer should recognize the Pico as a storage device (like a USB stick) and mount it as a drive
- Copy the [VersaTerm.uf2](software/VersaTerm.uf2) file to the drive mounted in the previous step

## Building the VersaTerm firmware from source

### Requirements
- CMake 3.12 or later
- GCC (cross-)compiler: arm-none-eabi-gcc

### Getting and building the source

```
git clone https://github.com/dhansel/VersaTerm.git
cd VersaTerm/software/lib
git submodule update --init
cd pico-sdk/lib
git submodule update --init
cd tinyusb
git checkout 86ad6e5
cd ../../../..
mkdir build
cd build
cmake .. -DPICO_SDK_PATH=../lib/pico-sdk -DPICO_COPY_TO_RAM=1
make
```

The `git checkout 86ad6e5` command updates TinyUSB to version 0.18 instead of version 0.12
which was included with the pico-sdk version used by VersaTerm. Version 0.12 has issues
with (some) USB hubs which are resolved in 0.18.

This should create file VersaTerm/software/build/src/VersaTerm.uf2<br>
Follow the "Uploading firmware to Raspberry Pi Pico" instructions above to upload the .uf2 file to the Pico.

The instructions above will use the of pico-sdk and PicoDVI versions that were
current when I wrote and tested VersaTerm. Building from those sources should result in
the same VerTerm.uf2 file as the one in VersaTerm/software.

If you feel adventurous you can build VersaTerm with the latest versions of the libraries:
```
git clone https://github.com/dhansel/VersaTerm.git
cd VersaTerm/software/lib
git submodule update --init
cd pico-sdk/lib
git submodule update --init
git submodule update --remote --merge
cd ../..
git submodule update --remote --merge
cd ..
mkdir build
cd build
cmake .. -DPICO_SDK_PATH=../lib/pico-sdk -DPICO_COPY_TO_RAM=1
make
```

## Some solutions to compile issues

A big thank you to user [unclouded](https://github.com/un-clouded) who reported a number of compile-time 
issues and their solutions:

When it said to me:

    arm-none-eabi-gcc: fatal error: cannot read spec file 'nosys.specs': No such file or directory

I replied:

    apt install libnewlib-arm-none-eabi

And when it said:

    fatal error: cassert: No such file or directory

I retorted:

    apt install libstdc++-arm-none-eabi-dev

And then it complained that:

    /usr/lib/gcc/arm-none-eabi/12.2.1/../../../arm-none-eabi/bin/ld: cannot find -lstdc++: No such file or directory

And I spake thusly:

    apt install libstdc++-arm-none-eabi-newlib
