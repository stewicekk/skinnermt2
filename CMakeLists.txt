cmake_minimum_required(VERSION 3.21)

project(Metin2RiggingStudio
    VERSION 0.10.0
    DESCRIPTION "Metin2-only rigging, skinning, weight-transfer and armor authoring studio"
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(M2RIG_WITH_UI "Build the Dear ImGui + DirectX 11 desktop application" ON)
option(M2RIG_WITH_TESTS "Build and register CTest suites" ON)
option(M2RIG_WITH_CLI "Build the headless batch tool (core only)" ON)
option(M2RIG_WITH_GIZMO "Build with ImGuizmo bone manipulators (needs network at configure)" ON)
option(M2RIG_WITH_OPENFBX "Build with native OpenFBX reader (needs network at configure)" ON)
option(M2RIG_WITH_CGLTF "Build with native cgltf reader (needs network at configure)" ON)
option(M2RIG_WARNINGS_AS_ERRORS "Treat MSVC warnings as errors for first-party code" ON)
option(M2RIG_HEAP_TRIAGE "CRT heap validation in debug tests (very slow, crash triage only)" OFF)

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /utf-8 /FS)
endif()

# ---------------------------------------------------------------- core ---
add_library(m2rig_core STATIC
    src/anim.cpp
    src/logging.cpp
    src/lod.cpp
    src/diagnostics.cpp
    src/validation.cpp
    src/mesh.cpp
    src/skeleton.cpp
    src/skin_weights.cpp
    src/profiles.cpp
    src/json.cpp
    src/samples.cpp
    src/smd.cpp
    src/app_state.cpp
    src/mesh_views.cpp
    src/dds.cpp
    src/mse.cpp
    src/gr2_deep_parser.cpp
    src/self_learning.cpp
    src/ast/msm_ast.cpp
    src/adapters/gr2_adapter.cpp
    src/adapters/bridge_process.cpp
    src/extractors/universal_weight_extractor.cpp
    src/workspace/project_file.cpp
    src/coordsys.cpp
    src/utf8.cpp
    src/ibl.cpp
    src/spatial.cpp
)
target_include_directories(m2rig_core PUBLIC include)
if(MSVC)
    target_compile_options(m2rig_core PRIVATE /W4)
    if(M2RIG_WARNINGS_AS_ERRORS)
        target_compile_options(m2rig_core PRIVATE /WX)
    endif()
endif()

# ------------------------------------------------- native OpenFBX (Wave 9) --
# Pinned master HEAD verified via GitHub API (nem0/OpenFBX, NOT the dead
# ufo0905 fork). libdeflate ships inside its src/ tree, so no second
# dependency is needed. Core stays clean: only m2rig_fbx links it.
set(M2RIG_OPENFBX_REV "4d4a45a0ed8f357d11ff83904b087530547a4071"
    CACHE STRING "Pinned OpenFBX commit")
