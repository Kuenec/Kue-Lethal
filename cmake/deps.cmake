include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(JSON_SystemInclude OFF CACHE BOOL "" FORCE)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/archive/9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03.tar.gz
  URL_HASH SHA256=0dbc5e40a01ff142e7e68c03e85247a4dcede2f592d12d3677dee3664d17975a
)
FetchContent_MakeAvailable(json)

if(KUE_BUILD_MODULE)
  FetchContent_Declare(imgui_source
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG f5befd2d29e66809cd1110a152e375a7f1981f06
    GIT_SHALLOW FALSE
  )
  FetchContent_MakeAvailable(imgui_source)
endif()

if(KUE_BUILD_MODULE AND WIN32)
  FetchContent_Declare(capstone_source
    URL https://github.com/capstone-engine/capstone/archive/refs/tags/5.0.9.tar.gz
    URL_HASH SHA256=0619da31af08152600af95c481527ef6d756c0a8404fca7544a4fdf6dfc2c0f9
  )
  FetchContent_Populate(capstone_source)
endif()
