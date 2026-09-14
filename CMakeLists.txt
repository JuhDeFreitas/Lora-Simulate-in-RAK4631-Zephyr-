# SPDX-License-Identifier: Apache-2.0
#
# RAK4631 (nRF52840) Zephyr application
# (c) 2026, Inatel Competence Center

cmake_minimum_required(VERSION 3.20.0)

# Default board for this application (override with -b / west build -b ...).
set(BOARD rak4631/nrf52840 CACHE STRING "Target board")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rak4631_application LANGUAGES C ASM)

# --- ICC common core (git submodule: source/icc_common_core) ----------------
set(ICC_COMMON_CORE ${CMAKE_CURRENT_SOURCE_DIR}/source/icc_common_core)

if(NOT EXISTS ${ICC_COMMON_CORE}/src/icc_log.c)
  message(FATAL_ERROR
    "source/icc_common_core is missing. Run: git submodule update --init --recursive")
endif()

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${ICC_COMMON_CORE}/inc
)

# --- Application sources ----------------------------------------------------
target_sources(app PRIVATE
  source/main.c
  source/lora/lora.c
  ${ICC_COMMON_CORE}/src/icc_log.c
)
