# Copyright (C) 2007-2009 LuaDist.
# Created by Peter Kapec
# Redistribution and use of this file is allowed according to the terms of the MIT license.
# For details see the COPYRIGHT file distributed with LuaDist.
# Please note that the package source code is licensed under its own license.

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
set(CMAKE_INSTALL_PREFIX ${LIBLUA_INSTALLDIR})
# ------------------------------------------------------------------------------

CMAKE_MINIMUM_REQUIRED(VERSION 3.28)
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
ADD_LIBRARY(core MODULE ${LANES_SRC})

IF(UNIX AND NOT CYGWIN)
  SET(LIBS pthread)
ENDIF(UNIX AND NOT CYGWIN)


if(WIN32)
TARGET_LINK_LIBRARIES(core ${LUA_LIBRARY} ${LIBS})
else(WIN32)
TARGET_LINK_LIBRARIES(core ${LIBS})
endif(WIN32)

SET_TARGET_PROPERTIES(core PROPERTIES PREFIX "")

# ------------------------------------------------------------------------------
target_compile_options(core PRIVATE
  /D_CRT_SECURE_NO_WARNINGS /D_WINDLL /std:c++20 /DNODEBUG)
# ------------------------------------------------------------------------------

# Install all files and documentation
# set(INSTALL_LMOD share/lua/lmod CACHE PATH "Directory to install Lua modules.")
# set(INSTALL_CMOD share/lua/cmod CACHE PATH "Directory to install Lua binary modules.")
# set(INSTALL_DATA share/${PROJECT_NAME} CACHE PATH "Directory the package can store documentation, tests or other data in.")
# set(INSTALL_DOC ${INSTALL_DATA}/doc CACHE PATH "Recommended directory to install documentation into.")
# set(INSTALL_TEST ${INSTALL_DATA}/test CACHE PATH "Recommended directory to install tests into.")

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
# Redefine original install dirs for OneLuaPro
set(INSTALL_CMOD
  ${INSTALL_LIBDIR}/lua/${liblua_VERSION_MAJOR}.${liblua_VERSION_MINOR}/lanes
  CACHE PATH "Directory to install Lua binary modules.")
set(INSTALL_LMOD
  ${LIBLUA_INSTALLDIR}/share/lua/${liblua_VERSION_MAJOR}.${liblua_VERSION_MINOR}
  CACHE PATH "Directory to install Lua modules.")
set(INSTALL_DATA
  ${LIBLUA_INSTALLDIR}/share/doc/lanes
  CACHE PATH "Directory the package can store documentation, tests or other data in.")
set(INSTALL_DOC
  ${LIBLUA_INSTALLDIR}/share/doc/lanes/doc
  CACHE PATH "Recommended directory to install documentation into.")
set(INSTALL_TEST
  ${LIBLUA_INSTALLDIR}/share/doc/lanes/test
  CACHE PATH "Recommended directory to install tests into.")
# ------------------------------------------------------------------------------

INSTALL (TARGETS core DESTINATION ${INSTALL_CMOD})
INSTALL (FILES src/lanes.lua DESTINATION ${INSTALL_LMOD})

INSTALL (FILES ABOUT BUGS COPYRIGHT CHANGES README TODO DESTINATION ${INSTALL_DATA})
INSTALL (DIRECTORY docs/ DESTINATION ${INSTALL_DOC})
INSTALL (DIRECTORY tests/ DESTINATION ${INSTALL_TEST})
