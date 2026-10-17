cmake_minimum_required(VERSION 3.16)
project(TotalChaosAGA711Recomp C CXX)

# Total Chaos AGA 7.11 (Amiga, J. Conwell + Team Chaos 2008) - port nativo per
# ricompilazione statica, con AmigaOS e i chip custom riscritti in C.
#
#   1. python tools/estrai_hdf.py ../TotalChaos711.hdf dati_gioco
#      python game/prepara_immagine.py dati_gioco/DH0/TotalChaos
#   2. cmake -S . -B build/cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#   3. cmake --build build/cmake
#
# Il C generato sta solo nella cartella di build e non si modifica a mano.

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(CHAOS_IMAGE "${CMAKE_SOURCE_DIR}/build/chaos_immagine.bin" CACHE FILEPATH
    "Immagine preparata da game/prepara_immagine.py")
set(CHAOS_SDL2_DIR "${CMAKE_SOURCE_DIR}/../../../RAMH traduzione/sdl/SDL2-2.32.2" CACHE PATH
    "Pacchetto di sviluppo SDL2 (include/ e lib/x64/)")

# ---- ricompilatore ---------------------------------------------------------------
add_subdirectory(amigarecomp/recompiler)

set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
set(GEN_FILES "${GEN_DIR}/chaos_decls.h" "${GEN_DIR}/chaos_dispatch.c"
              "${GEN_DIR}/chaos_layout.c" "${GEN_DIR}/chaos_cycles.c")
foreach(i RANGE 0 31)
    string(LENGTH "${i}" _n)
    if(_n EQUAL 1)
        set(i "0${i}")
    endif()
    list(APPEND GEN_FILES "${GEN_DIR}/chaos_part${i}.c")
endforeach()

add_custom_command(
    OUTPUT ${GEN_FILES}
    COMMAND AmigaRecomp "${CHAOS_IMAGE}" --game game.toml --output-dir "${GEN_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/game"
    DEPENDS AmigaRecomp "${CHAOS_IMAGE}" "${CMAKE_SOURCE_DIR}/game/game.toml"
            "${CMAKE_SOURCE_DIR}/build/chaos_semi.toml" "${CMAKE_SOURCE_DIR}/game/funzioni_scoperte.toml"
    COMMENT "AmigaRecomp: 68000 -> C"
    VERBATIM)

# ---- Musashi (interprete 68020 per la prova differenziale, AMIGA_VERIFICA) ----------
set(MUSASHI_DIR "${CMAKE_SOURCE_DIR}/external/musashi")
set(MUSASHI_GEN "${CMAKE_BINARY_DIR}/musashi")
add_executable(m68kmake "${MUSASHI_DIR}/m68kmake.c")
target_compile_options(m68kmake PRIVATE -w)
add_custom_command(
    OUTPUT "${MUSASHI_GEN}/m68kops.c" "${MUSASHI_GEN}/m68kops.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MUSASHI_GEN}"
    COMMAND m68kmake "${MUSASHI_GEN}" "${MUSASHI_DIR}/m68k_in.c"
    DEPENDS m68kmake "${MUSASHI_DIR}/m68k_in.c"
    COMMENT "Musashi: tabella delle istruzioni"
    VERBATIM)
add_library(musashi STATIC "${MUSASHI_DIR}/m68kcpu.c" "${MUSASHI_GEN}/m68kops.c"
            "${MUSASHI_DIR}/softfloat/softfloat.c")
target_include_directories(musashi PUBLIC "${MUSASHI_DIR}" "${MUSASHI_GEN}")
target_compile_options(musashi PRIVATE -w
    -include "${CMAKE_SOURCE_DIR}/amigarecomp/runner/musashi_aggancio.h")

