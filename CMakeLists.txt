cmake_minimum_required(VERSION 3.21)

project(FlightViz
        VERSION 0.1.0
        DESCRIPTION "LTX AE assignment - flight data viewer with 2D map and 3D globe"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- Qt
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Sql)
qt_standard_project_setup()

# ------------------------------------------------- third-party (Task note:
# "Use of CMake and third party libraries for the tasks carries extra weight")
include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------- target
qt_add_executable(flightviz
    src/main.cpp

    src/model/FlightRecord.h
    src/model/CsvLoader.h          src/model/CsvLoader.cpp
    src/model/FlightTableModel.h   src/model/FlightTableModel.cpp

    src/io/GeoJsonWriter.h         src/io/GeoJsonWriter.cpp
    src/io/BinaryWriter.h          src/io/BinaryWriter.cpp

    src/map/Mercator.h
    src/map/MBTilesReader.h        src/map/MBTilesReader.cpp
    src/map/FlightTrack.h          src/map/FlightTrack.cpp
    src/map/GeodesicPath.h         src/map/GeodesicPath.cpp

    src/gl/LineBuffer.h
    src/gl/Map2DWidget.h           src/gl/Map2DWidget.cpp
    src/gl/Globe3DWidget.h         src/gl/Globe3DWidget.cpp

    src/ui/MainWindow.h            src/ui/MainWindow.cpp
)

target_include_directories(flightviz PRIVATE src)

# Warnings are scoped to this target: the fetched dependencies build with
# their own settings, and GeographicLib turns warnings into errors.
if(MSVC)
    target_compile_options(flightviz PRIVATE /W4)
else()
    target_compile_options(flightviz PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(flightviz PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Sql
    nlohmann_json::nlohmann_json
    ${GEOGRAPHICLIB_TARGET}
)

set_target_properties(flightviz PROPERTIES WIN32_EXECUTABLE ON)

# Keep the sample data next to the binary so the app can find it on launch.
add_custom_command(TARGET flightviz POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/NZ5_Flightdata.csv"
            "$<TARGET_FILE_DIR:flightviz>/NZ5_Flightdata.csv"
    COMMENT "Staging NZ5_Flightdata.csv next to the executable")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/World_10M.mbtiles")
    add_custom_command(TARGET flightviz POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/data/World_10M.mbtiles"
                "$<TARGET_FILE_DIR:flightviz>/World_10M.mbtiles"
        COMMENT "Staging World_10M.mbtiles next to the executable")
else()
    message(WARNING "data/World_10M.mbtiles not found - the map and globe will start empty")
endif()
