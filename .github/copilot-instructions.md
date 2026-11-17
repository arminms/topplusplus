# Project Guidelines

## Build and Test

- Requirements: CMake 3.25.2+ and a C++23 compiler (see README for supported compiler versions). The presets use Ninja.
- Configure the project with tests enabled: `cmake --preset default -DTOPPP_ENABLE_TESTS=ON`.
- When `VCPKG_INSTALLATION_ROOT` is set and vcpkg dependencies are preferred, configure with `cmake --preset vcpkg -DTOPPP_ENABLE_TESTS=ON`.
- Build with `cmake --build build`.
- Run all unit tests with `ctest --test-dir build --output-on-failure`.
- Run one Catch2 test case through CTest, for example: `ctest --test-dir build -R "Pushing and popping preserves FIFO order" --output-on-failure`. Replace the expression with the test case name or a matching regular expression.
- CMake looks for installed FTXUI, hwinfo, and (when tests are enabled) Catch2 before fetching missing dependencies. `TOPPP_ENABLE_STATIC=ON` builds FTXUI and hwinfo from source for release packaging; hwinfo's fetch tracks its `main` branch.

## Architecture

- `src/top++.cpp` assembles the FTXUI full-screen application, gathers host/CPU/GPU inventory through hwinfo, manages tabs and input events, and advances the monitor from the UI loop.
- `src/monitor.hpp` owns CPU, GPU and memory sampling, the latest snapshot and histories; the UI reads these rather than sampling independently. Construction records an initial sample, and `tick()` samples again when its count exceeds `tick_max`.
- `src/circular_buffer.hpp` holds up to 64 samples in oldest-first logical order, overwriting the oldest when full. CPU, GPU and memory plots use these histories, with a 60-sample display window.
- `src/cpu_elements.hpp` and `src/detail/cpu_elements.hpp` build per-socket and per-core displays; `src/gpu_elements.hpp` builds GPU displays. Shared colors, canvas background and UI helpers live in `src/commons.hpp`.
- `src/scroll_view.hpp` implements the custom vertical viewport. Its state is owned by the application and updated by the UI event handler; layout clamps the offset and records the viewport for mouse events.
- `test/` contains the Catch2 unit tests, discovered and run through CTest.

## Codebase Conventions

- Compose terminal output with FTXUI elements and decorators such as `hbox`, `vbox`, `text`, `gauge`, and `|`. Keep interactive state in the app and capture it in renderer/event lambdas.
- Canvas nodes overwrite whole cells: use `detail::paint_background` before drawing plots rather than relying on `bgcolor`.
- Hardware queries that return `hwinfo::result` must be checked before accessing their values; display the result's error message in the UI when a query fails.
- Keep CPU presentation in the `socket_content` family and scrolling behavior in `scroll_view`; avoid reproducing either in `src/top++.cpp`.
- Put implementation details that may change in the `detail` namespace and `src/detail/`, keeping presentation entry points in their corresponding headers.
- Follow the C++23 and formatting patterns in the file being changed. Unit tests use Catch2 `TEST_CASE`/`REQUIRE` macros and are registered with `catch_discover_tests`.
