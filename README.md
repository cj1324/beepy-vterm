# beepy-vterm

## Build

```
cd [project]
./build.sh
./sdl 
```

## HW

* beepy from https://beepy.sqfmi.com/
* raspbeey pi zero 2w
* 3dprinter case

## Deps

```
  libSDL2-2.0.so.0 => /usr/local/lib/libSDL2-2.0.so.0 (0x0000ffff852c3000)
  libvterm.so.0 => /lib/aarch64-linux-gnu/libvterm.so.0 (0x0000ffff852a4000)
  libSDL2_ttf-2.0.so.0 => /usr/local/lib/libSDL2_ttf-2.0.so.0 (0x0000ffff85284000)
  libutil.so.1 => /lib/aarch64-linux-gnu/libutil.so.1 (0x0000ffff85270000)
  libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x0000ffff850fc000)
  libm.so.6 => /lib/aarch64-linux-gnu/libm.so.6 (0x0000ffff85051000)
  libdl.so.2 => /lib/aarch64-linux-gnu/libdl.so.2 (0x0000ffff8503d000)
  libpthread.so.0 => /lib/aarch64-linux-gnu/libpthread.so.0 (0x0000ffff8500c000)
  /lib/ld-linux-aarch64.so.1 (0x0000ffff8551a000)
  libfreetype.so.6 => /lib/aarch64-linux-gnu/libfreetype.so.6 (0x0000ffff84f47000)
  libpng16.so.16 => /lib/aarch64-linux-gnu/libpng16.so.16 (0x0000ffff84f00000)
  libz.so.1 => /lib/aarch64-linux-gnu/libz.so.1 (0x0000ffff84ed6000)
  libbrotlidec.so.1 => /lib/aarch64-linux-gnu/libbrotlidec.so.1 (0x0000ffff84eb9000)
  libbrotlicommon.so.1 => /lib/aarch64-linux-gnu/libbrotlicommon.so.1 (0x0000ffff84e88000)
```

## Unicode FONT

* load font path: `/usr/share/fonts/truetype/Unifont/Unifont-Medium.ttf`
* download by : `https://ftp.gnu.org/gnu/unifont/unifont-15.1.01/`


## SDL rebuild

```
cmake  ../SDL2-2.28.3/   -DSDL_WAYLAND=OFF -DSDL_VULKAN=OFF -DSDL_DUMMYAUDIO=OFF -DSDL_WAYLAND=OFF  -DSDL_DUMMYAUDIO=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF  -DSDL_X11=OFF
```

## Base

* https://gist.github.com/min4builder/0a044cf8b1d5d60d8fcce10ee24993fe
* https://gist.github.com/shimarin/71ace40e7443ed46387a477abf12ea70

