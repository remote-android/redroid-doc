# Native Bridge Support in ReDroid

There are several choose to support native bridge (typically for arm) in *ReDroid*
- `libndk_translation` from Arm
- `libhoudini` from Intel
- `QEMU`

Prepare the resource and build the image.
And run `docker build --build-arg BASE_IMAGE=<Base Image> .`

```bash
# example structure, be careful the file owner and mode

system/
├── bin
│   ├── arm
│   └── arm64
├── etc
│   ├── binfmt_misc
│   └── init
├── lib
│   ├── arm
│   └── libnb.so
└── lib64
    ├── arm64
    └── libnb.so

# grab libndk_translation from Android 11 Emulator
find /system \( -name 'libndk_translation*' -o -name '*arm*' -o -name 'ndk_translation*' \) | tar -cf native-bridge.tar -T - 

```

Example: Patch libndk_translation in ReDroid 11

![Screenshot of ReDroid 11 with libndk_translation](./redroid_11_libndk_translation.png)
