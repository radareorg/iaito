cmake_minimum_required(VERSION 3.5)

if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DisallowInSource)
include(Utils)

set(IAITO_PYTHON_MIN 3.5)

option(IAITO_USE_BUNDLED_RADARE2 "Use radare2 from src/radare2 submodule instead of searching for it on the system" OFF)
option(IAITO_USE_ADDITIONAL_RADARE2_PATHS "Search radare2 in additional paths which are not part of default system library paths.\
 Disable this option if you are linking against radare2 pacakged as proper system library or in a custom path and additional are paths causing problems." ON)
option(IAITO_ENABLE_PYTHON "Enable Python integration. Requires Python >= ${IAITO_PYTHON_MIN}." OFF)
option(IAITO_ENABLE_PYTHON_BINDINGS "Enable generating Python bindings with Shiboken2. Unused if IAITO_ENABLE_PYTHON=OFF." OFF)
option(IAITO_ENABLE_CRASH_REPORTS "Enable crash report system. Unused if IAITO_ENABLE_CRASH_REPORTS=OFF" OFF)
option(IAITO_APPIMAGE_BUILD "Enable Appimage specific changes. Doesn't cause building of Appimage itself." OFF)
tri_option(IAITO_ENABLE_KSYNTAXHIGHLIGHTING "Use KSyntaxHighlighting" AUTO)
tri_option(IAITO_ENABLE_GRAPHVIZ "Enable use of graphviz for graph layout" AUTO)
set(SHIBOKEN_EXTRA_OPTIONS "" CACHE STRING "Extra options for shiboken generator")
set(IAITO_EXTRA_PLUGIN_DIRS "" CACHE STRING "List of addition plugin locations")
option(IAITO_ENABLE_DEPENDENCY_DOWNLOADS "Enable downloading of dependencies. Setting to OFF doesn't affect any downloads done by r2 build." OFF)
option(IAITO_PACKAGE_DEPENDENCIES "During install step include the third party dependencies." OFF)
option(IAITO_PACKAGE_R2GHIDRA "Compile and install r2ghidra during install step." OFF)
option(IAITO_PACKAGE_R2DEC "Compile and install r2dec during install step." OFF)
OPTION(IAITO_QT6 "Use QT6" OFF)

if(NOT IAITO_ENABLE_PYTHON)
    set(IAITO_ENABLE_PYTHON_BINDINGS OFF)
endif()

# Parse Iaito.pro to get filenames
include(QMakeProParse)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Iaito.pro"
        "${CMAKE_CURRENT_BINARY_DIR}/Iaito.pro"
        COPYONLY) # trigger reconfigure if Iaito.pro changes
parse_qmake_pro("${CMAKE_CURRENT_BINARY_DIR}/Iaito.pro" IAITO_PRO)
set(SOURCE_FILES ${IAITO_PRO_SOURCES})
set(HEADER_FILES ${IAITO_PRO_HEADERS})
set(UI_FILES ${IAITO_PRO_FORMS})
set(QRC_FILES ${IAITO_PRO_RESOURCES})
set(IAITO_VERSION_MAJOR "${IAITO_PRO_IAITO_VERSION_MAJOR}")
set(IAITO_VERSION_MINOR "${IAITO_PRO_IAITO_VERSION_MINOR}")
set(IAITO_VERSION_PATCH "${IAITO_PRO_IAITO_VERSION_PATCH}")

set(IAITO_VERSION_FULL "${IAITO_VERSION_MAJOR}.${IAITO_VERSION_MINOR}.${IAITO_VERSION_PATCH}")

project(iaito VERSION "${IAITO_VERSION_FULL}")

set(CMAKE_CXX_STANDARD 11)


set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
if (IAITO_QT6)
    set(QT_PREFIX Qt6)
else()
    set(QT_PREFIX Qt5)
endif()
find_package(${QT_PREFIX} REQUIRED COMPONENTS Core Widgets Gui Svg Network)

include(IaitoInstallDirs)

