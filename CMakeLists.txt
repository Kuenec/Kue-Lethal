cmake_minimum_required(VERSION 3.24)
project(KueLethal VERSION 1.0.0 LANGUAGES CXX)

include(CheckIPOSupported)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(KUE_FETCH_DEPS "Fetch pinned dependencies from their upstream repositories" ON)
option(KUE_BUILD_MODULE "Build the production injectable module" ON)
set(KUE_JSON_SOURCE_DIR "" CACHE PATH "nlohmann/json 3.11.3 source directory used when fetching is disabled")
if(KUE_BUILD_MODULE)
  set(KUE_IMGUI_SOURCE_DIR "" CACHE PATH "Dear ImGui 1.91.9b source directory used when fetching is disabled")
  set(KUE_GAME_MANAGED_DIR "" CACHE PATH "Lethal Company Managed assembly directory")
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  message(FATAL_ERROR "Kue Lethal supports Linux x86-64 only")
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
  message(FATAL_ERROR "Kue Lethal supports GCC and Clang only")
endif()

set(KUE_STRICT_COMPILE_OPTIONS
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wsign-conversion
  -Wshadow
  -Wcast-align
  -Wcast-qual
  -Wformat=2
  -Wundef
  -Wnull-dereference
  -Wdouble-promotion
  -Wnon-virtual-dtor
  -Woverloaded-virtual
  -Wold-style-cast
  -Wimplicit-fallthrough
  -Wswitch-enum
  -Werror
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND KUE_STRICT_COMPILE_OPTIONS
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
  )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND KUE_STRICT_COMPILE_OPTIONS -Wextra-semi)
endif()

set(KUE_PRODUCTION_SOURCES
  src/core/Config.cpp
  src/core/Log.cpp
  src/entry/module.cpp
  src/game/LethalState.cpp
  src/game/RuntimeCatalog.cpp
  src/mono/PeExport.cpp
  src/mono/Runtime.cpp
  src/mono/UnityMetadata.cpp
  src/main.cpp
  src/overlay/InternalHud.cpp
  src/overlay/InternalHudRenderer.cpp
  src/overlay/Menu.cpp
  src/overlay/Theme.cpp
)
set(KUE_PRODUCTION_HEADERS
  src/core/Config.h
  src/core/Log.h
  src/core/Utf8.h
  src/game/Game.h
  src/game/LethalState.h
  src/game/PlayerActions.h
  src/game/PlayerSnapshot.h
  src/game/RuntimeCatalog.h
  src/mono/PeExport.h
  src/mono/Runtime.h
  src/mono/UnityMetadata.h
  src/main.h
  src/overlay/InternalHud.h
  src/overlay/InternalHudRenderer.h
  src/overlay/Menu.h
  src/overlay/Theme.h
)
set(KUE_BUILD_IDENTITY_PROJECT_INPUTS
  CMakeLists.txt
  cmake/BuildIdentity.cmake
  cmake/BuildIdentity.cpp.in
  cmake/deps.cmake
  config/kuelethal.json
  scripts/entry-validation.sh
  scripts/build-managed.sh
  src/managed/KueInternalHud.cs
  ${KUE_PRODUCTION_SOURCES}
  ${KUE_PRODUCTION_HEADERS}
)
include(cmake/BuildIdentity.cmake)
set(KUE_BUILD_IDENTITY_DEPENDENCY_LABELS
  tool/cmake
  tool/cxx-compiler
  tool/cxx-compiler-archiver
  tool/cxx-compiler-ranlib
  tool/linker
  tool/archiver
  tool/ranlib
)
set(KUE_BUILD_IDENTITY_DEPENDENCY_PATHS
  "${CMAKE_COMMAND}"
  "${CMAKE_CXX_COMPILER}"
  "${CMAKE_CXX_COMPILER_AR}"
  "${CMAKE_CXX_COMPILER_RANLIB}"
  "${CMAKE_LINKER}"
  "${CMAKE_AR}"
  "${CMAKE_RANLIB}"
)

message(STATUS "Kue Lethal production module: ${KUE_BUILD_MODULE}")
if(BUILD_TESTING)
  find_program(KUE_BASH_EXECUTABLE bash REQUIRED)

  if(KUE_BUILD_MODULE)
    message(STATUS "Kue Lethal test graph: full native guards")
  else()
    message(STATUS "Kue Lethal test graph: dependency-independent native guards")
  endif()
