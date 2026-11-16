# The core, the formats, the SH-4 code and the programs of the scenes: a library for the application and the tests
set(SRC_CORE
    src/core/bytes.h
    src/core/dir.c
    src/core/dir.h
    src/core/dynamicArray.c
    src/core/dynamicArray.h
    src/core/file.c
    src/core/file.h
    src/core/frameTime.c
    src/core/frameTime.h
    src/core/hashTable.c
    src/core/hashTable.h
    src/core/log.c
    src/core/log.h
    src/core/path.c
    src/core/path.h
    src/core/stringBuilder.c
    src/core/stringBuilder.h
    src/core/text.c
    src/core/text.h
    src/core/thread.c
    src/core/thread.h

    src/formats/animation/auth.c
    src/formats/animation/auth.h
    src/formats/animation/motn.c
    src/formats/animation/motn.h

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

    src/formats/font/fon.c
    src/formats/font/fon.h

    src/formats/map/mapinfo.c
    src/formats/map/mapinfo.h
    src/formats/map/weather.c
    src/formats/map/weather.h

    src/formats/model/mt5.c
    src/formats/model/mt5.h

    src/formats/script/chrt.c
    src/formats/script/chrt.h
    src/formats/script/scn.c
    src/formats/script/scn.h
    src/formats/script/scnf.c
    src/formats/script/scnf.h

    src/formats/sound/snd.c
    src/formats/sound/snd.h
    src/formats/sound/xwm.c
    src/formats/sound/xwm.h

    src/formats/texture/dds.c
    src/formats/texture/dds.h
    src/formats/texture/pvr.c
    src/formats/texture/pvr.h
    src/formats/texture/spr.c
    src/formats/texture/spr.h

    src/scenario/scnBytecode.c
    src/scenario/scnBytecode.h
    src/scenario/scnCutscene.c
    src/scenario/scnCutscene.h
    src/scenario/scnData.c
    src/scenario/scnData.h
    src/scenario/scnDecompile.c
    src/scenario/scnDecompile.h
    src/scenario/scnDisplay.c
    src/scenario/scnDisplay.h
    src/scenario/scnGoal.c
    src/scenario/scnGoal.h
    src/scenario/scnPair.c
    src/scenario/scnPair.h
    src/scenario/scnVm.c
    src/scenario/scnVm.h

    src/sh4/sh4.c
    src/sh4/sh4.h
    src/sh4/sh4Code.c
    src/sh4/sh4Code.h
    src/sh4/sh4Listing.c
    src/sh4/sh4Listing.h
    src/sh4/sh4Program.c
    src/sh4/sh4Program.h
    PARENT_SCOPE
)

# The application: the files of the game, the rendering, the audio, the interface and the views
set(SRC
    src/CMakeLists.txt
    src/main.c

    src/app/controlPipe.c
    src/app/controlPipe.h
    src/app/dialog.c
    src/app/dialog.h
    src/app/filenameDB.c
    src/app/filenameDB.h
    src/app/fileOpen.c
    src/app/fileOpen.h
    src/app/gameDB.c
    src/app/gameDB.h
    src/app/headless.c
    src/app/headless.h
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

    src/game/mapDate.c
    src/game/mapDate.h
    src/game/mapFiles.c
    src/game/mapFiles.h
    src/game/mapProgram.c
    src/game/mapProgram.h

    src/render/bounds.h
    src/render/freeCamera.c
    src/render/freeCamera.h
    src/render/mt5Export.c
    src/render/mt5Export.h
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

    src/views/chrtView.c
    src/views/chrtView.h
    src/views/fonView.c
    src/views/fonView.h
    src/views/imageView.c
    src/views/imageView.h
    src/views/motnView.c
    src/views/motnView.h
    src/views/mt5View.c
    src/views/mt5View.h
    src/views/sndView.c
    src/views/sndView.h
    src/views/views.c
    src/views/views.h
    src/views/xwmView.c
    src/views/xwmView.h

    src/views/auth/authModels.c
    src/views/auth/authMusic.c
    src/views/auth/authMusic.h
    src/views/auth/authMotions.c
    src/views/auth/authPose.c
    src/views/auth/authProgram.c
    src/views/auth/authSounds.c
    src/views/auth/authSounds.h
    src/views/auth/authView.c
    src/views/auth/authView.h
    src/views/auth/authViewInternal.h

    src/views/mapinfo/mapinfoDate.c
    src/views/mapinfo/mapinfoItems.c
    src/views/mapinfo/mapinfoModels.c
    src/views/mapinfo/mapinfoProperties.c
    src/views/mapinfo/mapinfoView.c
    src/views/mapinfo/mapinfoView.h
    src/views/mapinfo/mapinfoViewInternal.h

    src/views/program/programView.c
    src/views/program/programView.h
    PARENT_SCOPE
)
