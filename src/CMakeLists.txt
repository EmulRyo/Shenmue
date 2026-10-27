set(SRC
    src/CMakeLists.txt
    src/main.c

    src/audio/sndSynth.c
    src/audio/sndSynth.h

    src/core/bytes.h
    src/core/dir.c
    src/core/dir.h
    src/core/dynamicArray.c
    src/core/dynamicArray.h
    src/core/file.c
    src/core/file.h
    src/core/hashTable.c
    src/core/hashTable.h
    src/core/stringBuilder.c
    src/core/stringBuilder.h

    src/formats/archive/afs.c
    src/formats/archive/afs.h
    src/formats/archive/idx.c
    src/formats/archive/idx.h
    src/formats/archive/ipac.c
    src/formats/archive/ipac.h
    src/formats/archive/pkf.c
    src/formats/archive/pkf.h
    src/formats/archive/pks.c
    src/formats/archive/pks.h
    src/formats/archive/tac.c
    src/formats/archive/tac.h
    src/formats/archive/tad.c
    src/formats/archive/tad.h

    src/formats/model/mt5.c
    src/formats/model/mt5.h

    src/formats/sound/snd.c
    src/formats/sound/snd.h
    src/formats/sound/xwm.c
    src/formats/sound/xwm.h

    src/formats/text/chrt.c
    src/formats/text/chrt.h
    src/formats/text/scnf.c
    src/formats/text/scnf.h

    src/formats/texture/pvr.c
    src/formats/texture/pvr.h
    src/formats/texture/spr.c
    src/formats/texture/spr.h

    src/views/mt5View.c
    src/views/mt5View.h
    src/views/sndView.c
    src/views/sndView.h
    src/views/waveform.c
    src/views/waveform.h
    src/views/xwmView.c
    src/views/xwmView.h
    PARENT_SCOPE
)
