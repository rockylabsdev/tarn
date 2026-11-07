# Installs the build into SCRATCH and checks every file landed.
file(REMOVE_RECURSE "${SCRATCH}")
execute_process(
    COMMAND ${CMAKE_COMMAND} --install ${BUILD_DIR} --prefix ${SCRATCH}
    RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "cmake --install failed (${rc})")
endif()

set(expected
    bin/tarn
    bin/tarn-clip
    share/applications/tarn.desktop
    share/doc/tarn/tarn.lua
    share/doc/tarn/waybar/tarn-recording.sh)
foreach(f IN LISTS expected)
    if(NOT EXISTS "${SCRATCH}/${f}")
        message(SEND_ERROR "not installed: ${f}")
    endif()
endforeach()

if(EXISTS "${SCRATCH}/share/applications/tarn.desktop")
    file(READ "${SCRATCH}/share/applications/tarn.desktop" desktop)
    if(NOT desktop MATCHES "\nExec=tarn history\n")
        message(SEND_ERROR "tarn.desktop must run `tarn history`")
    endif()
    find_program(VALIDATOR desktop-file-validate)
    if(VALIDATOR)
        execute_process(COMMAND ${VALIDATOR} "${SCRATCH}/share/applications/tarn.desktop"
            RESULT_VARIABLE vrc OUTPUT_VARIABLE vout ERROR_VARIABLE vout)
        if(NOT vrc EQUAL 0)
            message(SEND_ERROR "desktop-file-validate: ${vout}")
        endif()
    endif()
endif()

if(EXISTS "${SCRATCH}/share/doc/tarn/tarn.lua")
    file(READ "${SCRATCH}/share/doc/tarn/tarn.lua" lua)
    if(lua MATCHES "/home/|qs ipc")
        message(SEND_ERROR "tarn.lua names a home folder or a shell command")
    endif()
endif()
