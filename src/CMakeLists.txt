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
    src/core/thread.c
    src/core/thread.h

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

    src/formats/font/fon.c
    src/formats/font/fon.h

    src/formats/map/mapinfo.c
    src/formats/map/mapinfo.h

    src/formats/model/mt5.c
    src/formats/model/mt5.h

    src/formats/script/chrt.c
    src/formats/script/chrt.h
    src/formats/script/scnf.c
    src/formats/script/scnf.h

    src/formats/sound/snd.c
    src/formats/sound/snd.h
    src/formats/sound/xwm.c
    src/formats/sound/xwm.h

    src/formats/texture/pvr.c
    src/formats/texture/pvr.h
    src/formats/texture/spr.c
    src/formats/texture/spr.h

    src/render/freeCamera.c
    src/render/freeCamera.h
    src/render/mt5Mesh.c
    src/render/mt5Mesh.h
    src/render/mt5Pose.c
    src/render/mt5Pose.h
    src/render/mt5Render.c
    src/render/mt5Render.h
    src/render/scene3D.c
    src/render/scene3D.h

    src/ui/input.c
    src/ui/input.h
    src/ui/properties.c
    src/ui/properties.h
    src/ui/toolbar.c
    src/ui/toolbar.h
    src/ui/tooltip.c
    src/ui/tooltip.h
    src/ui/tree.c
    src/ui/tree.h
    src/ui/waveform.c
    src/ui/waveform.h

    src/views/authSounds.c
    src/views/authSounds.h
    src/views/authView.c
    src/views/authView.h
    src/views/chrtView.c
    src/views/chrtView.h
    src/views/fonView.c
    src/views/fonView.h
    src/views/imageView.c
    src/views/imageView.h
    src/views/mapinfoView.c
    src/views/mapinfoView.h
    src/views/mt5View.c
    src/views/motnView.c
    src/views/motnView.h
    src/views/mt5View.h
    src/views/sndView.c
    src/views/sndView.h
    src/views/views.c
    src/views/views.h
    src/views/xwmView.c
    src/views/xwmView.h
    PARENT_SCOPE
)