if(IAITO_USE_BUNDLED_RADARE2)
    include(BundledRadare2)
    set(RADARE2_TARGET Radare2)
else()
    find_package(Radare2 REQUIRED)
    set(RADARE2_TARGET Radare2::libr)
endif()

if(IAITO_ENABLE_PYTHON)
    find_package(PythonInterp REQUIRED)
    find_package(PythonLibs ${IAITO_PYTHON_MIN} REQUIRED)

    include_directories(${PYTHON_INCLUDE_DIRS})
    add_definitions(-DIAITO_ENABLE_PYTHON)

    if(IAITO_ENABLE_PYTHON_BINDINGS)
        # 5.12.3 => 5.12
        if("${Qt5_VERSION}" MATCHES "^([0-9]+\\.[0-9]+)\\.[0-9]+")
            set(Shiboken2_VERSION_REQUIRED "${CMAKE_MATCH_1}")
        else()
            message(FATAL_ERROR "Failed to recognize Qt version")
        endif()
        find_package(Shiboken2 "${Shiboken2_VERSION_REQUIRED}" REQUIRED)
        find_package(PySide2 "${Shiboken2_VERSION_REQUIRED}" REQUIRED)

        get_target_property(PYSIDE_INCLUDE_DIR PySide2::pyside2 INTERFACE_INCLUDE_DIRECTORIES)
        list(GET PYSIDE_INCLUDE_DIR 0 PYSIDE_INCLUDE_DIR)
        include_directories(${PYSIDE_INCLUDE_DIR}
            ${PYSIDE_INCLUDE_DIR}/QtCore
            ${PYSIDE_INCLUDE_DIR}/QtGui
            ${PYSIDE_INCLUDE_DIR}/QtWidgets)

        add_definitions(-DIAITO_ENABLE_PYTHON_BINDINGS)
    endif()
endif()

if(IAITO_ENABLE_KSYNTAXHIGHLIGHTING)
    if(IAITO_ENABLE_KSYNTAXHIGHLIGHTING STREQUAL AUTO)
        find_package(KF5SyntaxHighlighting)
        if(KF5SyntaxHighlighting_FOUND)
            set(KSYNTAXHIGHLIGHTING_STATUS ON)
        else()
            set(KSYNTAXHIGHLIGHTING_STATUS "OFF (KSyntaxHighlighting not found)")
        endif()
    else()
        find_package(KF5SyntaxHighlighting REQUIRED)
        set(KSYNTAXHIGHLIGHTING_STATUS ON)
    endif()
else()
    set(KSYNTAXHIGHLIGHTING_STATUS OFF)
endif()

if (IAITO_ENABLE_GRAPHVIZ)
    if (IAITO_ENABLE_GRAPHVIZ STREQUAL AUTO)
        find_package(Graphviz)
    else()
        find_package(Graphviz REQUIRED)
    endif()
    set (IAITO_ENABLE_GRAPHVIZ ${Graphviz_FOUND})
endif()

message(STATUS "")
message(STATUS "Building Iaito version ${IAITO_VERSION_FULL}")
message(STATUS "Options:")
message(STATUS "- Bundled radare2: ${IAITO_USE_BUNDLED_RADARE2}")
message(STATUS "- Python: ${IAITO_ENABLE_PYTHON}")
message(STATUS "- Python Bindings: ${IAITO_ENABLE_PYTHON_BINDINGS}")
message(STATUS "- Crash Handling: ${IAITO_ENABLE_CRASH_REPORTS}")
message(STATUS "- KSyntaxHighlighting: ${KSYNTAXHIGHLIGHTING_STATUS}")
message(STATUS "- Graphviz: ${IAITO_ENABLE_GRAPHVIZ}")
message(STATUS "")


include(QMakeConfigureFile)
qmake_configure_file("${CMAKE_CURRENT_SOURCE_DIR}/IaitoConfig.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/IaitoConfig.h")


