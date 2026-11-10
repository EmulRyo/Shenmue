cmake_minimum_required(VERSION 3.22)
project(Shenmue VERSION 1.0)

set_property(GLOBAL PROPERTY C_STANDARD 11)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(src)
add_subdirectory(vendor/raylib)
add_subdirectory(vendor/nativefiledialog-extended)
set(RAYGUI
    vendor/raygui/raygui.h)
set(CJSON
    vendor/cJSON/cJSON.h
    vendor/cJSON/cJSON.c)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX src FILES ${SRC})
source_group(cJSON   FILES ${CJSON})
source_group(raygui   FILES ${RAYGUI})
add_executable(Shenmue)

set_target_properties(
  Shenmue PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Shenmue)
  
  target_sources(Shenmue PRIVATE ${SRC} ${RAYGUI} ${CJSON})

  target_include_directories(${PROJECT_NAME} PRIVATE src vendor/raygui vendor/cJSON)
  
  target_link_libraries(${PROJECT_NAME} PRIVATE raylib)
  target_link_libraries(${PROJECT_NAME} PRIVATE nfd)

# Media Foundation, for the WMA decoder of the xWMA files
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE mfplat mfuuid ole32 wmcodecdspuuid)
endif()

# Icon of the executable and of the window
if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE icon/Shenmue.rc)
endif()

# The threads of the TAD extraction
if (NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
endif()

# Checks if OSX and links appropriate frameworks (only required on MacOS)
if (APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework IOKit")
    target_link_libraries(${PROJECT_NAME} "-framework Cocoa")
    target_link_libraries(${PROJECT_NAME} "-framework OpenGL")
endif()

# The databases of resources/ go inside the executable: tools/embed.c turns each one into a C array at build time
add_executable(embed tools/embed.c)
set_target_properties(embed PROPERTIES FOLDER tools)

function(embed_resource file symbol)
    set(output ${CMAKE_CURRENT_BINARY_DIR}/generated/${symbol}.c)
    add_custom_command(
        OUTPUT ${output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
        COMMAND embed ${CMAKE_CURRENT_SOURCE_DIR}/resources/${file} ${output} ${symbol}
        DEPENDS embed ${CMAKE_CURRENT_SOURCE_DIR}/resources/${file}
        COMMENT "Embedding resources/${file}"
        VERBATIM)
    target_sources(${PROJECT_NAME} PRIVATE ${output})
    source_group(generated FILES ${output})
endfunction()

embed_resource(FilenameDatabase.txt g_filenameDatabase)
embed_resource(Shenmue1Characters.tsv g_shenmue1Characters)
embed_resource(Shenmue1Maps.tsv g_shenmue1Maps)