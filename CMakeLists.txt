CMAKE_MINIMUM_REQUIRED (VERSION 2.6)
PROJECT(ntptime C)

SET(PROGRAM_VERSION 0.01)
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic -std=c99")
ADD_DEFINITIONS(-DPROGRAM_VERSION="${PROGRAM_VERSION}")
MESSAGE(STATUS "Program version: ${PROGRAM_VERSION}")
include(FindSubversion)
IF(Subversion_FOUND)
        MESSAGE(STATUS "Looking for Subversion - found")
        #Subversion_WC_INFO(${CMAKE_SOURCE_DIR} "svn")
        EXECUTE_PROCESS(
                COMMAND "svnversion" "--no-newline" ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE SVN_REVISION
                ERROR_VARIABLE SVN_REVISION
        )
        MESSAGE(STATUS "SVN revision number: ${SVN_REVISION}" )
ELSE(Subversion_FOUND)
        MESSAGE(STATUS "Looking for Subversion - not found")
        ADD_DEFINITIONS(-DSVN_REVISION=""unknown"")
ENDIF(Subversion_FOUND)

ADD_DEFINITIONS(-DSVN_REVISION="${SVN_REVISION}")
ADD_DEFINITIONS(-D_GNU_SOURCE)

ADD_LIBRARY(ntptime SHARED ntptime.c crc8.c)

ADD_EXECUTABLE(${BIN_NAME_PREFIX}ntptimed ntptimed.c)
ADD_EXECUTABLE(${BIN_NAME_PREFIX}ntptime_client ntptime_client.c)
ADD_EXECUTABLE(${BIN_NAME_PREFIX}test_send test/test_send.c)
ADD_EXECUTABLE(test_sync_ts test/test_sync_ts.c)
ADD_EXECUTABLE(simple test/simple.c)

TARGET_LINK_LIBRARIES(ntptime glink shmsbuf)
TARGET_LINK_LIBRARIES(${BIN_NAME_PREFIX}ntptimed glink vu_tools ntptime m iniparser pthread shmsbuf mysqlclient prx_mysql)
TARGET_LINK_LIBRARIES(${BIN_NAME_PREFIX}ntptime_client glink vu_tools ntptime shmsbuf)
TARGET_LINK_LIBRARIES(${BIN_NAME_PREFIX}test_send glink vu_tools ntptime)
TARGET_LINK_LIBRARIES(test_sync_ts ntptime glink vu_tools)
TARGET_LINK_LIBRARIES(simple ntptime)

# set_target_properties(${BIN_NAME_PREFIX}ntptimed PROPERTIES COMPILE_DEFINITIONS NTPTIMED_DEBUG)

#Find swig
FIND_PACKAGE(SWIG REQUIRED)
INCLUDE(${SWIG_USE_FILE})

#find PythonLibs
FIND_PACKAGE(PythonLibs)
INCLUDE_DIRECTORIES(${PYTHON_INCLUDE_PATH})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

SWIG_ADD_MODULE(Ntptime python ntptime.i)
SWIG_LINK_LIBRARIES(Ntptime ${PYTHON_LIBRARIES} ntptime)

#add_custom_target(copy_ntptime_python_scripts ALL
#                  COMMENT "Copying python scripts"
#                  SOURCES NTPbindingTest.py)
#add_custom_command(TARGET copy_ntptime_python_scripts
#                   COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/NTPbindingTest.py
#                                                    ${CMAKE_CURRENT_BINARY_DIR}/NTPCbindingTest.py)

## configure install

FIND_PACKAGE(PythonInterp REQUIRED)
EXECUTE_PROCESS(COMMAND ${PYTHON_EXECUTABLE} -c "from sys import stdout; from distutils import sysconfig; stdout.write(sysconfig.get_python_lib())" OUTPUT_VARIABLE PYTHON_LIB_PATH)
MESSAGE(STATUS "Python lib path: ${PYTHON_LIB_PATH}")

# libdir
IF(CMAKE_SIZEOF_VOID_P EQUAL 4)
    SET(LIB_SUFFIX "")
ELSE(CMAKE_SIZEOF_VOID_P EQUAL 4)
    SET(LIB_SUFFIX 64)
ENDIF(CMAKE_SIZEOF_VOID_P EQUAL 4)
SET(LIB_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX})
MESSAGE(STATUS "Library install dir: ${LIB_INSTALL_DIR}")

SET(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/include)
MESSAGE(STATUS "Headers install dir: ${INCLUDE_INSTALL_DIR}")

# install

INSTALL(TARGETS ntptime LIBRARY DESTINATION ${LIB_INSTALL_DIR})
INSTALL(FILES ntptime.h DESTINATION ${INCLUDE_INSTALL_DIR}/ntptime)

INSTALL(TARGETS ${BIN_NAME_PREFIX}ntptimed ${BIN_NAME_PREFIX}ntptime_client
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

INSTALL(TARGETS _Ntptime LIBRARY DESTINATION ${PYTHON_LIB_PATH})
INSTALL(FILES   ${CMAKE_CURRENT_BINARY_DIR}/Ntptime.py
                ${CMAKE_CURRENT_SOURCE_DIR}/ntptime.py
        DESTINATION ${PYTHON_LIB_PATH})
# uninstall

CONFIGURE_FILE(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

ADD_CUSTOM_TARGET(ntptime_uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")


