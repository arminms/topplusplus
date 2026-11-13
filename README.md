[![Build and Test (Linux/macOS/Windows)](https://github.com/arminms/topplusplus/actions/workflows/cmke-multiplatform.yml/badge.svg)](https://github.com/arminms/topplusplus/actions/workflows/cmke-multiplatform.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
# top++
Multiplatform system information and monitoring tool in `C++23`.

## Features
* Multiplatform (Linux/macOS/Windows)
* Responsive interface
* Single self-contained executable
* Easy one line installation command on all platforms

## Installation
Run one of the following commands depending on your platform (macOS requires superuser unless you install it in a local folder in your path). Once done, you can run it by entering `top++`.

### Linux
```sh
curl -sSL https://github.com/arminms/topplusplus/releases/latest/download/top++-linux-x86_64.tar.gz | tar -xz -C ~/.local
```

### macOS
```sh
sudo curl -sSL https://github.com/arminms/topplusplus/releases/latest/download/top++-macos-universal.tar.gz | tar -xz -C /usr/local
```

### Windows
```sh
curl -sSL https://github.com/arminms/topplusplus/releases/latest/download/top++-windows-x86_64.tar.gz | tar --strip-components=1 -xz -C %USERPROFILE%\AppData\Local\Microsoft\WindowsApps
```

## Building from source
### Requirements
* C++23 compiler
  - GCC 14+
  - Clang 20+ (libstdc++ 14+ or libc++ 20+)
  - Apple Clang 16+ (Xcode 16)
  - MSVC 19.40+ (Visual Studio 2022 17.10)
  - MinGW-w64 with GCC 14+
* CMake 3.25.2+

```sh
git clone https://github.com/arminms/topplusplus.git
cd topplusplus
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTOPPP_ENABLE_STATIC=ON
cmake --build build --config Release --parallel
cmake --install build
```
