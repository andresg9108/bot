---
name: esp-idf-reusable-components
description: Guides reusable ESP-IDF C component scaffolding and integration; use ONLY when creating, adapting, or reviewing components, their public APIs, dependencies, or cross-project reuse.
---

# Reusable ESP-IDF Components

Develop components that can be built and consumed independently of the `agbot` application while accurately describing what is actually wired into its build.

## When to use

- The user asks to create a component with `idf.py create-component <name>` or to turn existing code into a reusable ESP-IDF component.
- The task involves component discovery, public headers, dependency boundaries, consumption from `main`, or reuse in another ESP-IDF project.
- Do not use this skill for unrelated firmware changes, README-only edits, or OpenCode configuration changes.

## Workflow

1. Load `esp-idf-firmware` for project facts and read `AGENTS.md`, root and affected component `CMakeLists.txt` files, public headers, and relevant C sources. Check the installed ESP-IDF version and the command's help if available; do not assume the generated template is identical across releases. Ask for a component name, destination, API, or hardware target only when it cannot be inferred safely.
2. Choose placement deliberately: `components/<name>/` is the project's conventional discoverable component location. For a component at a custom location, add its directory (or its parent component directory) to `EXTRA_COMPONENT_DIRS` in root `CMakeLists.txt` **before** `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`. For example, `set(EXTRA_COMPONENT_DIRS "agBLE")` would register the existing root-level component if requested; the current root CMake file has no such setting. Avoid naming collisions with built-in or other project components.
3. With an initialized ESP-IDF environment and permission to run commands, use `idf.py create-component <name>` from the project as the starting scaffold for a new component. Inspect `idf.py create-component --help` for destination options such as `-C` in the installed version when placing it elsewhere. Confirm the resulting path and files before adding code; if the tool is unavailable or commands are not permitted, create the equivalent component files only when requested and say the CLI scaffold was not run. Never overwrite a component already present.
4. Keep the component's public contract in `include/<name>.h` (or an existing project-appropriate public header) and implementation in one or more `.c` files; adjust to the actual generated layout. Name public symbols consistently and avoid pulling application-only state or `app_main` into a reusable component. Specify inputs, ownership, initialization and cleanup, error behavior, and concurrency assumptions where relevant. Add C++ linkage guards only if C++ consumption is required. Do not introduce hardware or platform assumptions without explicit requirements.
5. Register only real files and paths in the component's `CMakeLists.txt`, for example `idf_component_register(SRCS "<name>.c" INCLUDE_DIRS "include")`. Public headers must live in an `INCLUDE_DIRS` directory; implementation-only headers can use `PRIV_INCLUDE_DIRS`. Use `REQUIRES` for dependencies exposed through public headers and `PRIV_REQUIRES` for implementation-only or link-time dependencies. Avoid undeclared dependencies, application-specific paths, unnecessary component-manager manifests, or hardcoded chip features. Check dependency names and APIs against the ESP-IDF version in use.
6. When integration is requested, verify the component is discovered before including its public header from `main/`. Add `PRIV_REQUIRES <name>` in `main/CMakeLists.txt` when making the consumer relationship explicit (especially useful for reuse or restricted component builds); ESP-IDF's special `main` component automatically requires discovered components in ordinary builds, but this does not discover root-level `agBLE/` by itself. Only call component functions from `app_main` if asked to wire runtime behavior.
7. For reuse, identify the files to move as a unit, place the component under another project's `components/` or register a custom location via `EXTRA_COMPONENT_DIRS`, declare any IDF/third-party requirements, and check any target-, configuration-, or IDF-version-specific APIs. A generated scaffold alone does not demonstrate an exportable API or a successful build in another project; use a second minimal consumer project or a meaningful component test when portability matters. Do not claim registry publication without a manifest and an actual publication workflow.
8. Re-read changed source, headers, and CMake entries together. If permitted and configured with an intended chip target, run `idf.py build` and inspect the configured component list or build output to confirm inclusion. Use `idf.py reconfigure` when CMake discovery needs refreshing. Test actual API behavior where meaningful (e.g., with ESP-IDF component/Unity tests or a consumer program), and report build/test and hardware checks separately. Do not select a target or run a standalone CMake build just to validate instructions.

## Output

List the created or updated component and consumer files, public API and dependency choices, discovery path, reuse constraints, and checks actually performed. Distinguish a source-level review from a successful ESP-IDF build or on-device test; flag missing environment, target, or hardware prerequisites without implying they were validated.
