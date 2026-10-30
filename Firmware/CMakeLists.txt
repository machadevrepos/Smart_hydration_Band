cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smart_hydration_band)

target_include_directories(app PRIVATE include)

target_sources(app PRIVATE
  src/main.c
  src/max30101.c
  src/heart_rate.c
)
