cmake_minimum_required(VERSION 3.0)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(vita_moonlight_paf)
include("${VITASDK}/share/vita.cmake" REQUIRED)

# Embed the source revision into the About page at configure time.
execute_process(
  COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=12 HEAD
  OUTPUT_VARIABLE VITA_MOONLIGHT_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE VITA_MOONLIGHT_GIT_RESULT
)

if(NOT VITA_MOONLIGHT_GIT_RESULT EQUAL 0 OR NOT VITA_MOONLIGHT_GIT_COMMIT)
  set(VITA_MOONLIGHT_GIT_COMMIT "unknown")
endif()

add_definitions(-DMOONLIGHT_GIT_COMMIT=\"${VITA_MOONLIGHT_GIT_COMMIT}\")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g3 -Wl,-q -Wall -O0 -fno-inline -fno-builtin -fshort-wchar")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3 -fno-rtti -fno-exceptions -Wl,-q -Wall -O0 -fno-builtin -fshort-wchar -Wno-unused-function -Wno-sign-compare")

option(MOONLIGHT_SYSTEM_APP "Experiment: launch alongside a suspended Vita game" OFF)
option(MOONLIGHT_HEAP_TRACE "Diagnostic: track newlib allocation call sites" OFF)
option(MOONLIGHT_DISCONNECT_TEST "Test only: disconnect/reconnect once after opening Apps" OFF)
option(MOONLIGHT_ACTIVE_DISCONNECT_TEST "Test only: disconnect/reconnect after a selected stream starts" OFF)
option(MOONLIGHT_PENDING_DISCONNECT_TEST "Test only: disconnect after launch response before RTSP" OFF)
option(MOONLIGHT_HTTP_DISCONNECT_TEST "Test only: disconnect inside launch/resume HTTPS transfer" OFF)
option(MOONLIGHT_STARTUP_DISCONNECT_TEST "Test only: disconnect during selected common-c startup stage" OFF)
set(MOONLIGHT_STARTUP_DISCONNECT_STAGE "4" CACHE STRING "Startup disconnect checkpoint: 4=RTSP entry, 10=after video setup")
set_property(CACHE MOONLIGHT_STARTUP_DISCONNECT_STAGE PROPERTY STRINGS 4 10)
if(MOONLIGHT_STARTUP_DISCONNECT_TEST AND NOT MOONLIGHT_STARTUP_DISCONNECT_STAGE MATCHES "^(4|10)$")
  message(FATAL_ERROR "MOONLIGHT_STARTUP_DISCONNECT_STAGE must be 4 or 10")
endif()
if((MOONLIGHT_STARTUP_DISCONNECT_TEST AND (MOONLIGHT_DISCONNECT_TEST OR MOONLIGHT_ACTIVE_DISCONNECT_TEST OR MOONLIGHT_PENDING_DISCONNECT_TEST OR MOONLIGHT_HTTP_DISCONNECT_TEST)) OR
   (MOONLIGHT_HTTP_DISCONNECT_TEST AND (MOONLIGHT_DISCONNECT_TEST OR MOONLIGHT_ACTIVE_DISCONNECT_TEST OR MOONLIGHT_PENDING_DISCONNECT_TEST)) OR
   (MOONLIGHT_DISCONNECT_TEST AND MOONLIGHT_ACTIVE_DISCONNECT_TEST) OR
   (MOONLIGHT_PENDING_DISCONNECT_TEST AND (MOONLIGHT_DISCONNECT_TEST OR MOONLIGHT_ACTIVE_DISCONNECT_TEST)))
  message(FATAL_ERROR "Select only one disconnect test: idle, active stream, pending setup, HTTP or startup")
endif()
if(MOONLIGHT_HEAP_TRACE AND NOT MOONLIGHT_SYSTEM_APP)
  message(FATAL_ERROR "MOONLIGHT_HEAP_TRACE requires MOONLIGHT_SYSTEM_APP=ON")
endif()
set(MOONLIGHT_SYSTEM_MEMSIZE_KIB 65536 CACHE STRING "System app total memory budget (KiB)")
set(MOONLIGHT_SYSTEM_PHYCONT_KIB 0 CACHE STRING "System app physical reservation inside total budget (KiB)")
set(MOONLIGHT_SYSTEM_NEWLIB_KIB 6144 CACHE STRING "System app newlib heap capacity (KiB)")
set(MOONLIGHT_SYSTEM_PAF_KIB 10240 CACHE STRING "System app PAF global heap capacity (KiB)")

