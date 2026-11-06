cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smart_hydration_band)

target_include_directories(app PRIVATE
  include
  # Bosch's BMI270 firmware blob, used by src/step_counter.c.
  ${ZEPHYR_BASE}/drivers/sensor/bosch/bmi270
)

target_sources(app PRIVATE
  src/main.c
  src/max30101.c
  src/heart_rate.c
  src/step_counter.c
  src/status_led.c
  src/button.c
  src/haptic.c
  src/band_mode.c
  src/ble_service.c
  src/battery.c
)
