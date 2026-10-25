cmake_minimum_required(VERSION 3.22)
project(Shenmue VERSION 1.0)

# Dependencies
set(RAYLIB_VERSION 5.5)
find_package(raylib ${RAYLIB_VERSION} QUIET) # QUIET or REQUIRED
if (NOT raylib_FOUND) # If there's none, fetch and build raylib
  include(FetchContent)
  FetchContent_Declare(
    raylib
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
  )
  FetchContent_GetProperties(raylib)
  if (NOT raylib_POPULATED) # Have we downloaded raylib yet?
    set(FETCHCONTENT_QUIET NO)
    FetchContent_MakeAvailable(raylib)
  endif()
endif()

set_property(GLOBAL PROPERTY C_STANDARD 11)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(src)
add_subdirectory(vendor/nativefiledialog-extended)
set(CJSON
    vendor/cJSON/cJSON.h
    vendor/cJSON/cJSON.c)

source_group(src   FILES ${SRC})
source_group(cJSON   FILES ${CJSON})
add_executable(Shenmue)

set_target_properties(
  Shenmue PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Shenmue)
  
  target_sources(Shenmue PRIVATE ${SRC} ${CJSON})

  target_include_directories(${PROJECT_NAME} PRIVATE vendor/cJSON)
  
  target_link_libraries(${PROJECT_NAME} PRIVATE raylib)
  target_link_libraries(${PROJECT_NAME} PRIVATE nfd)

# Media Foundation, for the WMA decoder of the xWMA files
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE mfplat mfuuid ole32 wmcodecdspuuid)
endif()

# Checks if OSX and links appropriate frameworks (only required on MacOS)
if (APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework IOKit")
    target_link_libraries(${PROJECT_NAME} "-framework Cocoa")
    target_link_libraries(${PROJECT_NAME} "-framework OpenGL")
endif()

add_custom_command(TARGET ${PROJECT_NAME}  POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/resources
                "$<TARGET_FILE_DIR:Shenmue>/resources")