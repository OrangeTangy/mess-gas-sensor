cmake_minimum_required(VERSION 3.22)
# The SGP30 driver is shared with the gas_sensor project.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../gas_sensor/components/sgp30)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(mess_bme280)
