# Repository notes

- This is an ESP-IDF C project (`project(agbot)`); build from an initialized ESP-IDF environment with `idf.py build`. The root `CMakeLists.txt` loads `$IDF_PATH/tools/cmake/project.cmake`, so standalone CMake without the ESP-IDF environment is not the build path.
- `main/agbot.c` is the firmware entrypoint (`app_main`). `agBLE/` contains a separate component stub (`func`), not called by `app_main`.
- `agBLE/` is at the repository root, outside ESP-IDF's default `components/` directory. The root CMake file does not set `EXTRA_COMPONENT_DIRS`, so register this directory before expecting its source or public header to participate in a build.
- No target is pinned in the repository (no `sdkconfig` or defaults file); select the intended chip with `idf.py set-target <target>` when setting up a build.
