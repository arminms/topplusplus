[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
# top++
Multiplatform system information and monitoring tool in `C++23`.

## Building packages

Build on the target platform with static dependencies enabled:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTOPPP_ENABLE_STATIC=ON
cmake --build build --config Release
```

Then run the appropriate CPack generator from the build directory (the `-C`
option also selects the configuration for multi-configuration generators):

```sh
cpack --config build/CPackConfig.cmake -C Release -G DEB        # Debian/Ubuntu
cpack --config build/CPackConfig.cmake -C Release -G RPM        # RPM-based Linux
cpack --config build/CPackConfig.cmake -C Release -G DragNDrop  # macOS DMG
cpack --config build/CPackConfig.cmake -C Release -G WIX        # Windows MSI
```

The DEB generator requires `dpkg-shlibdeps`; RPM requires `rpmbuild`; DMG
requires the macOS packaging tools; and MSI requires WiX Toolset v3 on PATH
(CPack's default). `cmake --build build --config Release --target package` runs
the platform's default generators (both DEB and RPM on Linux), so all their
tools must be installed. CPack writes packages to the current directory unless
`-B <directory>` is supplied.

Each package contains the `top++` executable under `bin`. The DMG is not an
application bundle: copy the executable from its `bin` directory to a
directory on your PATH. The MSI installs it under the chosen installation
directory's `bin` directory; add that directory to PATH to run it by name.

Static mode links the project's dependencies into the executable, but still
uses platform system libraries (including libc on Linux). On Linux, the C++
runtime is linked statically when supported by the compiler. Linux packages
declare detected system-library dependencies; optional GPU libraries may be
loaded at runtime. macOS builds are ad-hoc signed, not notarized, and Windows
installers are not signed. Package builds are only enabled in static mode.