endif()

if(KUE_BUILD_MODULE OR BUILD_TESTING)
  find_package(Threads REQUIRED)
  check_ipo_supported(RESULT KUE_IPO_SUPPORTED OUTPUT KUE_IPO_ERROR LANGUAGES CXX)
  if(NOT KUE_IPO_SUPPORTED)
    message(FATAL_ERROR "C++ interprocedural optimization is required: ${KUE_IPO_ERROR}")
  endif()
endif()

if(KUE_BUILD_MODULE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(CAPSTONE REQUIRED IMPORTED_TARGET capstone>=5.0 capstone<6.0)
  find_program(KUE_WINE_EXECUTABLE wine REQUIRED)
  find_program(KUE_WINEPATH_EXECUTABLE winepath REQUIRED)

  set(KUE_CSC "$ENV{KUE_CSC}" CACHE FILEPATH "Wine Mono C# compiler")
  if(NOT KUE_CSC)
    file(GLOB KUE_CSC_CANDIDATES LIST_DIRECTORIES FALSE
      "/usr/share/wine/mono/*/lib/mono/4.5/csc.exe"
    )
    list(SORT KUE_CSC_CANDIDATES)
    list(LENGTH KUE_CSC_CANDIDATES KUE_CSC_CANDIDATE_COUNT)
    if(KUE_CSC_CANDIDATE_COUNT EQUAL 1)
      list(GET KUE_CSC_CANDIDATES 0 KUE_CSC)
      set(KUE_CSC "${KUE_CSC}" CACHE FILEPATH "Wine Mono C# compiler" FORCE)
    elseif(KUE_CSC_CANDIDATE_COUNT GREATER 1)
      message(FATAL_ERROR
        "Multiple Wine Mono C# compilers were found; set KUE_CSC to the intended csc.exe"
      )
    else()
      message(FATAL_ERROR
        "Wine Mono C# compiler was not found; install Wine Mono or set KUE_CSC to its csc.exe"
      )
    endif()
  endif()
  get_filename_component(KUE_CSC "${KUE_CSC}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
  if(NOT EXISTS "${KUE_CSC}" OR IS_DIRECTORY "${KUE_CSC}")
    message(FATAL_ERROR "KUE_CSC does not name a Wine Mono C# compiler file: ${KUE_CSC}")
  endif()
  set(KUE_CSC "${KUE_CSC}" CACHE FILEPATH "Wine Mono C# compiler" FORCE)

  list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_LABELS
    tool/wine
    tool/winepath
    tool/wine-mono-csc
  )
  list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_PATHS
    "${KUE_WINE_EXECUTABLE}"
    "${KUE_WINEPATH_EXECUTABLE}"
    "${KUE_CSC}"
  )
endif()

