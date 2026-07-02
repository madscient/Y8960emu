cmake_minimum_required(VERSION 3.20)
project(Y8960Emu VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------
#  ymfm サブモジュールのパス
#  git submodule add https://github.com/aaronsgiles/ymfm extern/ymfm
# -------------------------------------------------------
set(YMFM_DIR "${CMAKE_SOURCE_DIR}/extern/ymfm/src" CACHE PATH "ymfm src dir")

# =======================================================
#  ターゲット: Y8960EngineApi.dll / libY8960EngineApi.so
# =======================================================
add_library(Y8960EngineApi SHARED
    src/FmEngineApi.cpp
    src/opl2ex.cpp
    src/opllex.cpp
    ${YMFM_DIR}/ymfm_opl.cpp
    ${YMFM_DIR}/ymfm_adpcm.cpp
    ${YMFM_DIR}/ymfm_pcm.cpp   # ymfm_opl.cpp内のymf278b(OPL4)が参照するpcm_engine実装。
                               # 本DLLではOPL4は使わないが、リンクを満たすために必要。
)

target_include_directories(Y8960EngineApi PRIVATE
    src/
    ${YMFM_DIR}
)

target_compile_definitions(Y8960EngineApi PRIVATE FMENGINE_EXPORTS)

if (MSVC)
    set_target_properties(Y8960EngineApi PROPERTIES
        LINK_FLAGS               "/DEF:\"${CMAKE_SOURCE_DIR}/src/FmEngineApi.def\""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin"
        DEBUG_POSTFIX            ""
    )
    target_compile_options(Y8960EngineApi PRIVATE
        /Oi /GL /W4 /utf-8
        /wd4100   # unreferenced formal parameter (ymfm 内)
        /wd4244   # narrowing conversion (ymfm 内)
        /wd4251   # dll-interface warning
    )
    target_link_options(Y8960EngineApi PRIVATE /LTCG)
    target_sources(Y8960EngineApi PRIVATE src/FmEngineApi.rc)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Y8960EngineApi PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden)
endif()

# =======================================================
#  インストール規則
#  cmake --install build --prefix <dir>
# =======================================================
install(TARGETS Y8960EngineApi
    RUNTIME DESTINATION bin    # .dll
    LIBRARY DESTINATION lib    # .so
    ARCHIVE DESTINATION lib    # .lib (import library)
)
install(FILES src/FmEngineApi.h DESTINATION include)
