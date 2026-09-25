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

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g3 -Wl,-q -Wall -O0 -fno-inline -fno-builtin -fshort-wchar")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3 -fno-rtti -fno-exceptions -Wl,-q -Wall -O0 -fno-builtin -fshort-wchar -Wno-unused-function -Wno-sign-compare")

set(VITA_MKSFOEX_FLAGS
    "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE=33472 -d ATTRIBUTE_MINOR=16"
)

set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -a 0x2F00000000000101")

enable_language(CXX ASM)

# Verify PAF headers exist
execute_process(
  COMMAND find "${VITASDK}" -name "paf.h" -type f
  OUTPUT_VARIABLE PAF_HEADER_FIND
  OUTPUT_STRIP_TRAILING_WHITESPACE
  TIMEOUT 5
)

if(NOT PAF_HEADER_FIND)
  message(FATAL_ERROR "Could not find paf.h. Make sure VITASDK is properly set up with PAF component.")
endif()

# Find vitasdk samples library directory
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

add_executable(${PROJECT_NAME}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/paf_sample.cpp
)

target_link_libraries(${PROJECT_NAME}
  SceAppMgr_stub
  SceKernelThreadMgr_stub
  SceSysmodule_stub
  SceCtrl_stub
  SceLibKernel_stub
  SceLibc_stub_weak
  SceIofilemgr_stub
  SceProcessmgr_stub
  SceNet_stub
  SceNetCtl_stub
  SceSsl_stub
  SceHttp_stub
  ScePaf_stub_weak
  ScePafWebMapView_stub_weak
  SceWebUIPlugin_stub_weak
)

set_target_properties(${PROJECT_NAME}
  PROPERTIES LINK_FLAGS "-nostartfiles -nostdlib"
  COMPILE_FLAGS "'-D__declspec(__v__)=/**/'"
)

set(RCO_FILE "${CMAKE_CURRENT_BINARY_DIR}/vita_moonlight_ui.rco")
set(RCO_SIZE 0)
if(EXISTS "${RCO_FILE}")
  file(SIZE "${RCO_FILE}" RCO_SIZE)
endif()

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME}
  CONFIG exports.yml
  UNSAFE
  STRIPPED
  REL_OPTIMIZE
)

if(RCO_SIZE GREATER 0)
  vita_create_vpk(vita_moonlight_paf.vpk "VLMP00001" ${PROJECT_NAME}.self
    VERSION "01.00"
    NAME "Vita Moonlight PAF UI"
    FILE ${RCO_FILE} vita_moonlight_ui.rco
  )
else()
  message(WARNING "Building VPK without RCO (UI will not display). Run build_cxml.sh first.")
  vita_create_vpk(vita_moonlight_paf.vpk "VLMP00001" ${PROJECT_NAME}.self
    VERSION "01.00"
    NAME "Vita Moonlight PAF UI"
  )
endif()

vita_create_stubs(stubs_${PROJECT_NAME} ${PROJECT_NAME} exports.yml)
