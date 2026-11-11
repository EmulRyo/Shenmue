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
    ../src/formats/script/sh4Program.h)

target_include_directories(ShenmueTests PRIVATE ../src)
# file.c decompresses the gzipped files with raylib
target_link_libraries(ShenmueTests PRIVATE raylib)
set_target_properties(ShenmueTests PROPERTIES FOLDER tests)

foreach(test mapinfo_chunks scn3_chunks scn_files scn_pairs sh4_encodings sh4_code sh4_program)
    add_test(NAME ${test} COMMAND ShenmueTests ${SHENMUE_CONTENT} ${test})
    set_tests_properties(${test} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