if(MOONLIGHT_SYSTEM_APP)
  foreach(budget MEMSIZE PHYCONT NEWLIB PAF)
    if(NOT "${MOONLIGHT_SYSTEM_${budget}_KIB}" MATCHES "^(0|[1-9][0-9]*)$" OR
       MOONLIGHT_SYSTEM_${budget}_KIB GREATER 75776)
      message(FATAL_ERROR "MOONLIGHT_SYSTEM_${budget}_KIB must be an integer from 0 to 75776")
    endif()
    math(EXPR budget_remainder "${MOONLIGHT_SYSTEM_${budget}_KIB} % 4")
    if(NOT budget_remainder EQUAL 0)
      message(FATAL_ERROR "MOONLIGHT_SYSTEM_${budget}_KIB must be aligned to 4 KiB")
    endif()
  endforeach()
  if(MOONLIGHT_SYSTEM_MEMSIZE_KIB LESS 4096 OR
     MOONLIGHT_SYSTEM_NEWLIB_KIB LESS 1024 OR MOONLIGHT_SYSTEM_PAF_KIB LESS 1024)
    message(FATAL_ERROR "System total must be >= 4096 KiB and both heaps >= 1024 KiB")
  endif()
  math(EXPR reserved_kib "${MOONLIGHT_SYSTEM_NEWLIB_KIB} + ${MOONLIGHT_SYSTEM_PAF_KIB} + ${MOONLIGHT_SYSTEM_PHYCONT_KIB}")
  if(NOT reserved_kib LESS MOONLIGHT_SYSTEM_MEMSIZE_KIB)
    message(FATAL_ERROR "System heaps and physical reservation must leave room inside the total budget")
  endif()
  set(moonlight_category gdc)
  # Physical reservation is part of MEMSIZE, not additional memory. The nongame
  # decoder allocates its explicit contiguous arena from main RAM at runtime.
  set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -pm ${MOONLIGHT_SYSTEM_PHYCONT_KIB}")
  message(STATUS "Moonlight launch profile: SYSTEM (gdc, ${MOONLIGHT_SYSTEM_MEMSIZE_KIB} KiB, boot attribute 0x08)")
else()
  set(moonlight_category gd)
  message(STATUS "Moonlight launch profile: NORMAL (gd). System experiment requires -DMOONLIGHT_SYSTEM_APP=ON")
endif()

# Preserve the current SFO attribute bits while isolating the launch category.
set(VITA_MKSFOEX_FLAGS
    "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE=33472 -d ATTRIBUTE_MINOR=16 -s CATEGORY=${moonlight_category}"
)

# Match BHBB's native PAF authority. A controlled 3.60 test changing only this
# ID removed the ux0/vs0 invalid:/ overlays seen with 0x2F00000000000101.
# Actual AppSettings persistence and PS/Quick Actions still need regression tests.
set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -a 0x2200000000000001")

enable_language(CXX ASM)

execute_process(
  COMMAND find "${VITASDK}" -name "paf.h" -type f
  OUTPUT_VARIABLE PAF_HEADER_FIND
  OUTPUT_STRIP_TRAILING_WHITESPACE
  TIMEOUT 5
)

if(NOT PAF_HEADER_FIND)
  message(FATAL_ERROR "Could not find paf.h. Make sure VITASDK is properly set up with PAF component.")
endif()

set(VITASDK_SAMPLES_LIB "${VITASDK}/../vitasdk-paf-component/paf_samples/vitasdk")
if(EXISTS "${VITASDK_SAMPLES_LIB}")
  link_directories(${VITASDK_SAMPLES_LIB})
  message(STATUS "Found vitasdk samples lib directory")
else()
  message(WARNING "Could not find vitasdk samples at expected location, will try fallback")
  set(VITASDK_SAMPLES_LIB "${VITASDK}/build")
  if(EXISTS "${VITASDK_SAMPLES_LIB}")
    link_directories(${VITASDK_SAMPLES_LIB})
  endif()
endif()


# Upstream Moonlight streaming engine and its ENet transport.
set(MOONLIGHT_COMMON_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/AudioStream.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/ByteBuffer.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/Connection.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/ConnectionTester.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/ControlStream.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/FakeCallbacks.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/InputStream.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/LinkedBlockingQueue.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/Misc.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/Platform.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/PlatformCrypto.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/PlatformSockets.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/RecorderCallbacks.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/RtpAudioQueue.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/RtpVideoQueue.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/RtspConnection.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/RtspParser.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/SdpGenerator.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/SimpleStun.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/VideoDepacketizer.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/VideoStream.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/reedsolomon/rs.c
)

