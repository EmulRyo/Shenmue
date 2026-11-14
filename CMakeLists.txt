cmake_minimum_required(VERSION 3.22)
project(Shenmue VERSION 1.0)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(src)
add_subdirectory(vendor/raylib)
add_subdirectory(vendor/nativefiledialog-extended)
set(RAYGUI
    vendor/raygui/raygui.h)
set(CJSON
    vendor/cJSON/cJSON.h
    vendor/cJSON/cJSON.c)

# The core and the formats, shared by the application and the tests
add_library(ShenmueCore STATIC ${SRC_CORE})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX src FILES ${SRC_CORE})
set_target_properties(ShenmueCore PROPERTIES C_STANDARD 11 FOLDER libraries)
target_include_directories(ShenmueCore PUBLIC src)
# raylib: the gzip decompression and the file functions
target_link_libraries(ShenmueCore PUBLIC raylib)

# Media Foundation, for the WMA decoder of the xWMA files
if (WIN32)
    target_link_libraries(ShenmueCore PUBLIC mfplat mfuuid ole32 wmcodecdspuuid)
endif()

# The threads of the TAD extraction
if (NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(ShenmueCore PUBLIC Threads::Threads)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX src FILES ${SRC})
source_group(cJSON   FILES ${CJSON})
source_group(raygui   FILES ${RAYGUI})
add_executable(Shenmue)

set_target_properties(Shenmue PROPERTIES
    C_STANDARD 11
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Shenmue)

target_sources(Shenmue PRIVATE ${SRC} ${RAYGUI} ${CJSON})
target_include_directories(${PROJECT_NAME} PRIVATE src vendor/raygui vendor/cJSON)
target_link_libraries(${PROJECT_NAME} PRIVATE ShenmueCore raylib nfd)

# Icon of the executable and of the window
if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE icon/Shenmue.rc)
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
embed_resource(Shenmue1GameFunctions.tsv g_shenmue1GameFunctions)

# Tests over the files of the game: cmake --build build --config Release --target ShenmueTests, then
# ctest --test-dir build -C Release -j 8 (they run in parallel; the emulation is several times slower in Debug)
cmake_host_system_information(RESULT SHENMUE_CORES QUERY NUMBER_OF_LOGICAL_CORES)
set(CMAKE_CTEST_ARGUMENTS --parallel ${SHENMUE_CORES} --output-on-failure)    # Of the RUN_TESTS target
enable_testing()
set(SHENMUE_CONTENT "${CMAKE_SOURCE_DIR}/content/Shenmue1" CACHE PATH "Folder with the extracted files of Shenmue I, for the tests")
add_subdirectory(tests)