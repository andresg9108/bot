# Mantener este orden: versión mínima de CMake, componentes adicionales,
# infraestructura de ESP-IDF y declaración del proyecto.
# Keep this order: minimum CMake version, extra components,
# ESP-IDF infrastructure, and project declaration.
cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS "../ESP32-utilities/BLE/agBLE")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(agbot)
