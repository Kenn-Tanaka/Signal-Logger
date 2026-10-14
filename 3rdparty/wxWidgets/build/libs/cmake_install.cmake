# Install script for directory: X:/3rdparty/wxWidgets/build/cmake/lib

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/wxWidgets")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "C:/Strawberry/c/bin/objdump.exe")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gcc_x64_lib" TYPE STATIC_LIBRARY FILES "X:/3rdparty/wxWidgets/build/lib/gcc_x64_lib/libwxregexu.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gcc_x64_lib" TYPE STATIC_LIBRARY FILES "X:/3rdparty/wxWidgets/build/lib/gcc_x64_lib/libwxzlib.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gcc_x64_lib" TYPE STATIC_LIBRARY FILES "X:/3rdparty/wxWidgets/build/lib/gcc_x64_lib/libwxexpat.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gcc_x64_lib" TYPE STATIC_LIBRARY FILES "X:/3rdparty/wxWidgets/build/lib/gcc_x64_lib/libwxjpeg.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gcc_x64_lib" TYPE STATIC_LIBRARY FILES "X:/3rdparty/wxWidgets/build/lib/gcc_x64_lib/libwxpng.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gcc_x64_lib" TYPE STATIC_LIBRARY FILES "X:/3rdparty/wxWidgets/build/lib/gcc_x64_lib/libwxtiff.a")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("X:/3rdparty/wxWidgets/build/libs/base/cmake_install.cmake")
  include("X:/3rdparty/wxWidgets/build/libs/net/cmake_install.cmake")
  include("X:/3rdparty/wxWidgets/build/libs/core/cmake_install.cmake")
  include("X:/3rdparty/wxWidgets/build/libs/adv/cmake_install.cmake")
  include("X:/3rdparty/wxWidgets/build/libs/xrc/cmake_install.cmake")
  include("X:/3rdparty/wxWidgets/build/libs/xml/cmake_install.cmake")

endif()

