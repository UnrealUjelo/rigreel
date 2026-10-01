cmake_minimum_required(VERSION 3.21)
project(DirectorStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Quick QuickWidgets QuickControls2 Network Svg Multimedia Concurrent ZlibPrivate
             Quick3D Quick3DPrivate)
qt_standard_project_setup(REQUIRES 6.8)

# ---------------------------------------------------------------- standalone engine (renderer, assets, games)
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/engine/*.cpp src/engine/*.h)
add_library(director_engine STATIC ${ENGINE_SOURCES})
target_include_directories(director_engine PUBLIC sdk src/engine)
target_link_libraries(director_engine PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Quick3D Qt6::Quick3DPrivate Qt6::Concurrent)

qt_add_executable(dirview tools/dirview/main.cpp)
qt_add_qml_module(dirview URI DirView VERSION 1.0
    QML_FILES tools/dirview/Main.qml qml/scene/ReMaterial.qml qml/scene/ReMultiply.qml qml/scene/TestMaterial.qml
    RESOURCES qml/scene/re_standard.frag qml/scene/re_multiply.frag qml/scene/re_multiply.vert)
target_link_libraries(dirview PRIVATE director_engine)

# ---------------------------------------------------------------- RE Engine game plugin
# The readers are a static library so the plugin DLL and the recli test tool share them.
file(GLOB RE_CORE_SOURCES CONFIGURE_DEPENDS plugins/reengine/*.cpp plugins/reengine/*.h)
list(FILTER RE_CORE_SOURCES EXCLUDE REGEX "ReEnginePlugin\\.(cpp|h)$")
add_library(reengine_core STATIC ${RE_CORE_SOURCES})
target_include_directories(reengine_core PUBLIC sdk plugins/reengine)
target_link_libraries(reengine_core PUBLIC Qt6::Core Qt6::Gui Qt6::ZlibPrivate)

qt_add_plugin(reengine SHARED CLASS_NAME ReEnginePlugin
    plugins/reengine/ReEnginePlugin.cpp plugins/reengine/ReEnginePlugin.h)
target_link_libraries(reengine PRIVATE reengine_core)
set_target_properties(reengine PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/games
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/games)
file(COPY plugins/reengine/data/ DESTINATION ${CMAKE_BINARY_DIR}/plugins/games/reengine)

qt_add_executable(recli tools/recli/main.cpp)
target_link_libraries(recli PRIVATE reengine_core)
set_target_properties(recli PROPERTIES WIN32_EXECUTABLE OFF)
target_compile_definitions(recli PRIVATE RE_PLUGIN_DATA="${CMAKE_CURRENT_SOURCE_DIR}/plugins/reengine/data")

configure_file(third_party/zstd/libzstd.dll ${CMAKE_BINARY_DIR}/libzstd.dll COPYONLY)

qt_add_executable(DirectorStudio WIN32
    src/main.cpp
    src/app/MainWindow.h src/app/MainWindow.cpp
    src/app/Theme.h src/app/Theme.cpp
    src/core/Paths.h src/core/Paths.cpp
    src/core/Bridge.h src/core/Bridge.cpp
    src/core/Studio.h src/core/Studio.cpp
    src/core/KeyedListModel.h src/core/KeyedListModel.cpp
    src/core/GameWindow.h src/core/GameWindow.cpp
    src/core/Capture.h src/core/Capture.cpp
    src/core/ControlServer.h src/core/ControlServer.cpp
    src/core/IconProvider.h src/core/IconProvider.cpp
    src/core/SoundCatalog.h src/core/SoundCatalog.cpp
    src/core/LoopbackRecorder.h src/core/LoopbackRecorder.cpp
    src/core/RenderJob.h src/core/RenderJob.cpp
    src/core/AnimImporter.h src/core/AnimImporter.cpp
    src/core/UiAutomation.h src/core/UiAutomation.cpp
    src/core/GuideAudio.h src/core/GuideAudio.cpp
    src/core/FilmRender.h src/core/FilmRender.cpp
    src/views/TimelineView.h src/views/TimelineView.cpp
    src/views/GraphView.h src/views/GraphView.cpp
    src/views/TimeCanvas.h src/views/TimeCanvas.cpp
    src/views/TimelineMath.h
    resources/app.rc
)

file(GLOB ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} resources/icons/*.svg)
file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml qml/controls/*.qml qml/panels/*.qml qml/sections/*.qml qml/scene/*.qml)
file(GLOB SHADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/scene/*.frag qml/scene/*.vert)
set_source_files_properties(qml/Theme.qml qml/Ui.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(DirectorStudio
    URI Director
    VERSION 1.0
    QML_FILES ${QML_SOURCES} qml/Names.js
    RESOURCES ${ICONS} resources/logo.svg ${SHADERS}
)

target_include_directories(DirectorStudio PRIVATE src src/core src/views src/app)
target_link_libraries(DirectorStudio PRIVATE director_engine
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickWidgets Qt6::QuickControls2
    Qt6::Network Qt6::Svg Qt6::Multimedia Qt6::Concurrent Qt6::ZlibPrivate Qt6::Quick3D Qt6::Quick3DPrivate
    user32 gdi32 dwmapi ole32 uuid winmm)
set_target_properties(DirectorStudio PROPERTIES OUTPUT_NAME RigReelStudio)

if(MINGW)
    target_link_options(DirectorStudio PRIVATE -static-libgcc -static-libstdc++)
    target_link_options(recli PRIVATE -static-libgcc -static-libstdc++)
endif()
