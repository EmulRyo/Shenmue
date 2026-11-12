# Tests over the files of the game: they check the formats as the wiki describes them. They need the extracted files
# of Shenmue I in SHENMUE_CONTENT, and are skipped if the folder isn't there
add_executable(ShenmueTests
    main.c
    test.h
    corpus.c
    corpus.h
    testMapinfo.c
    testScn.c
    testSh4.c
    testSh4Program.c
    testSh4Listing.c
    testGameFunctions.c
    testBytecode.c
    testScnPair.c
    testDecompile.c

    ../src/core/bytes.h
    ../src/core/file.c
    ../src/core/file.h
    ../src/formats/animation/auth.c
    ../src/formats/animation/auth.h
    ../src/formats/map/mapinfo.c
    ../src/formats/map/mapinfo.h
    ../src/formats/script/scn.c
    ../src/formats/script/scn.h
    ../src/formats/script/sh4.c
    ../src/formats/script/sh4.h
    ../src/formats/script/sh4Code.c
    ../src/formats/script/sh4Code.h
    ../src/formats/script/sh4Program.c
    ../src/formats/script/sh4Program.h
    ../src/formats/script/sh4Listing.c
    ../src/formats/script/sh4Listing.h
    ../src/formats/script/scnBytecode.c
    ../src/formats/script/scnBytecode.h
    ../src/formats/script/scnPair.c
    ../src/formats/script/scnPair.h
    ../src/formats/script/scnDecompile.c
    ../src/formats/script/scnDecompile.h)

target_include_directories(ShenmueTests PRIVATE ../src)
# file.c decompresses the gzipped files with raylib
target_link_libraries(ShenmueTests PRIVATE raylib)
set_target_properties(ShenmueTests PROPERTIES FOLDER tests)
# The databases of resources/, read by the tests from the source tree
target_compile_definitions(ShenmueTests PRIVATE SHENMUE_RESOURCES="${CMAKE_SOURCE_DIR}/resources")

foreach(test mapinfo_chunks scn3_chunks scn_files scn_pairs sh4_encodings sh4_code sh4_program sh4_listing game_functions bc_encodings bc_files scn_pair decompile)
    add_test(NAME ${test} COMMAND ShenmueTests ${SHENMUE_CONTENT} ${test})
    set_tests_properties(${test} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
