# Build instructions

## Windows (MinGW)

* Copy the following files to the `examples\3dcam` folder:
  - From raylib distribution:
    - `raylib.dll`
    - `libraylibdll.a`
    - `raylib.h`
  - From MinGW distribution
    - `libwinpthead-1.dll`
* Add MinGW paths to the `PATH` environment variable
* Run `build_3dcam_windows_mingw.bat`
* Run `3dcam.exe`

## Linux

* Copy the following files to the `examples/3dcam` folder:
  - From raylib distribution (assuming the latest raylib version to be 4.5):
    - `libraylib.so`
    - `libraylib.so.4.5.0`
    - `libraylib.so.450`
    - `raylib.h`
* Run `./build_3dcam_linux.sh`
* Run `./3dcam`
