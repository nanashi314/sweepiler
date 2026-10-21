# The web build: the core and the pixel UI in WebAssembly (Emscripten), drawn
# with WebGL 2 through SDL2, without Z3. See docs/WEB.md.
#   emcmake cmake -S web -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-web
cmake_minimum_required(VERSION 3.24)
project(SweepilerWeb VERSION 1.4.0 LANGUAGES CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure the web build with emcmake (Emscripten).")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9b GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
# The Japanese font for the reference and code comments (SIL OFL 1.1), as the
# Windows fonts the native app uses are not there.
set(IMS_WEB_FONT ${CMAKE_BINARY_DIR}/fonts/BIZUDGothic-Regular.ttf)
if(NOT EXISTS ${IMS_WEB_FONT})
  file(DOWNLOAD https://github.com/google/fonts/raw/main/ofl/bizudgothic/BIZUDGothic-Regular.ttf ${IMS_WEB_FONT} STATUS got)
  file(DOWNLOAD https://github.com/google/fonts/raw/main/ofl/bizudgothic/OFL.txt ${CMAKE_BINARY_DIR}/fonts/OFL.txt)
endif()
# Threads (the session builds and ticks on workers) and native wasm exceptions everywhere.
add_compile_options(-pthread -fwasm-exceptions -sUSE_ZLIB=1 "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/WebPort.h")
add_link_options(-pthread -fwasm-exceptions -sUSE_ZLIB=1)
add_library(imgui STATIC ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# 32-bit draw indices: WebGL has no glDrawElementsBaseVertex, so the OpenGL 3
# backend cannot split a draw list, and the board near the zoom where cells
# turn to plain colours passes 65536 vertices.
target_compile_definitions(imgui PUBLIC "ImDrawIdx=unsigned int")
target_compile_options(imgui PUBLIC -sUSE_SDL=2)
target_link_options(imgui PUBLIC -sUSE_SDL=2)
# core: as in the native build, but FrameSolver's Z3 fallback reports an error (IMS_NO_Z3).
add_library(core STATIC ${ROOT}/src/Board.cpp ${ROOT}/src/BuildControl.cpp ${ROOT}/src/Circuits.cpp ${ROOT}/src/ClassicCircuits.cpp ${ROOT}/src/ClassicKernels.cpp ${ROOT}/src/ComposedCircuits.cpp ${ROOT}/src/Constraints.cpp ${ROOT}/src/ClassicSolver.cpp ${ROOT}/src/CircuitVerification.cpp ${ROOT}/src/Rules.cpp ${ROOT}/src/NorNetlist.cpp ${ROOT}/src/GridLayout.cpp ${ROOT}/src/MachineFrame.cpp ${ROOT}/src/FrameSolver.cpp ${ROOT}/src/FrameMachine.cpp ${ROOT}/src/GameLanguage.cpp ${ROOT}/src/MineC.cpp ${ROOT}/src/MachineSession.cpp)
target_include_directories(core PUBLIC ${ROOT}/src)
target_compile_definitions(core PUBLIC IMS_NO_Z3)
set(IMS_WEB_MEMORY -sALLOW_MEMORY_GROWTH -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=4MB -sDEFAULT_PTHREAD_STACK_SIZE=4MB)
# sweepiler: the page (sweepiler.html, .js, .wasm, .data).
add_executable(sweepiler WebMain.cpp LabWeb.cpp PixelDocsWeb.cpp ${ROOT}/ui/LabCommon.cpp ${ROOT}/ui/PixelApp.cpp ${ROOT}/ui/PixelFont.cpp ${ROOT}/ui/PixelEditor.cpp ${ROOT}/ui/PixelDocs.cpp ${ROOT}/ui/MineCDocs.cpp)
target_include_directories(sweepiler PRIVATE ${ROOT}/ui)
target_link_libraries(sweepiler PRIVATE core imgui)
set_target_properties(sweepiler PROPERTIES SUFFIX .html LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
target_link_options(sweepiler PRIVATE ${IMS_WEB_MEMORY} -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sPTHREAD_POOL_SIZE=3
  -sEXPORTED_FUNCTIONS=_main,_web_opened,_web_status,_web_touch_mode,_web_type,_web_key -sEXPORTED_RUNTIME_METHODS=ccall,FS -lidbfs.js
  "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html" "SHELL:--preload-file ${IMS_WEB_FONT}@/fonts/BIZUDGothic-Regular.ttf")
add_custom_command(TARGET sweepiler POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/coi-serviceworker.js ${CMAKE_BINARY_DIR}/fonts/OFL.txt ${CMAKE_BINARY_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/sweepiler.html ${CMAKE_BINARY_DIR}/index.html)
# frame_tool: lays out and times the samples under Node, and writes frame caches for the page.
add_executable(frame_tool FrameTool.cpp)
target_link_libraries(frame_tool PRIVATE core)
target_link_options(frame_tool PRIVATE ${IMS_WEB_MEMORY} -sNODERAWFS -sEXIT_RUNTIME)
# web_frames: every sample's frame in frames/ (*.frame.gz is what the page fetches). About two minutes.
add_custom_target(web_frames COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:frame_tool> ${CMAKE_BINARY_DIR}/frames --ticks=5
  DEPENDS frame_tool WORKING_DIRECTORY ${CMAKE_BINARY_DIR} USES_TERMINAL)
# web_site: what to publish, in site/ (run web_frames first for the prebuilt frames).
add_custom_target(web_site COMMAND ${CMAKE_COMMAND} -DBUILD=${CMAKE_BINARY_DIR} -DSITE=${CMAKE_BINARY_DIR}/site -P ${CMAKE_CURRENT_SOURCE_DIR}/site.cmake DEPENDS sweepiler)
