cmake_minimum_required(VERSION 3.22)

set(PROJECT_VER "0.2.0-m2")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# One component lock file per target (esp32c3 = LCDkit, esp32s3 = A0), so
# switching boards never rewrites the other board's pinned versions.
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${IDF_TARGET}")
project(mao)
