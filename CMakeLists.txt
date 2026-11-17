cmake_minimum_required(VERSION 3.16.0)

# sdkconfig.defaults.<idf_target> is appended automatically.
# A board env can add one more file with:
#   board_build.cmake_extra_args = -DSTARFORGE_EXTRA_SDKCONFIG=sdkconfig.s3-touch
set(_sdkconfig_defaults "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
if(STARFORGE_EXTRA_SDKCONFIG)
    set(_sdkconfig_defaults "${_sdkconfig_defaults};${CMAKE_SOURCE_DIR}/${STARFORGE_EXTRA_SDKCONFIG}")
endif()
set(SDKCONFIG_DEFAULTS "${_sdkconfig_defaults}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(StarForgeOS)
