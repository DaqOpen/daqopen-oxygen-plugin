#
# Oxygen plugin for DaqOpen
# Receive data from DaqZMQ Publisher into OXYGEN as synchronous scalar channels.
#
cmake_minimum_required(VERSION 3.16)

# Name of the plugin project and compiled plugin file
set(LIBNAME daqopen-oxygen-plugin)
# This is just any stable GUID to help Visual Studio identify the project for rebuilds
set("${LIBNAME}_GUID_CMAKE" "FDCBCD66-8BC9-41B0-9CB9-079AA23DF9FC" CACHE INTERNAL "remove this and Visual Studio will mess up incremental builds")

# handle setup of a cmake toplevel project
# finding libraries etc
if (${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_SOURCE_DIR})

  #
  # Force C++17
  set(CMAKE_CXX_STANDARD 17)

  # project name
  project(${LIBNAME})

  get_filename_component(ODK_ROOT "../OXYGEN-SDK" ABSOLUTE)
  message("ODKROOT = ${ODK_ROOT}")
  # expand cmake search path to check for project settings
  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ODK_ROOT}/cmake )

  include(CMakeSettings)
  include(OxygenPluginFunctions)

  SetLinkStaticRuntime()
  SetCommonOutputDirectory()

  AddUniqueTargetFromSubdirectory(pugixml "${SW_APP_ROOT}/3rdparty/pugixml-1.9/scripts" "3rdparty/pugixml-1.9")
  AddUniqueTargetFromSubdirectory(odk "${ODK_ROOT}/odk" "odk")
else()
  include(OxygenPluginFunctions)
endif()

find_package(SDKQt REQUIRED)
find_package(cppzmq)

include_directories(
  ../shared
)

set(SOURCE_FILES
  daqopen-oxygen-plugin.cpp
)
source_group("Source Files" FILES ${SOURCE_FILES})

add_library(${LIBNAME} SHARED
  ${SOURCE_FILES}
)

target_link_libraries(${LIBNAME}
  odk_framework
  cppzmq
)

qt5_add_binary_resources(${LIBNAME}_qml qml.qrc DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/qml.rcc")

set(RESOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/resources)
include_directories(${RESOURCE_DIR})
AddResourceFile(${LIBNAME} "${CMAKE_CURRENT_BINARY_DIR}/qml.rcc" ${RESOURCE_DIR})

SetPluginOutputOptions(${LIBNAME})

