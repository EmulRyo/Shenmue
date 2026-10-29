set(SRC
    src/CMakeLists.txt
    src/main.c

    src/app/fileOpen.c
    src/app/fileOpen.h
    src/app/filenameDB.c
    src/app/filenameDB.h
    src/app/style.c
    src/app/style.h
    src/app/tadBatch.c
    src/app/tadBatch.h

    src/audio/sndDsp.c
    src/audio/sndDsp.h
    src/audio/sndSequencer.c
    src/audio/sndSynth.c
    src/audio/sndSynth.h
    src/audio/sndSynthInternal.h

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

    src/formats/animation/auth.c
    src/formats/animation/auth.h
    src/formats/animation/motn.c
    src/formats/animation/motn.h

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

    src/views/authView.c
    src/views/authView.h
    src/views/chrtView.c
    src/views/chrtView.h
    src/views/imageView.c
    src/views/imageView.h
    src/views/mt5Mesh.c
    src/views/mt5Mesh.h
    src/views/mt5Render.c
    src/views/mt5Render.h
    src/views/mt5View.c
    src/views/mt5View.h
    src/views/orbitCamera.c
    src/views/orbitCamera.h
    src/views/sndView.c
    src/views/sndView.h
    src/views/views.c
    src/views/views.h
    src/views/waveform.c
    src/views/waveform.h
    src/views/xwmView.c
    src/views/xwmView.h
    PARENT_SCOPE
)