if(M2RIG_WITH_OPENFBX AND (M2RIG_WITH_UI OR M2RIG_WITH_CLI OR M2RIG_WITH_TESTS))
    include(FetchContent)
    FetchContent_Declare(
        openfbx
        URL "https://github.com/nem0/OpenFBX/archive/${M2RIG_OPENFBX_REV}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(openfbx)
    if(NOT openfbx_POPULATED)
        FetchContent_Populate(openfbx)
    endif()
    add_library(openfbx_lib STATIC
        ${openfbx_SOURCE_DIR}/src/ofbx.cpp
        ${openfbx_SOURCE_DIR}/src/libdeflate.c
    )
    target_include_directories(openfbx_lib PUBLIC ${openfbx_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(openfbx_lib PRIVATE /W3 /WX-)
    endif()
        add_library(m2rig_fbx STATIC src/fbx/fbx_reader.cpp)
        target_include_directories(m2rig_fbx PUBLIC include)
        target_link_libraries(m2rig_fbx PUBLIC m2rig_core openfbx_lib)
        # Core TUs that call the adapter (app_state.cpp) need the switch too;
        # m2rig_core itself still links nothing third-party.
        target_compile_definitions(m2rig_core PRIVATE M2RIG_WITH_OPENFBX=1)
    if(MSVC)
        target_compile_options(m2rig_fbx PRIVATE /W4)
        if(M2RIG_WARNINGS_AS_ERRORS)
            target_compile_options(m2rig_fbx PRIVATE /WX)
        endif()
    endif()
endif()

# ----------------------------------------------------- native cgltf (Wave 29a) --
# Pinned commit verified via GitHub API (jkuhlmann/cgltf master HEAD,
# GPG-signed merge #281, KHR_meshopt_compression parsing support). Single
# header (cgltf.h) at the repo root; compiled once in cgltf_impl.c so the
# adapter sees declarations only. Core stays clean: only m2rig_gltf links
# it. NOTE (2026-09-23): CVE-2026-75148 affects cgltf_validate() through
# v1.15 with no upstream fix at this pin — the adapter never calls
# cgltf_validate()/unpack helpers and enforces its own overflow-checked
# byte-range validation on every accessor read (see src/gltf/gltf_reader.cpp).
set(M2RIG_CGLTF_REV "85cd62382dfea638278962690cf515023f33ed00"
    CACHE STRING "Pinned cgltf commit")
if(M2RIG_WITH_CGLTF AND (M2RIG_WITH_UI OR M2RIG_WITH_CLI OR M2RIG_WITH_TESTS))
    include(FetchContent)
    FetchContent_Declare(
        cgltf
        URL "https://github.com/jkuhlmann/cgltf/archive/${M2RIG_CGLTF_REV}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_GetProperties(cgltf)
    if(NOT cgltf_POPULATED)
        FetchContent_Populate(cgltf)
    endif()
    add_library(cgltf_lib STATIC src/gltf/cgltf_impl.c)
    target_include_directories(cgltf_lib PUBLIC ${cgltf_SOURCE_DIR})
    if(MSVC)
        target_compile_options(cgltf_lib PRIVATE /W3 /WX-)
    endif()
        add_library(m2rig_gltf STATIC src/gltf/gltf_reader.cpp)
        target_include_directories(m2rig_gltf PUBLIC include)
        target_link_libraries(m2rig_gltf PUBLIC m2rig_core cgltf_lib)
    if(MSVC)
        target_compile_options(m2rig_gltf PRIVATE /W4)
        if(M2RIG_WARNINGS_AS_ERRORS)
            target_compile_options(m2rig_gltf PRIVATE /WX)
        endif()
    endif()
endif()

# ---------------------------------------------------------------- tests --
if(M2RIG_WITH_TESTS)
    enable_testing()
    add_executable(m2rig_tests
        tests/test_main.cpp
        tests/test_math.cpp
        tests/test_core.cpp
        tests/test_json.cpp
        tests/test_smd.cpp
        tests/test_weights.cpp
        tests/test_dds.cpp
        tests/test_mse.cpp
        tests/test_gltf.cpp
        tests/test_render.cpp
    )
    target_include_directories(m2rig_tests PRIVATE include tests)
    target_link_libraries(m2rig_tests PRIVATE m2rig_core)
    if(WIN32)
        # Headless render smoke test needs the D3D11 backend TU.
        target_sources(m2rig_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/renderer.cpp)
        target_link_libraries(m2rig_tests PRIVATE d3d11 d3dcompiler dxgi)
    endif()
    if(M2RIG_WITH_OPENFBX)
        target_link_libraries(m2rig_tests PRIVATE m2rig_fbx)
        target_compile_definitions(m2rig_tests PRIVATE M2RIG_WITH_OPENFBX=1)
    endif()
    if(M2RIG_WITH_CGLTF)
        target_link_libraries(m2rig_tests PRIVATE m2rig_gltf)
        target_compile_definitions(m2rig_tests PRIVATE M2RIG_WITH_CGLTF=1)
    endif()
    if(MSVC)
        target_compile_options(m2rig_tests PRIVATE /W4)
        if(M2RIG_WARNINGS_AS_ERRORS)
            target_compile_options(m2rig_tests PRIVATE /WX)
        endif()
        target_link_options(m2rig_tests PRIVATE /OPT:NOREF)
    endif()
    if(M2RIG_HEAP_TRIAGE)
        target_compile_definitions(m2rig_tests PRIVATE M2RIG_HEAP_TRIAGE=1)
    endif()
    add_test(NAME m2rig_tests COMMAND m2rig_tests)
endif()

# ------------------------------------------------------------------- CLI --
if(M2RIG_WITH_CLI)
    add_executable(m2rig_cli tools/cli/main.cpp)
    target_include_directories(m2rig_cli PRIVATE include)
    target_link_libraries(m2rig_cli PRIVATE m2rig_core)
    if(M2RIG_WITH_OPENFBX)
        target_link_libraries(m2rig_cli PRIVATE m2rig_fbx)
        target_compile_definitions(m2rig_cli PRIVATE M2RIG_WITH_OPENFBX=1)
    endif()
    if(M2RIG_WITH_CGLTF)
        target_link_libraries(m2rig_cli PRIVATE m2rig_gltf)
        target_compile_definitions(m2rig_cli PRIVATE M2RIG_WITH_CGLTF=1)
    endif()
    target_compile_definitions(m2rig_cli PRIVATE M2RIG_VERSION="${PROJECT_VERSION}")
    if(MSVC)
        target_compile_options(m2rig_cli PRIVATE /W4)
        if(M2RIG_WARNINGS_AS_ERRORS)
            target_compile_options(m2rig_cli PRIVATE /WX)
        endif()
    endif()
    if(M2RIG_WITH_TESTS)
        add_test(NAME cli-validate
                 COMMAND m2rig_cli validate ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd)
        add_test(NAME cli-validate-msm
                 COMMAND m2rig_cli validate-msm ${CMAKE_SOURCE_DIR}/tests/data/sample.msm)
        add_test(NAME cli-validate-mse
                 COMMAND m2rig_cli validate-mse ${CMAKE_SOURCE_DIR}/tests/data/sample.mse)
        add_test(NAME cli-info
                 COMMAND m2rig_cli info ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd)
        add_test(NAME cli-smd2smd
                 COMMAND m2rig_cli smd2smd ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd
                         ${CMAKE_CURRENT_BINARY_DIR}/cli_roundtrip.smd)
        add_test(NAME cli-smd2msm
                 COMMAND m2rig_cli smd2msm ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd
                         ${CMAKE_CURRENT_BINARY_DIR}/cli_shape.msm)
        add_test(NAME cli-autorig
                 COMMAND m2rig_cli autorig ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd
                         ${CMAKE_CURRENT_BINARY_DIR}/cli_autorig.smd)
        add_test(NAME cli-lod
                 COMMAND m2rig_cli lod ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd
                         ${CMAKE_CURRENT_BINARY_DIR}/cli_lod.smd --ratio 0.5)
        # Self-learning verbs over the fixture (exit-code gate, same pattern
        # as cli-autorig). learn-from-gr2-dir / analyze-gr2-dir stay unwired:
        # they need Data/Models, absent in clean checkouts. fbx2smd stays
        # build-only: no FBX fixture exists and none can be hand-written.
        # gltf2smd stays build-only too: no .glb fixture is checked in
        # (unit tests in tests/test_gltf.cpp synthesize minimal .glb files
        # at runtime instead).
        add_test(NAME cli-learn-from-asset
                 COMMAND m2rig_cli learn-from-asset ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd
                         ${CMAKE_CURRENT_BINARY_DIR}/cli_learn.m2learn)
        add_test(NAME cli-self-learn-autorig
                 COMMAND m2rig_cli self-learn-autorig ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd
                         ${CMAKE_CURRENT_BINARY_DIR}/cli_self_autorig.smd)
        add_test(NAME cli-self-learn-transfer
                 COMMAND m2rig_cli self-learn-transfer ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd
                         ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd
                         ${CMAKE_CURRENT_BINARY_DIR}/cli_self_xfer.smd)
        add_test(NAME cli-orient-two-bone
                 COMMAND m2rig_cli orient ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd)
        # Negative-path pins: a nonexistent input must fail with exit 2
        # (IO/parse, tools/cli/main.cpp), never 0 or a crash. WILL_FAIL
        # inverts the designed non-zero exit into a ctest pass.
        add_test(NAME cli-gr22smd-missing
                 COMMAND m2rig_cli gr22smd ${CMAKE_SOURCE_DIR}/tests/data/does-not-exist.gr2
                         ${CMAKE_CURRENT_BINARY_DIR}/cli_missing.smd)
        set_tests_properties(cli-gr22smd-missing PROPERTIES WILL_FAIL TRUE)
        # fbx2smd only exists in M2RIG_WITH_OPENFBX builds (the dispatch is
        # #ifdef-gated in tools/cli/main.cpp; without it the verb falls
        # through to usage/exit 1, so the suite is not registered there).
        if(M2RIG_WITH_OPENFBX)
            add_test(NAME cli-fbx2smd-missing
                     COMMAND m2rig_cli fbx2smd ${CMAKE_SOURCE_DIR}/tests/data/does-not-exist.fbx
                             ${CMAKE_CURRENT_BINARY_DIR}/cli_missing_fbx.smd)
            set_tests_properties(cli-fbx2smd-missing PROPERTIES WILL_FAIL TRUE)
        endif()
    endif()
endif()

# ------------------------------------------------------------------- UI --
if(M2RIG_WITH_UI)
    include(FetchContent)
    # Pinned Dear ImGui docking-branch commit (docking is not in master tags).
    # Reason: dockable professional panels (spec 36-37). License: MIT.
    # Update deliberately: bump M2RIG_IMGUI_REV + rebuild + note in docs/DEPENDENCIES.md.
    set(M2RIG_IMGUI_REV "367b2c24f399988ddafc0bb4628da0106bcc09be" CACHE STRING "Pinned Dear ImGui docking commit")
    FetchContent_Declare(
        imgui
        URL "https://github.com/ocornut/imgui/archive/${M2RIG_IMGUI_REV}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    if(MSVC)
        # Third-party code: warnings must not break the build.
        target_compile_options(imgui_lib PRIVATE /W3 /WX-)
    endif()

    # ImGuizmo 3D manipulators (translate/rotate bones in the viewport).
    # Pinned commit verified via GitHub API (master HEAD, SetRect fix).
    # Layout note: sources live under src/ since 2024; include both dirs.
    set(M2RIG_IMGUIZMO_REV "18cef5e031d8c6973d80284c67f60549fafd78c1"
        CACHE STRING "Pinned ImGuizmo commit")
    if(M2RIG_WITH_GIZMO)
        FetchContent_Declare(
            imguizmo
            URL "https://github.com/CedricGuillemet/ImGuizmo/archive/${M2RIG_IMGUIZMO_REV}.tar.gz"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        # Populate only: never configure the upstream CMakeLists (it builds
        # editors/sequencers we don't ship). We compile ImGuizmo.cpp alone.
        FetchContent_GetProperties(imguizmo)
        if(NOT imguizmo_POPULATED)
            FetchContent_Populate(imguizmo)
        endif()
        add_library(imguizmo_lib STATIC ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
        target_include_directories(imguizmo_lib PUBLIC
            ${imguizmo_SOURCE_DIR}/src
            ${imguizmo_SOURCE_DIR}
            ${imgui_SOURCE_DIR}
        )
        if(MSVC)
            target_compile_options(imguizmo_lib PRIVATE /W3 /WX-)
        endif()
    endif()

    add_executable(Metin2RiggingStudio WIN32
        src/renderer.cpp
        src/app/main.cpp
        src/app/app_gpu.cpp
        src/app/panels.cpp
        src/app/file_dialog.cpp
    )
    target_include_directories(Metin2RiggingStudio PRIVATE include)
    target_link_libraries(Metin2RiggingStudio PRIVATE
        m2rig_core imgui_lib d3d11 d3dcompiler dxgi dwmapi comdlg32)
    if(M2RIG_WITH_GIZMO)
        target_link_libraries(Metin2RiggingStudio PRIVATE imguizmo_lib)
        target_compile_definitions(Metin2RiggingStudio PRIVATE M2RIG_WITH_GIZMO=1)
    endif()
    target_compile_definitions(Metin2RiggingStudio PRIVATE
        M2RIG_VERSION="${PROJECT_VERSION}")
    if(M2RIG_WITH_OPENFBX)
        target_link_libraries(Metin2RiggingStudio PRIVATE m2rig_fbx)
        target_compile_definitions(Metin2RiggingStudio PRIVATE M2RIG_WITH_OPENFBX=1)
    endif()
    if(MSVC)
        target_compile_options(Metin2RiggingStudio PRIVATE /W4)
        if(M2RIG_WARNINGS_AS_ERRORS)
            target_compile_options(Metin2RiggingStudio PRIVATE /WX)
        endif()
        set_target_properties(Metin2RiggingStudio PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")
    endif()
endif()