# ---- runner + gioco ----------------------------------------------------------------
add_executable(TotalChaosAGA711Recomp
    amigarecomp/runner/amiga_glue.c
    amigarecomp/runner/amiga_hw.c
    amigarecomp/runner/amiga_debug.c
    amigarecomp/runner/amiga_paula.c
    amigarecomp/runner/amiga_video_gl.c
    amigarecomp/runner/amiga_ingressi.c
    amigarecomp/runner/amiga_console.c
    amigarecomp/runner/amiga_menu.c
    amigarecomp/runner/amiga_cpu020.c
    amigarecomp/runner/amiga_os.c
    amigarecomp/runner/amiga_exec.c
    amigarecomp/runner/amiga_dos.c
    amigarecomp/runner/amiga_grafica.c
    amigarecomp/runner/amiga_intuition.c
    amigarecomp/runner/amiga_device.c
    amigarecomp/runner/amiga_voce.c
    amigarecomp/runner/amiga_verifica.c
    amigarecomp/runner/amiga_crash.c
    amigarecomp/runner/amiga_task.c
    amigarecomp/runner/amiga_utility.c
    amigarecomp/runner/amiga_nomi_v40.c
    amigarecomp/runner/amiga_keymap.c
    amigarecomp/runner/amiga_layers.c
    amigarecomp/runner/amiga_asl.c
    amigarecomp/runner/amiga_iffparse.c
    amigarecomp/runner/amiga_diskfont.c
    amigarecomp/runner/amiga_lha.c
    amigarecomp/runner/amiga_interprete.c
    amigarecomp/runner/amiga_nomi.c
    game/tc_spec.c
    game/tc_immagine.c
    game/tc_installa.c
    game/tc_launcher.c
    game/tc_manuale.c
    game/tc_testi_fissi.c
    game/tc_pp20.c
    game/tc_tutorial.c
    amigarecomp/runner/amiga_impostazioni.c
    amigarecomp/runner/amiga_shorten.c
    ${GEN_FILES})

target_include_directories(TotalChaosAGA711Recomp PRIVATE
    amigarecomp/runner
    amigarecomp/runner/include
    game
    "${GEN_DIR}"
    "${CMAKE_SOURCE_DIR}/build"            # chaos_segmenti.h da game/prepara_immagine.py
    "${CHAOS_SDL2_DIR}/include")

target_link_directories(TotalChaosAGA711Recomp PRIVATE "${CHAOS_SDL2_DIR}/lib/x64")
target_link_libraries(TotalChaosAGA711Recomp PRIVATE SDL2 opengl32 sapi ole32 uuid comdlg32 musashi)
if(MINGW OR CMAKE_CXX_COMPILER_TARGET MATCHES "mingw" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_link_options(TotalChaosAGA711Recomp PRIVATE -static-libstdc++ -static-libgcc
                        "-Wl,-Bstatic" -lc++ -lunwind "-Wl,-Bdynamic"
                        $<$<CONFIG:Release>:-s>)
endif()
set_source_files_properties(${GEN_FILES} PROPERTIES COMPILE_OPTIONS "-w")

add_custom_command(TARGET TotalChaosAGA711Recomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CHAOS_SDL2_DIR}/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:TotalChaosAGA711Recomp>")

# l'immagine serve solo al ricompilatore: il programma la ricostruisce
# all'avvio dai file del gioco installati (game/tc_immagine.c)

# il launcher recomp-ui (profilo "amiga"): pacchetto, installazione, lingua, video, audio
option(TC_LAUNCHER "Launcher recomp-ui all'avvio" ON)
if(TC_LAUNCHER)
    set(RECOMP_UI_SDL3 OFF CACHE BOOL "" FORCE)
    include("${CMAKE_SOURCE_DIR}/external/recomp-ui/recomp_ui.cmake")
    recomp_target_launcher_ui(TotalChaosAGA711Recomp CONSOLE amiga)
    target_include_directories(TotalChaosAGA711Recomp PRIVATE "${CMAKE_SOURCE_DIR}/external/recomp-ui/src")
endif()

# le nostre modifiche al pacchetto di Aminet (tools/crea_modifiche.py), accanto all'eseguibile
if(EXISTS "${CMAKE_SOURCE_DIR}/build/tc_modifiche.bin")
    add_custom_target(tc_modifiche ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/build/tc_modifiche.bin"
                "$<TARGET_FILE_DIR:TotalChaosAGA711Recomp>/tc_modifiche.bin"
        DEPENDS TotalChaosAGA711Recomp
        COMMENT "modifiche del port accanto all'eseguibile")
endif()

# i punti di chiamata del C generato: le percentuali di copertura della
# finestra di debug (--debug)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/copertura_siti.bin"
        COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/siti_copertura.py" "${GEN_DIR}"
                "${CMAKE_BINARY_DIR}/copertura_siti.bin" 0x23A000
        DEPENDS ${GEN_FILES} "${CMAKE_SOURCE_DIR}/tools/siti_copertura.py")
    add_custom_target(copertura_siti ALL DEPENDS "${CMAKE_BINARY_DIR}/copertura_siti.bin"
        COMMENT "punti di chiamata per la copertura")
endif()
