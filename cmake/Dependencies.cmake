# Third-party dependencies, pulled in reproducibly with FetchContent.
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# --- nlohmann/json -------------------------------------------------------
# Used by src/io/GeoJsonWriter.cpp to emit the GeoJSON FeatureCollection
# (Task-3). Header-only, so this costs almost nothing at build time.
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

# --- GeographicLib -------------------------------------------------------
# Task-8's shortest path is a true WGS84 geodesic, not a spherical great
# circle, so it uses Karney's algorithm as implemented by GeographicLib
# rather than a hand-rolled approximation.
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(BUILD_MANPAGES      OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES      OFF CACHE BOOL "" FORCE)
set(BUILD_NETGEOGRAPHICLIB OFF CACHE BOOL "" FORCE)

# EXCLUDE_FROM_ALL: GeographicLib always adds its command-line tools
# (GeodSolve, Planimeter, ...) with no option to turn them off. Excluding them
# from the default target means only the library we actually link gets built.
FetchContent_Declare(geographiclib
    GIT_REPOSITORY   https://github.com/geographiclib/geographiclib.git
    GIT_TAG          v2.4
    GIT_SHALLOW      TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(geographiclib)

# The in-tree build exports different target names depending on how it was
# configured, so pick whichever one actually exists.
if(TARGET GeographicLib::GeographicLib)
    set(GEOGRAPHICLIB_TARGET GeographicLib::GeographicLib)
elseif(TARGET GeographicLib_STATIC)
    set(GEOGRAPHICLIB_TARGET GeographicLib_STATIC)
elseif(TARGET GeographicLib_SHARED)
    set(GEOGRAPHICLIB_TARGET GeographicLib_SHARED)
else()
    message(FATAL_ERROR "GeographicLib was fetched but exported no usable target")
endif()
message(STATUS "GeographicLib target: ${GEOGRAPHICLIB_TARGET}")
