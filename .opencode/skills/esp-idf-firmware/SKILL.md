---
name: esp-idf-firmware
description: Guides agbot ESP-IDF C firmware and build work; use ONLY when implementing, reviewing, or documenting firmware, component registration, or ESP-IDF build setup in this project.
---

# ESP-IDF Firmware

Ground firmware changes and build instructions in the actual `agbot` project layout.

## When to use

- Implement or review ESP-IDF C code, component CMake files, or root build configuration.
- Document or answer questions about the firmware, component registration, chip target, or ESP-IDF build.
- Do not use this skill for OpenCode-only tasks unless the agent or skill being written describes this project's firmware or build behavior; exclude unrelated documentation.

## Workflow

1. Read `AGENTS.md`, root `CMakeLists.txt`, and the affected component sources and CMake files before changing behavior or asserting build facts.
2. Treat `main/agbot.c` as the `app_main` entrypoint. It currently contains an empty stub. `agBLE/agBLE.c` implements `func` declared in `agBLE/include/agBLE.h`; it is not currently called by `app_main`.
3. `agBLE/` lives at the repository root, outside ESP-IDF's default `components/` location. Its CMake file declares a component, but the root CMake file does not register that directory via `EXTRA_COMPONENT_DIRS`. Register it before depending on it, and declare any needed component dependency from `main` when wiring it in.
4. Follow existing C/CMake conventions and ESP-IDF component APIs. Do not claim BLE initialization, connectivity, GPIO assignments, a specific board, or a configured target unless the sources and configuration actually establish them.
5. Build only with an initialized ESP-IDF environment (`IDF_PATH` set and tools available). Select a target with `idf.py set-target <target>` only when the intended chip is known; this repository does not pin a target or ship `sdkconfig` defaults. Then use `idf.py build`. Do not substitute standalone CMake for the ESP-IDF build.
6. Review the relevant diff and run checks appropriate to the change when the environment and permissions allow. If a build cannot run, identify the missing prerequisite rather than claiming success.

## Output

Describe changed firmware/build files, relevant checks and results, and any unverified hardware assumptions. Keep documentation claims limited to implemented behavior.
