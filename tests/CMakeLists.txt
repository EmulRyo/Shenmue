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
    testScnData.c
    testVm.c
    testDisplay.c
    testCutscene.c)

# The formats, the programs and the emulation, with raylib (the gzipped files)
target_link_libraries(ShenmueTests PRIVATE ShenmueCore)
set_target_properties(ShenmueTests PROPERTIES C_STANDARD 11 FOLDER tests)
# The databases of resources/, read by the tests from the source tree
target_compile_definitions(ShenmueTests PRIVATE SHENMUE_RESOURCES="${CMAKE_SOURCE_DIR}/resources")

foreach(test mapinfo_chunks scn3_chunks scn_files scn_pairs sh4_encodings sh4_code sh4_program sh4_listing game_functions bc_encodings bc_files scn_pair decompile scn_data vm display cutscene)
    add_test(NAME ${test} COMMAND ShenmueTests ${SHENMUE_CONTENT} ${test})
    set_tests_properties(${test} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
# The tests that emulate the programs, the longest ones, first when they run in parallel (until CTest knows their times)
set_tests_properties(display PROPERTIES COST 100)
set_tests_properties(cutscene scn_data PROPERTIES COST 60)
set_tests_properties(vm PROPERTIES COST 30)