if(KUE_BUILD_MODULE OR BUILD_TESTING)
  if(KUE_FETCH_DEPS)
    include(cmake/deps.cmake)
  else()
    if(KUE_JSON_SOURCE_DIR)
      if(NOT EXISTS "${KUE_JSON_SOURCE_DIR}/include/nlohmann/json.hpp" OR
         NOT EXISTS "${KUE_JSON_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "KUE_JSON_SOURCE_DIR does not contain a complete nlohmann/json source tree")
      endif()
      file(STRINGS "${KUE_JSON_SOURCE_DIR}/CMakeLists.txt" KUE_JSON_PROJECT_LINE
           REGEX "^project\\(nlohmann_json VERSION ")
      if(NOT KUE_JSON_PROJECT_LINE MATCHES "VERSION 3\\.11\\.3 LANGUAGES CXX\\)")
        message(FATAL_ERROR "nlohmann/json source must be exactly version 3.11.3")
      endif()
      set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
      set(JSON_Install OFF CACHE BOOL "" FORCE)
      set(JSON_SystemInclude OFF CACHE BOOL "" FORCE)
      add_subdirectory("${KUE_JSON_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/json")
    else()
      find_package(nlohmann_json 3.11.3 EXACT CONFIG REQUIRED)
    endif()
  endif()
endif()

if(KUE_BUILD_MODULE AND NOT KUE_FETCH_DEPS)
  if(NOT KUE_IMGUI_SOURCE_DIR)
    message(FATAL_ERROR
      "KUE_FETCH_DEPS=OFF requires KUE_IMGUI_SOURCE_DIR to name a Dear ImGui 1.91.9b source tree"
    )
  endif()
  set(imgui_source_SOURCE_DIR "${KUE_IMGUI_SOURCE_DIR}")
endif()

if(KUE_BUILD_MODULE)
  foreach(KUE_IMGUI_FILE imgui.h imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp)
    if(NOT EXISTS "${imgui_source_SOURCE_DIR}/${KUE_IMGUI_FILE}")
      message(FATAL_ERROR
        "Dear ImGui source is incomplete: ${imgui_source_SOURCE_DIR}/${KUE_IMGUI_FILE} is missing"
      )
    endif()
  endforeach()
  file(STRINGS "${imgui_source_SOURCE_DIR}/imgui.h" KUE_IMGUI_VERSION_LINE
       REGEX "^#define IMGUI_VERSION ")
  if(NOT KUE_IMGUI_VERSION_LINE MATCHES "\"1\\.91\\.9b\"")
    message(FATAL_ERROR "Dear ImGui source must be exactly version 1.91.9b")
  endif()
  set(KUE_IMGUI_BUILD_INPUTS
    imconfig.h
    imgui.cpp
    imgui.h
    imgui_draw.cpp
    imgui_internal.h
    imgui_tables.cpp
    imgui_widgets.cpp
    imstb_rectpack.h
    imstb_textedit.h
    imstb_truetype.h
  )
  foreach(KUE_IMGUI_BUILD_INPUT IN LISTS KUE_IMGUI_BUILD_INPUTS)
    list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_LABELS "imgui/${KUE_IMGUI_BUILD_INPUT}")
    list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_PATHS
      "${imgui_source_SOURCE_DIR}/${KUE_IMGUI_BUILD_INPUT}"
    )
  endforeach()

  add_library(imgui STATIC
    "${imgui_source_SOURCE_DIR}/imgui.cpp"
    "${imgui_source_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_source_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_source_SOURCE_DIR}/imgui_widgets.cpp"
  )
  target_include_directories(imgui PUBLIC "${imgui_source_SOURCE_DIR}")
  target_compile_options(imgui PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})

  add_library(kuelethal MODULE ${KUE_PRODUCTION_SOURCES})
  target_include_directories(kuelethal PRIVATE src)
  target_compile_options(kuelethal PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  target_link_libraries(kuelethal PRIVATE
    imgui
    nlohmann_json::nlohmann_json
    PkgConfig::CAPSTONE
    Threads::Threads
    ${CMAKE_DL_LIBS}
  )
  set_target_properties(kuelethal PROPERTIES
    PREFIX ""
    OUTPUT_NAME "kuelethal"
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  set_property(TARGET kuelethal PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

  if(NOT KUE_GAME_MANAGED_DIR)
    set(KUE_GAME_MANAGED_CANDIDATES)
    if(DEFINED ENV{KUE_GAME_MANAGED_DIR})
      list(APPEND KUE_GAME_MANAGED_CANDIDATES "$ENV{KUE_GAME_MANAGED_DIR}")
    endif()
    if(DEFINED ENV{KUE_STEAM_COMMON})
      list(APPEND KUE_GAME_MANAGED_CANDIDATES
        "$ENV{KUE_STEAM_COMMON}/Lethal Company/Lethal Company_Data/Managed"
      )
    endif()
    if(DEFINED ENV{XDG_DATA_HOME})
      list(APPEND KUE_GAME_MANAGED_CANDIDATES
        "$ENV{XDG_DATA_HOME}/Steam/steamapps/common/Lethal Company/Lethal Company_Data/Managed"
      )
    endif()
    if(DEFINED ENV{HOME})
      list(APPEND KUE_GAME_MANAGED_CANDIDATES
        "$ENV{HOME}/.local/share/Steam/steamapps/common/Lethal Company/Lethal Company_Data/Managed"
        "$ENV{HOME}/.steam/steam/steamapps/common/Lethal Company/Lethal Company_Data/Managed"
        "$ENV{HOME}/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/common/Lethal Company/Lethal Company_Data/Managed"
      )
    endif()
    foreach(KUE_GAME_MANAGED_CANDIDATE IN LISTS KUE_GAME_MANAGED_CANDIDATES)
      if(EXISTS "${KUE_GAME_MANAGED_CANDIDATE}/Assembly-CSharp.dll")
        set(KUE_GAME_MANAGED_DIR "${KUE_GAME_MANAGED_CANDIDATE}" CACHE PATH
            "Lethal Company Managed assembly directory" FORCE)
        break()
      endif()
    endforeach()
  endif()

  set(KUE_MANAGED_REFERENCES
    mscorlib.dll
    netstandard.dll
    System.dll
    Assembly-CSharp.dll
    Unity.Netcode.Runtime.dll
    UnityEngine.dll
    UnityEngine.CoreModule.dll
    UnityEngine.IMGUIModule.dll
    UnityEngine.AIModule.dll
    UnityEngine.InputLegacyModule.dll
    UnityEngine.PhysicsModule.dll
    UnityEngine.TextRenderingModule.dll
    Unity.InputSystem.dll
    UnityEngine.UI.dll
  )
  if(NOT KUE_GAME_MANAGED_DIR)
    message(FATAL_ERROR
      "Lethal Company assemblies were not found; set KUE_GAME_MANAGED_DIR to Lethal Company_Data/Managed"
    )
  endif()
  set(KUE_MANAGED_REFERENCE_PATHS)
  foreach(KUE_MANAGED_REFERENCE IN LISTS KUE_MANAGED_REFERENCES)
    set(KUE_MANAGED_REFERENCE_PATH "${KUE_GAME_MANAGED_DIR}/${KUE_MANAGED_REFERENCE}")
    if(NOT EXISTS "${KUE_MANAGED_REFERENCE_PATH}")
      message(FATAL_ERROR "Required managed assembly is missing: ${KUE_MANAGED_REFERENCE_PATH}")
    endif()
    list(APPEND KUE_MANAGED_REFERENCE_PATHS "${KUE_MANAGED_REFERENCE_PATH}")
    list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_LABELS
      "managed-reference/${KUE_MANAGED_REFERENCE}"
    )
    list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_PATHS "${KUE_MANAGED_REFERENCE_PATH}")
  endforeach()

  get_target_property(KUE_JSON_INCLUDE_ENTRIES
    nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES
  )
  set(KUE_JSON_INCLUDE_ROOT "")
  foreach(KUE_JSON_INCLUDE_ENTRY IN LISTS KUE_JSON_INCLUDE_ENTRIES)
    if(KUE_JSON_INCLUDE_ENTRY MATCHES "^\\$<BUILD_INTERFACE:(.*)>$")
      set(KUE_JSON_INCLUDE_CANDIDATE "${CMAKE_MATCH_1}")
    elseif(IS_ABSOLUTE "${KUE_JSON_INCLUDE_ENTRY}")
      set(KUE_JSON_INCLUDE_CANDIDATE "${KUE_JSON_INCLUDE_ENTRY}")
    else()
      continue()
    endif()
    if(EXISTS "${KUE_JSON_INCLUDE_CANDIDATE}/nlohmann/json.hpp")
      set(KUE_JSON_INCLUDE_ROOT "${KUE_JSON_INCLUDE_CANDIDATE}/nlohmann")
      break()
    endif()
  endforeach()
  if(NOT KUE_JSON_INCLUDE_ROOT)
    message(FATAL_ERROR "cannot resolve the nlohmann/json 3.11.3 build input tree")
  endif()
  set(KUE_JSON_BUILD_INPUTS
    adl_serializer.hpp
    byte_container_with_subtype.hpp
    detail/abi_macros.hpp
    detail/conversions/from_json.hpp
    detail/conversions/to_chars.hpp
    detail/conversions/to_json.hpp
    detail/exceptions.hpp
    detail/hash.hpp
    detail/input/binary_reader.hpp
    detail/input/input_adapters.hpp
    detail/input/json_sax.hpp
    detail/input/lexer.hpp
    detail/input/parser.hpp
    detail/input/position_t.hpp
    detail/iterators/internal_iterator.hpp
    detail/iterators/iter_impl.hpp
    detail/iterators/iteration_proxy.hpp
    detail/iterators/iterator_traits.hpp
    detail/iterators/json_reverse_iterator.hpp
    detail/iterators/primitive_iterator.hpp
    detail/json_custom_base_class.hpp
    detail/json_pointer.hpp
    detail/json_ref.hpp
    detail/macro_scope.hpp
    detail/macro_unscope.hpp
    detail/meta/call_std/begin.hpp
    detail/meta/call_std/end.hpp
    detail/meta/cpp_future.hpp
    detail/meta/detected.hpp
    detail/meta/identity_tag.hpp
    detail/meta/is_sax.hpp
    detail/meta/std_fs.hpp
    detail/meta/type_traits.hpp
    detail/meta/void_t.hpp
    detail/output/binary_writer.hpp
    detail/output/output_adapters.hpp
    detail/output/serializer.hpp
    detail/string_concat.hpp
    detail/string_escape.hpp
    detail/value_t.hpp
    json.hpp
    json_fwd.hpp
    ordered_map.hpp
    thirdparty/hedley/hedley.hpp
    thirdparty/hedley/hedley_undef.hpp
  )
  foreach(KUE_JSON_BUILD_INPUT IN LISTS KUE_JSON_BUILD_INPUTS)
    list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_LABELS "nlohmann/${KUE_JSON_BUILD_INPUT}")
    list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_PATHS
      "${KUE_JSON_INCLUDE_ROOT}/${KUE_JSON_BUILD_INPUT}"
    )
  endforeach()

  find_path(KUE_CAPSTONE_HEADER_ROOT capstone/capstone.h REQUIRED)
  set(KUE_CAPSTONE_BUILD_HEADERS
    arm.h
    arm64.h
    bpf.h
    capstone.h
    evm.h
    m680x.h
    m68k.h
    mips.h
    mos65xx.h
    platform.h
    ppc.h
    riscv.h
    sh.h
    sparc.h
    systemz.h
    tms320c64x.h
    tricore.h
    wasm.h
    x86.h
    xcore.h
  )
  foreach(KUE_CAPSTONE_BUILD_HEADER IN LISTS KUE_CAPSTONE_BUILD_HEADERS)
    list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_LABELS
      "capstone/include/${KUE_CAPSTONE_BUILD_HEADER}"
    )
    list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_PATHS
      "${KUE_CAPSTONE_HEADER_ROOT}/capstone/${KUE_CAPSTONE_BUILD_HEADER}"
    )
  endforeach()
  find_library(KUE_CAPSTONE_LIBRARY_FILE NAMES capstone HINTS ${CAPSTONE_LIBRARY_DIRS} REQUIRED)
  list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_LABELS capstone/library)
  list(APPEND KUE_BUILD_IDENTITY_DEPENDENCY_PATHS "${KUE_CAPSTONE_LIBRARY_FILE}")

  set(KUE_MANAGED_SOURCE "${CMAKE_SOURCE_DIR}/src/managed/KueInternalHud.cs")
  set(KUE_MANAGED_HUD "${CMAKE_BINARY_DIR}/managed/KueInternalHud.dll")
  get_filename_component(KUE_WINE_DIRECTORY "${KUE_WINE_EXECUTABLE}" DIRECTORY)
  get_filename_component(KUE_WINEPATH_DIRECTORY "${KUE_WINEPATH_EXECUTABLE}" DIRECTORY)
  set(KUE_MANAGED_TOOL_DIRECTORIES "${KUE_WINE_DIRECTORY}" "${KUE_WINEPATH_DIRECTORY}")
  list(REMOVE_DUPLICATES KUE_MANAGED_TOOL_DIRECTORIES)
  list(JOIN KUE_MANAGED_TOOL_DIRECTORIES ":" KUE_MANAGED_TOOL_PATH)
  string(APPEND KUE_MANAGED_TOOL_PATH ":$ENV{PATH}")
  add_custom_command(
    OUTPUT "${KUE_MANAGED_HUD}"
    COMMAND "${CMAKE_COMMAND}" -E env
            "KUE_CSC=${KUE_CSC}"
            "KUE_GAME_MANAGED_DIR=${KUE_GAME_MANAGED_DIR}"
            "PATH=${KUE_MANAGED_TOOL_PATH}"
            "${CMAKE_SOURCE_DIR}/scripts/build-managed.sh" "${KUE_MANAGED_HUD}"
    DEPENDS
      "${KUE_MANAGED_SOURCE}"
      "${CMAKE_SOURCE_DIR}/scripts/build-managed.sh"
      "${KUE_CSC}"
      "${KUE_WINE_EXECUTABLE}"
      "${KUE_WINEPATH_EXECUTABLE}"
      ${KUE_MANAGED_REFERENCE_PATHS}
    VERBATIM
  )
  add_custom_target(kue-managed-hud ALL DEPENDS "${KUE_MANAGED_HUD}")
  add_dependencies(kuelethal kue-managed-hud)
