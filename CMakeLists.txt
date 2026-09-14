cmake_minimum_required(VERSION 3.21)

project(Metin2RiggingStudio
    VERSION 0.9.0
    DESCRIPTION "Metin2-only rigging, skinning, weight-transfer and armor authoring studio"
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(M2RIG_WITH_UI "Build the Dear ImGui + DirectX 11 desktop application" ON)
option(M2RIG_WITH_TESTS "Build and register CTest suites" ON)
option(M2RIG_WITH_CLI "Build the headless batch tool (core only)" ON)
option(M2RIG_WITH_GIZMO "Build with ImGuizmo bone manipulators (needs network at configure)" ON)
option(M2RIG_WARNINGS_AS_ERRORS "Treat MSVC warnings as errors for first-party code" ON)

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /utf-8 /FS)
endif()

# ---------------------------------------------------------------- core ---
add_library(m2rig_core STATIC
    src/logging.cpp
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
    src/ast/msm_ast.cpp
    src/adapters/gr2_adapter.cpp
    src/adapters/bridge_process.cpp
    src/extractors/universal_weight_extractor.cpp
    src/workspace/project_file.cpp
)
target_include_directories(m2rig_core PUBLIC include)
if(MSVC)
    target_compile_options(m2rig_core PRIVATE /W4)
    if(M2RIG_WARNINGS_AS_ERRORS)
        target_compile_options(m2rig_core PRIVATE /WX)
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
    )
    target_include_directories(m2rig_tests PRIVATE include tests)
    target_link_libraries(m2rig_tests PRIVATE m2rig_core)
    if(MSVC)
        target_compile_options(m2rig_tests PRIVATE /W4)
    endif()
    add_test(NAME m2rig_tests COMMAND m2rig_tests)
endif()

# ------------------------------------------------------------------- CLI --
if(M2RIG_WITH_CLI)
    add_executable(m2rig_cli tools/cli/main.cpp)
    target_include_directories(m2rig_cli PRIVATE include)
    target_link_libraries(m2rig_cli PRIVATE m2rig_core)
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
        add_test(NAME cli-info
                 COMMAND m2rig_cli info ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd)
        add_test(NAME cli-smd2smd
                 COMMAND m2rig_cli smd2smd ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd
                         ${CMAKE_CURRENT_BINARY_DIR}/cli_roundtrip.smd)
        add_test(NAME cli-smd2msm
                 COMMAND m2rig_cli smd2msm ${CMAKE_SOURCE_DIR}/tests/data/two_bone.smd
                         ${CMAKE_CURRENT_BINARY_DIR}/cli_shape.msm)
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
    if(MSVC)
        target_compile_options(Metin2RiggingStudio PRIVATE /W4)
        if(M2RIG_WARNINGS_AS_ERRORS)
            target_compile_options(Metin2RiggingStudio PRIVATE /WX)
        endif()
        set_target_properties(Metin2RiggingStudio PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$(OutDir)")
    endif()
endif()
