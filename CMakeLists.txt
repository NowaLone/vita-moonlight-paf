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

set(VITA_MKSFOEX_FLAGS
    "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE=33472 -d ATTRIBUTE_MINOR=16"
)

set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -a 0x2F00000000000101")

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
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy_moonlight_adapter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy_device_store.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy/device.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy/config_compat.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/legacy/legacy_host_discovery.c
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
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pages/page_search.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pages/page_pairing.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pages/page_apps.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pages/page_stream.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pages/page_add_host.cpp
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
  SceSsl_stub
  SceHttp_stub
  ScePaf_stub_weak
  SceAppSettings_stub
  SceAppUtil_stub
  SceCommonDialog_stub
  SceCommonGuiDialog_stub
  -Wl,--end-group
)

set_target_properties(${PROJECT_NAME}
  PROPERTIES LINK_FLAGS "-nostartfiles -nostdlib"
  COMPILE_FLAGS "'-D__declspec(__v__)=/**/'"
)

set(RCO_FILE "${CMAKE_CURRENT_BINARY_DIR}/vita_moonlight_ui.rco")
if(NOT EXISTS "${RCO_FILE}")
  message(FATAL_ERROR "Missing vita_moonlight_ui.rco. Run build_cxml.sh before configuring the project.")
endif()

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)

vita_create_vpk(vita_moonlight_paf.vpk "VLMP00001" ${PROJECT_NAME}.self
  VERSION "01.00"
  NAME "Vita Moonlight PAF UI"
  FILE ${RCO_FILE} vita_moonlight_ui.rco
)
vita_create_stubs(stubs_${PROJECT_NAME} ${PROJECT_NAME} exports.yml)