endif()

string(REPLACE ";" "," KUE_BUILD_IDENTITY_STRICT_OPTIONS
       "${KUE_STRICT_COMPILE_OPTIONS}")
set(KUE_BUILD_IDENTITY_CONFIGURATION "schema=1\n")
foreach(KUE_BUILD_IDENTITY_VARIABLE
        CMAKE_VERSION
        CMAKE_GENERATOR
        CMAKE_GENERATOR_PLATFORM
        CMAKE_GENERATOR_TOOLSET
        CMAKE_SYSTEM_NAME
        CMAKE_SYSTEM_PROCESSOR
        CMAKE_CXX_COMPILER_ID
        CMAKE_CXX_COMPILER_VERSION
        CMAKE_CXX_COMPILER_FRONTEND_VARIANT
        CMAKE_CXX_COMPILER_TARGET
        CMAKE_CXX_STANDARD
        CMAKE_CXX_EXTENSIONS
        CMAKE_POSITION_INDEPENDENT_CODE
        CMAKE_CXX_VISIBILITY_PRESET
        CMAKE_VISIBILITY_INLINES_HIDDEN
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_MINSIZEREL
        CMAKE_MODULE_LINKER_FLAGS
        CMAKE_MODULE_LINKER_FLAGS_DEBUG
        CMAKE_MODULE_LINKER_FLAGS_RELEASE
        CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO
        CMAKE_MODULE_LINKER_FLAGS_MINSIZEREL
        CMAKE_DL_LIBS
        CMAKE_THREAD_LIBS_INIT
        KUE_FETCH_DEPS
        KUE_BUILD_MODULE
        CAPSTONE_capstone_VERSION)
  string(REPLACE ";" "," KUE_BUILD_IDENTITY_VALUE
         "${${KUE_BUILD_IDENTITY_VARIABLE}}")
  string(APPEND KUE_BUILD_IDENTITY_CONFIGURATION
         "${KUE_BUILD_IDENTITY_VARIABLE}=${KUE_BUILD_IDENTITY_VALUE}\n")