set(ENET_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/callbacks.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/compress.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/host.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/list.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/packet.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/peer.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/protocol.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/unix.c
)

add_executable(${PROJECT_NAME}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/paf_runtime.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/moonlight_app.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/event_queue.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy_moonlight_adapter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy_device_store.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy/device.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy/config_compat.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy/legacy_host_discovery.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy/legacy_host_probe.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy/legacy_gamestream.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy/mdns/udp_sniffer_vita.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/inih/ini.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/services/host_service.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/services/host_discovery_service.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/services/pairing_service.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/services/connection_service.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/services/application_service.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/services/settings_service.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/option_menu.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/api.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/stream_session.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/gamestream_xml.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/stream_stats.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/vita_video_renderer.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/vita_audio_renderer.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/vita_input.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/vita_touch.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/vita_motion.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/sps.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/h264bitstream/h264_stream.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/h264bitstream/h264_nal.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/h264bitstream/h264_sei.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/settings.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pages/page.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pages/page_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pages/page_pairing.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pages/page_apps.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pages/page_stream.cpp
  ${MOONLIGHT_COMMON_SOURCES}
  ${ENET_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/inih
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/reedsolomon
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/h264bitstream
  ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight
)

if(MOONLIGHT_STARTUP_DISCONNECT_TEST)
  message(STATUS "Startup disconnect test enabled at stage ${MOONLIGHT_STARTUP_DISCONNECT_STAGE}; expect cancellation error/OK")
  target_compile_definitions(${PROJECT_NAME} PRIVATE MOONLIGHT_DISCONNECT_TEST=1 MOONLIGHT_STARTUP_DISCONNECT_TEST=1
    MOONLIGHT_STARTUP_DISCONNECT_STAGE=${MOONLIGHT_STARTUP_DISCONNECT_STAGE})
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/disconnect_startup.cpp)
elseif(MOONLIGHT_HTTP_DISCONNECT_TEST)
  message(STATUS "HTTP disconnect test enabled: cancel inside curl before response; acknowledge expected error")
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    MOONLIGHT_DISCONNECT_TEST=1 MOONLIGHT_PENDING_DISCONNECT_TEST=1 MOONLIGHT_HTTP_DISCONNECT_TEST=1)
  target_sources(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/disconnect_pending.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/disconnect_http.c)
elseif(MOONLIGHT_PENDING_DISCONNECT_TEST)
  message(STATUS "Pending disconnect test enabled: cancel after launch response before RTSP; acknowledge expected error")
  target_compile_definitions(${PROJECT_NAME} PRIVATE MOONLIGHT_DISCONNECT_TEST=1 MOONLIGHT_PENDING_DISCONNECT_TEST=1)
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/disconnect_pending.cpp)
elseif(MOONLIGHT_ACTIVE_DISCONNECT_TEST)
  message(STATUS "Active disconnect test enabled: select an app; stream disconnects once after 3 seconds")
  target_compile_definitions(${PROJECT_NAME} PRIVATE MOONLIGHT_DISCONNECT_TEST=1)
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/disconnect_active.cpp)
elseif(MOONLIGHT_DISCONNECT_TEST)
  message(STATUS "Disconnect test enabled: opening Apps triggers one local disconnect/reconnect")
  target_compile_definitions(${PROJECT_NAME} PRIVATE MOONLIGHT_DISCONNECT_TEST=1)
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/disconnect_smoke.cpp)
endif()

if(MOONLIGHT_SYSTEM_APP)
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    MOONLIGHT_SYSTEM_APP=1
    MOONLIGHT_SYSTEM_MEMSIZE_KIB=${MOONLIGHT_SYSTEM_MEMSIZE_KIB}
    MOONLIGHT_SYSTEM_PHYCONT_KIB=${MOONLIGHT_SYSTEM_PHYCONT_KIB}
    MOONLIGHT_SYSTEM_NEWLIB_KIB=${MOONLIGHT_SYSTEM_NEWLIB_KIB}
    MOONLIGHT_SYSTEM_PAF_KIB=${MOONLIGHT_SYSTEM_PAF_KIB}
  )
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/app/memory_budget.cpp)
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/vita_transport_threads.c)
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/moonlight/vita_system_video.c)
  # Adapt only common-c's pthread setup; keep its submodule revision unchanged.
  # Its platform layer requests 1 MiB stacks and leaves pthread attributes alive.
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src/Platform.c
    APPEND_STRING PROPERTY COMPILE_FLAGS " -include moonlight/vita_transport_threads.h")
