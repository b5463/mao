# LAMP 01 test device: minimal ODD BUS light for MAO M2 development.
# Not product firmware. Reuses MAO's odd_bus component unchanged.
cmake_minimum_required(VERSION 3.22)

set(PROJECT_VER "0.1.0-m2")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components/odd_bus")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lamp_01_test)