endforeach()
string(APPEND KUE_BUILD_IDENTITY_CONFIGURATION
       "PROJECT_VERSION=${PROJECT_VERSION}\n"
       "KUE_STRICT_COMPILE_OPTIONS=${KUE_BUILD_IDENTITY_STRICT_OPTIONS}\n"
       "KUE_IPO_RELEASE=TRUE\n"
       "KUE_IMGUI_VERSION=1.91.9b\n"
       "KUE_JSON_VERSION=3.11.3\n")

kue_compute_build_identity(
  OUTPUT KUE_BUILD_INPUT_DIGEST
  SOURCE_ROOT "${CMAKE_SOURCE_DIR}"
  CONFIGURATION "${KUE_BUILD_IDENTITY_CONFIGURATION}"
  PROJECT_INPUTS ${KUE_BUILD_IDENTITY_PROJECT_INPUTS}
  DEPENDENCY_LABELS ${KUE_BUILD_IDENTITY_DEPENDENCY_LABELS}
  DEPENDENCY_PATHS ${KUE_BUILD_IDENTITY_DEPENDENCY_PATHS}
)
set(KUE_GENERATED_SOURCE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${KUE_GENERATED_SOURCE_DIRECTORY}")
configure_file(
  cmake/BuildIdentity.cpp.in
  "${KUE_GENERATED_SOURCE_DIRECTORY}/BuildIdentity.cpp"
  @ONLY
  NEWLINE_STYLE UNIX
)
add_library(kue-build-identity OBJECT
  "${KUE_GENERATED_SOURCE_DIRECTORY}/BuildIdentity.cpp"
)
target_include_directories(kue-build-identity PRIVATE src)
target_compile_options(kue-build-identity PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
target_compile_definitions(kue-build-identity PRIVATE
  "KUE_BUILD_IDENTITY=\"${KUE_BUILD_INPUT_DIGEST}-$<CONFIG>\""
)
set_target_properties(kue-build-identity PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
  NO_SYSTEM_FROM_IMPORTED TRUE
)
if(KUE_BUILD_MODULE)
  target_sources(kuelethal PRIVATE $<TARGET_OBJECTS:kue-build-identity>)
endif()

if(BUILD_TESTING)
  add_executable(kue-config-tests tests/ConfigTests.cpp src/core/Config.cpp)
  target_include_directories(kue-config-tests PRIVATE src)
  target_compile_options(kue-config-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  target_link_libraries(kue-config-tests PRIVATE nlohmann_json::nlohmann_json)
  set_target_properties(kue-config-tests PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(NAME kue-config COMMAND kue-config-tests)

  add_executable(kue-log-tests tests/LogTests.cpp src/core/Log.cpp)
  target_include_directories(kue-log-tests PRIVATE src)
  target_compile_options(kue-log-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  target_link_libraries(kue-log-tests PRIVATE Threads::Threads)
  set_target_properties(kue-log-tests PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(NAME kue-log COMMAND kue-log-tests)

  add_executable(kue-player-action-queue-tests tests/PlayerActionQueueTests.cpp)
  target_include_directories(kue-player-action-queue-tests PRIVATE src)
  target_compile_options(kue-player-action-queue-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  target_link_libraries(kue-player-action-queue-tests PRIVATE Threads::Threads)
  set_target_properties(kue-player-action-queue-tests PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(NAME kue-player-action-queue COMMAND kue-player-action-queue-tests)

  add_executable(kue-player-action-validation-tests tests/PlayerActionValidationTests.cpp)
  target_include_directories(kue-player-action-validation-tests PRIVATE src)
  target_compile_options(kue-player-action-validation-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  set_target_properties(kue-player-action-validation-tests PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(NAME kue-player-action-validation COMMAND kue-player-action-validation-tests)

  add_executable(kue-player-snapshot-tests tests/PlayerSnapshotTests.cpp)
  target_include_directories(kue-player-snapshot-tests PRIVATE src)
  target_compile_options(kue-player-snapshot-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  set_target_properties(kue-player-snapshot-tests PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(NAME kue-player-snapshot COMMAND kue-player-snapshot-tests)

  add_executable(kue-utf8-tests tests/Utf8Tests.cpp)
  target_include_directories(kue-utf8-tests PRIVATE src)
  target_compile_options(kue-utf8-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  set_target_properties(kue-utf8-tests PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(NAME kue-utf8 COMMAND kue-utf8-tests)

  add_executable(kue-lethal-state-lifecycle-tests
    tests/LethalStateLifecycleTests.cpp
    src/game/LethalState.cpp
  )
  target_include_directories(kue-lethal-state-lifecycle-tests PRIVATE src)
  target_compile_options(kue-lethal-state-lifecycle-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  target_link_libraries(kue-lethal-state-lifecycle-tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  set_target_properties(kue-lethal-state-lifecycle-tests PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(NAME kue-lethal-state-lifecycle COMMAND kue-lethal-state-lifecycle-tests)

  add_executable(kue-main-boot-tests tests/MainBootTests.cpp src/main.cpp)
  target_sources(kue-main-boot-tests PRIVATE $<TARGET_OBJECTS:kue-build-identity>)
  target_include_directories(kue-main-boot-tests PRIVATE src)
  target_compile_options(kue-main-boot-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  target_link_libraries(kue-main-boot-tests PRIVATE Threads::Threads)
  set_target_properties(kue-main-boot-tests PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(NAME kue-main-boot COMMAND kue-main-boot-tests)

  add_executable(kue-metadata-cache-tests
    tests/MetadataCacheTests.cpp
    src/mono/UnityMetadata.cpp
  )
  target_include_directories(kue-metadata-cache-tests PRIVATE src)
  target_compile_options(kue-metadata-cache-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  target_link_libraries(kue-metadata-cache-tests PRIVATE Threads::Threads)
  set_target_properties(kue-metadata-cache-tests PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(NAME kue-metadata-cache COMMAND kue-metadata-cache-tests)

  add_executable(kue-runtime-catalog-tests
    tests/RuntimeCatalogTests.cpp
    src/game/RuntimeCatalog.cpp
  )
  target_include_directories(kue-runtime-catalog-tests PRIVATE src)
  target_compile_options(kue-runtime-catalog-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  set_target_properties(kue-runtime-catalog-tests PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(NAME kue-runtime-catalog COMMAND kue-runtime-catalog-tests)

  add_library(kue-module-entry-fixture MODULE
    src/entry/module.cpp
    tests/ModuleEntryFixture.cpp
  )
  target_include_directories(kue-module-entry-fixture PRIVATE src)
  target_compile_options(kue-module-entry-fixture PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  set_target_properties(kue-module-entry-fixture PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
    PREFIX ""
  )

  add_executable(kue-module-entry-tests tests/ModuleEntryTests.cpp)
  target_include_directories(kue-module-entry-tests PRIVATE src)
  target_compile_options(kue-module-entry-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
  target_link_libraries(kue-module-entry-tests PRIVATE ${CMAKE_DL_LIBS})
  set_target_properties(kue-module-entry-tests PROPERTIES
    ENABLE_EXPORTS TRUE
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    NO_SYSTEM_FROM_IMPORTED TRUE
  )
  add_test(
    NAME kue-module-entry
    COMMAND kue-module-entry-tests $<TARGET_FILE:kue-module-entry-fixture>
  )

  add_test(
    NAME kue-scripts
    COMMAND "${CMAKE_COMMAND}" -E env "CXX=${CMAKE_CXX_COMPILER}"
            "${KUE_BASH_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/ScriptTests.sh"
  )
  add_test(
    NAME kue-build-identity
    COMMAND "${CMAKE_COMMAND}" -E env "CXX=${CMAKE_CXX_COMPILER}"
            "${KUE_BASH_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/ScriptTests.sh" build-identity
  )
  add_test(
    NAME kue-inject-environment
    COMMAND "${CMAKE_COMMAND}" -E env "CXX=${CMAKE_CXX_COMPILER}"
            "${KUE_BASH_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/ScriptTests.sh" inject-environment
  )
  set_tests_properties(kue-inject-environment PROPERTIES TIMEOUT 10)

  if(KUE_BUILD_MODULE)
    add_executable(kue-internal-hud-renderer-tests
      tests/InternalHudRendererTests.cpp
      src/core/Config.cpp
      src/game/RuntimeCatalog.cpp
      src/overlay/InternalHudRenderer.cpp
      src/overlay/Menu.cpp
      src/overlay/Theme.cpp
    )
    target_include_directories(kue-internal-hud-renderer-tests PRIVATE src)
    target_compile_options(kue-internal-hud-renderer-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
    target_link_libraries(kue-internal-hud-renderer-tests PRIVATE
      imgui
      nlohmann_json::nlohmann_json
      Threads::Threads
    )
    set_target_properties(kue-internal-hud-renderer-tests PROPERTIES
      INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
      NO_SYSTEM_FROM_IMPORTED TRUE
    )
    add_test(NAME kue-internal-hud-renderer COMMAND kue-internal-hud-renderer-tests)

    add_executable(kue-mono-runtime-tests
      tests/MonoRuntimeTests.cpp
      src/core/Log.cpp
      src/mono/Runtime.cpp
    )
    target_include_directories(kue-mono-runtime-tests PRIVATE src)
    target_compile_options(kue-mono-runtime-tests PRIVATE ${KUE_STRICT_COMPILE_OPTIONS})
    target_link_libraries(kue-mono-runtime-tests PRIVATE
      PkgConfig::CAPSTONE
      Threads::Threads
      ${CMAKE_DL_LIBS}
    )
    set_target_properties(kue-mono-runtime-tests PROPERTIES
      ENABLE_EXPORTS TRUE
      INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
      NO_SYSTEM_FROM_IMPORTED TRUE
    )
    add_test(NAME kue-mono-runtime COMMAND kue-mono-runtime-tests)
  endif()
endif()
