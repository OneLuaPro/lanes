# Copyright (C) 2007-2009 LuaDist.
# Created by Peter Kapec
# Redistribution and use of this file is allowed according to the terms of the MIT license.
# For details see the COPYRIGHT file distributed with LuaDist.
# Please note that the package source code is licensed under its own license.
CMAKE_MINIMUM_REQUIRED(VERSION 3.18)
PROJECT(lanes CXX)
set (CMAKE_CXX_STANDARD 20)

# ------------------------------------------------------------------------------
# find OneLuaPro's liblua installation and version info
if(NOT LUA_HINTS)
  if(WIN32)
    set(LUA_HINTS "c:/Apps")
  endif()
endif()
find_package(liblua REQUIRED CONFIG HINTS ${LUA_HINTS})
if(liblua_FOUND)
  message(STATUS "liblua version        : ${liblua_VERSION}")
  message(STATUS "liblua install prefix : ${LIBLUA_INSTALLDIR}")
  message(STATUS "liblua include dir    : ${LIBLUA_INCLUDEDIR}")
  message(STATUS "liblua lib dir        : ${LIBLUA_LIBDIR}")
  # set lane's own CMake variables accordingly
  set(LUA_INCLUDE_DIR ${LIBLUA_INCLUDEDIR})
  set(LUA_LIBRARY ${LIBLUA_LIBDIR}/liblua.lib)
else()
  message(FATAL_ERROR "Unable to find liblua version ${liblua_VERSION}.")
endif()
# ------------------------------------------------------------------------------
# Installation prefix directory - automatically set from find_package()
# Needs to be defined before project definition statement - for whatever reason
if(NOT CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX ${LIBLUA_INSTALLDIR})
endif()
# ------------------------------------------------------------------------------

CMAKE_MINIMUM_REQUIRED(VERSION 3.23)
PROJECT(lanes CXX)

# FIND_PACKAGE(Lua51 REQUIRED)
INCLUDE_DIRECTORIES(${LUA_INCLUDE_DIR})

if(USE_PTHREAD)
ADD_DEFINITIONS(-DHAVE_WIN32_PTHREAD)
endif(USE_PTHREAD)
#2DO - patch threading.c to suppot cygwin.
# The following values are just a guess.
# WARNING: test segfault under Cygwin
IF(CYGWIN)
  ADD_DEFINITIONS(-D_PRIO_MODE=SCHED_FIFO)
  ADD_DEFINITIONS(-D_PRIO_HI=15) # maximum that doesn't crash
  ADD_DEFINITIONS(-D_PRIO_0=0)
  ADD_DEFINITIONS(-D_PRIO_LO=-15) # ???
  ADD_DEFINITIONS(-Dpthread_yield=sched_yield)
ENDIF(CYGWIN)


# Build
INCLUDE_DIRECTORIES(src)
aux_source_directory(./src LANES_SRC)
ADD_LIBRARY(lanes_core MODULE ${LANES_SRC})

IF(UNIX AND NOT CYGWIN)
  SET(LIBS pthread)
ENDIF(UNIX AND NOT CYGWIN)


if(WIN32)
TARGET_LINK_LIBRARIES(lanes_core ${LUA_LIBRARY} ${LIBS})
else(WIN32)
TARGET_LINK_LIBRARIES(lanes_core ${LIBS})
endif(WIN32)

SET_TARGET_PROPERTIES(lanes_core PROPERTIES PREFIX "")

# ------------------------------------------------------------------------------
target_compile_definitions(lanes_core PRIVATE
  _CRT_SECURE_NO_WARNINGS _WINDLL NODEBUG
)
set_target_properties(lanes_core PROPERTIES
  CXX_STANDARD ${CMAKE_CXX_STANDARD}
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
if(CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM")
  # Silence "warning: function declared 'noreturn' should not return
  # [-Winvalid-noreturn]" warnings
  target_compile_options(lanes_core PRIVATE -Wno-invalid-noreturn)
endif()
# ------------------------------------------------------------------------------

# ------------------------------------------------------------------------------
# Setup GNU-alike installatin directories
include (GNUInstallDirs)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR})
set(INSTALL_LIBDIR
  ${CMAKE_INSTALL_LIBDIR} CACHE PATH "Installation directory for libraries")
set(INSTALL_BINDIR
  ${CMAKE_INSTALL_BINDIR} CACHE PATH "Installation directory for executables")
set(INSTALL_INCLUDEDIR
  ${CMAKE_INSTALL_INCLUDEDIR} CACHE PATH "Installation directory for header files")
set(INSTALL_DOCDIR
  ${CMAKE_INSTALL_DOCDIR} CACHE PATH "Installation directory for documentation")
set(INSTALL_MANDIR
  ${CMAKE_INSTALL_MANDIR} CACHE PATH "Installation directory for manpages")
set(INSTALL_DATAROOTDIR
  ${CMAKE_INSTALL_DATAROOTDIR} CACHE PATH "Installation directory for data")
# Redefine original install dirs for OneLuaPro
set(INSTALL_CMOD
  ${INSTALL_LIBDIR}/lua/${liblua_VERSION_MAJOR}.${liblua_VERSION_MINOR}
  CACHE PATH "Directory to install Lua binary modules.")
set(INSTALL_LMOD
  ${INSTALL_DATAROOTDIR}/lua/${liblua_VERSION_MAJOR}.${liblua_VERSION_MINOR}
  CACHE PATH "Directory to install Lua modules.")
set(INSTALL_DATA
  ${INSTALL_DATAROOTDIR}/doc/lanes
  CACHE PATH "Directory the package can store documentation, tests or other data in.")

# ------------------------------------------------------------------------------
# Install files
INSTALL (TARGETS lanes_core DESTINATION ${INSTALL_CMOD})
INSTALL (FILES src/lanes.lua DESTINATION ${INSTALL_LMOD})
INSTALL (FILES ABOUT BUGS COPYRIGHT CHANGES README TODO DESTINATION ${INSTALL_DATA}/misc)
INSTALL (DIRECTORY docs/ DESTINATION ${INSTALL_DATA})
INSTALL (DIRECTORY tests DESTINATION ${INSTALL_DATA})