endif()

# The system renderer owns a CodecEngine pool for explicit decoder contexts.
if(MOONLIGHT_SYSTEM_APP)
  target_link_libraries(${PROJECT_NAME} SceCodecEngine_stub)
endif()

if(MOONLIGHT_HEAP_TRACE)
  target_compile_definitions(${PROJECT_NAME} PRIVATE MOONLIGHT_HEAP_TRACE=1)
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/app/heap_trace.c)
  set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS
    " -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free")
endif()

target_link_libraries(${PROJECT_NAME}
  -Wl,--start-group
  -lcurl
  -lexpat
  -lssl
  -lcrypto
  -lz
  -lzstd
  -lopus
  -lpthread
  -lm
  -lgcc
  -lc
  SceAppMgr_stub
  SceKernelThreadMgr_stub
  SceSysmem_stub
  SceRtc_stub
  SceSysmodule_stub
  SceCtrl_stub
  SceTouch_stub
  SceMotion_stub
  SceShellSvc_stub
  SceLibKernel_stub
  SceLibc_stub_weak
  SceIofilemgr_stub
  SceProcessmgr_stub
  SceNet_stub
  SceNetCtl_stub
  SceVideodec_stub
  SceAudio_stub
  SceGxm_stub
  SceDisplay_stub
  SceSsl_stub
  SceHttp_stub
  ScePaf_stub_weak
  SceAppSettings_stub
  SceAppUtil_stub
  SceCommonDialog_stub
  SceCommonGuiDialog_stub
  SceIme_stub
  -Wl,--end-group
)

set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS
  " -nostartfiles -nostdlib"
)
set_target_properties(${PROJECT_NAME}
  PROPERTIES COMPILE_FLAGS "'-D__declspec(__v__)=/**/'"
)

# VitaSDK's SELF/stub helpers expect the extensionless target path. Keep it and
# expose the same unstripped ELF with an extension for crash-dump tools.
set(DEBUG_ELF_FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf")
add_custom_command(OUTPUT "${DEBUG_ELF_FILE}"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${DEBUG_ELF_FILE}"
  DEPENDS ${PROJECT_NAME}
  COMMENT "Copying debug ELF ${PROJECT_NAME}.elf"
  VERBATIM
)
add_custom_target(${PROJECT_NAME}_debug_elf ALL DEPENDS "${DEBUG_ELF_FILE}")

set(RCO_FILE "${CMAKE_CURRENT_BINARY_DIR}/vita_moonlight_ui.rco")
if(NOT EXISTS "${RCO_FILE}")
  message(FATAL_ERROR "Missing vita_moonlight_ui.rco. Run build_cxml.sh before configuring the project.")
endif()

if(MOONLIGHT_SYSTEM_APP)
  # BHBB-style boot attributes. With nonzero MEMSIZE, -at is actually written.
  vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE
    MEMSIZE ${MOONLIGHT_SYSTEM_MEMSIZE_KIB} ATTRIBUTE 0x08)
else()
  # Preserve the ordinary launch configuration.
  vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE ATTRIBUTE 0x0E)
endif()

# sce_sys PNGs must stay 8-bit indexed. 24-bit assets fail VPK install with 0x8010113D.
vita_create_vpk(vita_moonlight_paf.vpk "VLMP00001" ${PROJECT_NAME}.self
  VERSION "01.00"
  NAME "Vita Moonlight PAF UI"
  FILE ${RCO_FILE} vita_moonlight_ui.rco
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/pic0.png sce_sys/pic0.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/frame1.png sce_sys/livearea/contents/frame1.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/frame2.png sce_sys/livearea/contents/frame2.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/frame3.png sce_sys/livearea/contents/frame3.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/frame4.png sce_sys/livearea/contents/frame4.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/frame5.png sce_sys/livearea/contents/frame5.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/frame6.png sce_sys/livearea/contents/frame6.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/frame7.png sce_sys/livearea/contents/frame7.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/manual/001.png sce_sys/manual/001.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/manual/002.png sce_sys/manual/002.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/manual/003.png sce_sys/manual/003.png
)
vita_create_stubs(stubs_${PROJECT_NAME} ${PROJECT_NAME} exports.yml)