if(IAITO_ENABLE_PYTHON_BINDINGS)
    set(BINDINGS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bindings")
    set(BINDINGS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/bindings")

    configure_file("${BINDINGS_SRC_DIR}/bindings.xml" "${BINDINGS_BUILD_DIR}/bindings.xml" COPYONLY) # trigger reconfigure if file changes

    execute_process(COMMAND "${PYTHON_EXECUTABLE}" "${BINDINGS_SRC_DIR}/src_list.py" cmake "${BINDINGS_BUILD_DIR}" OUTPUT_VARIABLE BINDINGS_SOURCE)

    set_property(SOURCE ${BINDINGS_SOURCE} PROPERTY SKIP_AUTOGEN ON)

    include_directories("${BINDINGS_BUILD_DIR}/IaitoBindings")

    set(SHIBOKEN_OPTIONS)
    if (WIN32)
        list(APPEND SHIBOKEN_OPTIONS --avoid-protected-hack)
    endif()

    add_custom_command(OUTPUT ${BINDINGS_SOURCE}
            COMMAND Shiboken2::shiboken2 --project-file="${BINDINGS_BUILD_DIR}/bindings.txt" ${SHIBOKEN_OPTIONS} ${SHIBOKEN_EXTRA_OPTIONS}
            DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/bindings/bindings.xml" "${BINDINGS_BUILD_DIR}/bindings.txt"
            IMPLICIT_DEPENDS CXX "${CMAKE_CURRENT_SOURCE_DIR}/bindings/bindings.h"
            COMMENT "Generating Python bindings with shiboken2")
else()
    set(BINDINGS_SOURCE "")
endif()


if (TARGET Graphviz::GVC)
    list(APPEND SOURCE_FILES ${IAITO_PRO_GRAPHVIZ_SOURCES})
    list(APPEND HEADER_FILES ${IAITO_PRO_GRAPHVIZ_HEADERS})
endif()

if (WIN32)
    set(PLATFORM_RESOURCES "img/iaito.rc")
else()
    set(PLATFORM_RESOURCES "")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_definitions(-Wall -Wextra)
    set_source_files_properties(${BINDINGS_SOURCE} PROPERTIES COMPILE_FLAGS -w)
endif()


add_executable(iaito MACOSX_BUNDLE ${UI_FILES} ${QRC_FILES} ${PLATFORM_RESOURCES} ${SOURCE_FILES} ${HEADER_FILES} ${BINDINGS_SOURCE})
set_target_properties(iaito PROPERTIES
        ENABLE_EXPORTS ON
        CXX_VISIBILITY_PRESET hidden
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist")
target_compile_definitions(iaito PRIVATE IAITO_SOURCE_BUILD)

set(IAITO_INCLUDE_DIRECTORIES core widgets common plugins menus .)
foreach(_dir ${IAITO_INCLUDE_DIRECTORIES})
    target_include_directories(iaito PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${_dir}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iaito/${_dir}>
    )
endforeach()

if (TARGET Graphviz::GVC)
    target_link_libraries(iaito PRIVATE Graphviz::GVC)
    target_compile_definitions(iaito PRIVATE IAITO_ENABLE_GRAPHVIZ)
endif()

if(IAITO_ENABLE_CRASH_REPORTS)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(iaito PRIVATE Threads::Threads)

    add_definitions(-DIAITO_ENABLE_CRASH_REPORTS)
    if (NOT WIN32)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g ")
    endif()
    find_package(Breakpad REQUIRED)
    target_link_libraries(iaito PRIVATE Breakpad::client)
endif()

target_link_libraries(iaito PUBLIC ${QT_PREFIX}::Core ${QT_PREFIX}::Widgets ${QT_PREFIX}::Gui PRIVATE  ${QT_PREFIX}::Svg ${QT_PREFIX}::Network)
target_link_libraries(iaito PUBLIC ${RADARE2_TARGET})
if(IAITO_ENABLE_PYTHON)
    if (WIN32)
        # On windows some of the Python STABLE API functions are in seperate library
        # which isn't added by CMake.
        get_filename_component(_PYTHON_LIB_DIR ${PYTHON_LIBRARIES} DIRECTORY)
        target_link_directories(iaito PRIVATE ${_PYTHON_LIB_DIR})
    endif()
    target_link_libraries(iaito PRIVATE ${PYTHON_LIBRARIES})
    if(IAITO_ENABLE_PYTHON_BINDINGS)
        target_link_libraries(iaito PRIVATE Shiboken2::libshiboken PySide2::pyside2)

        get_target_property(RAW_BINDINGS_INCLUDE_DIRS iaito INCLUDE_DIRECTORIES)
        set(BINDINGS_INCLUDE_DIRS "")
        foreach(_dir ${RAW_BINDINGS_INCLUDE_DIRS})
            string(REGEX REPLACE "\\$<BUILD_INTERFACE:(.*)>" "\\1" _dir ${_dir})
            string(REGEX REPLACE "\\$<INSTALL_INTERFACE:(.*)>" "" _dir ${_dir})
            if (NOT "${_dir}" STREQUAL "")
                list(APPEND BINDINGS_INCLUDE_DIRS "${_dir}")
            endif()
        endforeach()

        if(APPLE AND _qt5Core_install_prefix)
            list(APPEND BINDINGS_INCLUDE_DIRS "${_qt5Core_install_prefix}/include")
            list(APPEND BINDINGS_INCLUDE_DIRS "${_qt5Core_install_prefix}/include/QtCore")
            list(APPEND BINDINGS_INCLUDE_DIRS "${_qt5Core_install_prefix}/include/QtGui")
            list(APPEND BINDINGS_INCLUDE_DIRS "${_qt5Core_install_prefix}/include/QtWidgets")
        endif()
        list(APPEND BINDINGS_INCLUDE_DIRS ${Qt5Core_INCLUDE_DIRS} ${Qt5Widgets_INCLUDE_DIRS} ${Qt5Gui_INCLUDE_DIRS})
        list(APPEND BINDINGS_INCLUDE_DIRS ${Radare2_INCLUDE_DIRS})
        list(APPEND BINDINGS_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
        if (NOT WIN32)
            string(REPLACE ";" ":" BINDINGS_INCLUDE_DIRS "${BINDINGS_INCLUDE_DIRS}")
        endif()

        qmake_configure_file("${BINDINGS_SRC_DIR}/bindings.txt.in" "${BINDINGS_BUILD_DIR}/bindings.txt")
        add_compile_definitions(WIN32_LEAN_AND_MEAN)
    endif()
endif()

if(TARGET KF5::SyntaxHighlighting)
    target_link_libraries(iaito PRIVATE KF5::SyntaxHighlighting)
    target_compile_definitions(iaito PRIVATE IAITO_ENABLE_KSYNTAXHIGHLIGHTING)
endif()

if (IAITO_APPIMAGE_BUILD)
    target_compile_definitions(iaito PRIVATE APPIMAGE)
endif()

if (IAITO_PACKAGE_R2DEC)
    target_compile_definitions(iaito PRIVATE IAITO_APPVEYOR_R2DEC)
endif()

include(Translations)

# Install files
install(TARGETS iaito
        EXPORT iaitoTargets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        BUNDLE DESTINATION "." # needs to be tested
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Devel
)
install(EXPORT iaitoTargets
    NAMESPACE iaito::
    DESTINATION "${ConfigPackageLocation}"
    COMPONENT Devel)
install(FILES
    cmake/iaitoConfig.cmake
    DESTINATION ${ConfigPackageLocation}
    COMPONENT Devel
)
foreach(_file ${HEADER_FILES})
    # Can't use target PUBLIC_HEADER option for installing due to multiple directories
    get_filename_component(_header_dir "${_file}" DIRECTORY)
    install (FILES "${_file}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/iaito/${_header_dir}" COMPONENT Devel)
endforeach()

if(UNIX AND NOT APPLE)
    install (FILES "img/iaito-o.svg"
                DESTINATION "share/icons/hicolor/scalable/apps/")
    install(FILES "org.radare.iaito.desktop"
        DESTINATION "share/applications"
        COMPONENT Devel)
endif()

include(Packaging)
