cmake_minimum_required(VERSION 3.19)
project(kobalt LANGUAGES C CXX)
set(KOBALT_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(KOBALT_ROOT "${KOBALT_SOURCE_ROOT}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" KOBALT_DEFAULT_VERSION LIMIT_COUNT 1)
set(KOBALT_VERSION "${KOBALT_DEFAULT_VERSION}" CACHE STRING "Application version")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
include(cmake/Target.cmake)
include(cmake/Strings.cmake)
find_package(Qt${KOBALT_QT_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)
include(cmake/QtCompatibility.cmake)
set(KOBALT_FREETYPE_INCLUDE "${KOBALT_DEPENDENCY_PREFIX}/include/freetype2")
link_directories("${KOBALT_DEPENDENCY_PREFIX}/lib")
option(KOBALT_PAGE_ANIMATIONS "Build optional reader page animations" ON)
option(KOBALT_READER_HARDWARE "Build the native reader device package" ON)
option(KOBALT_READER_TEMPORARY "Remove the reader on its next Nickel restart" OFF)
option(KOBALT_MINI "Package Libron, NV Bitter and compatibility assets for focused testing" OFF)
configure_file(${KOBALT_HOOK_ROOT}/nhplugin.h nhplugin.h COPYONLY)
if(KOBALT_READER_TEMPORARY AND NOT KOBALT_READER_HARDWARE)
    message(FATAL_ERROR "KOBALT_READER_TEMPORARY requires KOBALT_READER_HARDWARE")
endif()
include(cmake/BuildConfiguration.cmake)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/nhplugin.json" "{\"Keys\":[\"nh.kobalt\"]}\n")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB_RECURSE KOBALT_ASSET_FILES CONFIGURE_DEPENDS resources/fonts/*.ttf resources/fonts/*.ttc
    vendor/ebook-fonts/*.ttf resources/hyph/*.pattern)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${KOBALT_ASSET_FILES} resources/fonts/ebook-core.json resources/fonts/noto-cjk.json
    dependencies/manifest.json tools/assets.py tools/mini_fonts.py tools/fetch_fonts.py tools/version.py)
set(KOBALT_ASSET_OPTIONS)
if(KOBALT_MINI)
    list(APPEND KOBALT_ASSET_OPTIONS --mini)
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/assets.py"
    --manifest "${CMAKE_CURRENT_BINARY_DIR}/asset-manifest.json" --version "${KOBALT_VERSION}"
    ${KOBALT_ASSET_OPTIONS}
    COMMAND_ERROR_IS_FATAL ANY)
add_library(reader-fonts STATIC src/assets/bundled_fonts.cpp src/assets/assets.cpp src/assets/system_fonts.cpp)
set_target_properties(reader-fonts PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(reader-fonts PUBLIC Qt${KOBALT_QT_MAJOR}::Gui PRIVATE z)
include(cmake/ReaderComponents.cmake)
kobalt_publication_library(reader-publication)

if(NOT CRENGINE_SOURCE)
    message(FATAL_ERROR "The reader requires CREngine")
endif()
find_package(Qt${KOBALT_QT_MAJOR} REQUIRED COMPONENTS Network Sensors)
add_subdirectory(src/interfaces/reader/ui)
# Device FreeType needs PNG even in checks that only link reader widgets.
target_link_libraries(reader-widgets PRIVATE png z)
add_library(reader-orientation STATIC src/platform/api/reader_orientation.cpp ${KOBALT_ORIENTATION_SOURCE})
set_target_properties(reader-orientation PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(reader-orientation PUBLIC Qt${KOBALT_QT_MAJOR}::Sensors Qt${KOBALT_QT_MAJOR}::Gui PRIVATE Qt${KOBALT_QT_MAJOR}::GuiPrivate ${CMAKE_DL_LIBS})
target_compile_options(reader-orientation PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)
find_package(Qt${KOBALT_QT_MAJOR} REQUIRED COMPONENTS Sql)
include(cmake/LayoutPolicy.cmake)
kobalt_layout_policy(LAYOUT_POLICY "${KOBALT_SOURCE_ROOT}" "${KOBALT_ARCHIVE_SOURCE}")
include(src/interfaces/reader/Sources.cmake)
add_library(reader-ui STATIC ${KOBALT_READER_INTERFACE_SOURCES})
set_target_properties(reader-ui PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(reader-ui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(reader-ui PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)
target_compile_options(reader-ui PRIVATE "-iquote${KOBALT_SOURCE_ROOT}/src")
target_compile_definitions(reader-ui PRIVATE KOBALT_VERSION="${KOBALT_VERSION}"
    $<$<BOOL:${KOBALT_PAGE_ANIMATIONS}>:KOBALT_PAGE_ANIMATIONS>
    $<$<BOOL:${KOBALT_READER_HARDWARE}>:KOBALT_READER_HARDWARE>)
target_link_libraries(reader-ui PUBLIC reader-widgets reader-reading reader-orientation)
set(READER_SOURCES ${KOBALT_INTEGRATION_SOURCES} ${KOBALT_REFRESH_SOURCE} ${KOBALT_ANIMATION_SOURCE}
    src/support/diagnostics.cpp src/platform/nickel/common/native_fallback.cpp
    src/platform/nickel/display/screen_density.cpp)
add_library(kobalt MODULE ${READER_SOURCES} src/platform/nickel/display/page_animation_hook.cpp ${KOBALT_HOOK_ROOT}/nh.c
    "${CMAKE_CURRENT_BINARY_DIR}/nhplugin.h")
target_include_directories(kobalt PRIVATE ${KOBALT_HOOK_ROOT})
target_compile_options(kobalt PRIVATE "-iquote${KOBALT_SOURCE_ROOT}/src")
target_compile_definitions(kobalt PRIVATE NH_VERSION="${KOBALT_VERSION}")
target_compile_definitions(kobalt PRIVATE KOBALT_VERSION="${KOBALT_VERSION}")
target_compile_definitions(kobalt PRIVATE KOBALT_SYSTEM_FONT_DIRECTORY="${KOBALT_SYSTEM_FONT_DIRECTORY}")
find_package(Qt${KOBALT_QT_MAJOR} REQUIRED COMPONENTS DBus)
target_link_libraries(kobalt PRIVATE Qt${KOBALT_QT_MAJOR}::DBus)
# About shows the commit. Only the generated header changes between commits,
# so a new commit recompiles only the files that include it.
set(KOBALT_COMMIT "" CACHE STRING "Commit of the sources, empty when unknown")
option(KOBALT_COMMIT_MODIFIED "The sources had uncommitted changes" OFF)
configure_file(src/support/build_info.h.in build_info.h @ONLY)
target_include_directories(kobalt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(kobalt PRIVATE -Wall -Wextra -Werror
    -Wno-missing-field-initializers -fvisibility=hidden)
target_link_libraries(kobalt PRIVATE Qt${KOBALT_QT_MAJOR}::Widgets Qt${KOBALT_QT_MAJOR}::GuiPrivate ${CMAKE_DL_LIBS} pthread)
target_link_options(kobalt PRIVATE -Wl,--no-undefined)
target_compile_definitions(kobalt PRIVATE KOBALT_LAYOUT_POLICY="${LAYOUT_POLICY}")
if(KOBALT_PAGE_ANIMATIONS)
    target_compile_definitions(kobalt PRIVATE KOBALT_PAGE_ANIMATIONS)
endif()
target_link_libraries(kobalt PRIVATE Qt${KOBALT_QT_MAJOR}::Sql)
file(GLOB READER_NOTICES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/notices/*")
foreach(target kobalt)
    qt_add_resources(${target} reader_notices PREFIX "/kobalt" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        FILES resources/acknowledgments.json ${READER_NOTICES})
endforeach()
if(KOBALT_READER_HARDWARE)
    target_sources(kobalt PRIVATE src/platform/nickel/common/install.c ${KOBALT_HOOK_ROOT}/resources.c)
    target_compile_definitions(kobalt PRIVATE KOBALT_READER_HARDWARE
        KOBALT_CONFIG_DIR="/mnt/onboard/.adds/kobalt")
    if(KOBALT_READER_TEMPORARY)
        target_compile_definitions(kobalt PRIVATE KOBALT_READER_TEMPORARY)
    endif()
    set(RESOURCE_DIR resources/reader)
    find_program(NH_PYTHON python3 REQUIRED)
    set(NH_RESOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/${RESOURCE_DIR}/doc ${CMAKE_CURRENT_SOURCE_DIR}/${RESOURCE_DIR}/uninstall)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/embedded_resources.h"
        COMMAND "${NH_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/${KOBALT_HOOK_ROOT}/embed_resources.py"
            "${CMAKE_CURRENT_BINARY_DIR}/embedded_resources.h" ${NH_RESOURCE_FILES}
        DEPENDS ${NH_RESOURCE_FILES} ${KOBALT_HOOK_ROOT}/embed_resources.py VERBATIM)
    target_sources(kobalt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/embedded_resources.h")
    target_include_directories(kobalt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
set(CRENGINE_SOURCE "" CACHE PATH "Pinned CREngine source with reader container support")
set(CRENGINE_BUILD "" CACHE PATH "CREngine build directory")
if(CRENGINE_SOURCE)
    # Redirect only CREngine's rasterization calls, without interposing on Qt
    # or changing the verified upstream archive.
    set(READER_ENGINE "${CMAKE_CURRENT_BINARY_DIR}/libcrengine-reader.a")
    add_custom_command(OUTPUT "${READER_ENGINE}"
        COMMAND "${CMAKE_OBJCOPY}"
            --redefine-sym FT_Load_Glyph=kobalt_font_load_glyph
            --redefine-sym FT_Render_Glyph=kobalt_font_render_glyph
            "${CRENGINE_BUILD}/libcrengine.a" "${READER_ENGINE}"
        DEPENDS "${CRENGINE_BUILD}/libcrengine.a" VERBATIM)
    add_custom_target(reader-engine DEPENDS "${READER_ENGINE}")
    include(cmake/EngineSources.cmake)
    add_library(reader-model STATIC ${KOBALT_ENGINE_SOURCES})
    target_compile_definitions(reader-model PRIVATE KOBALT_VERSION="${KOBALT_VERSION}")
    add_dependencies(reader-model reader-engine)
    file(READ "${CRENGINE_SOURCE}/cr3gui/data/html5.css" READER_HTML_CSS)
    configure_file(src/engine/crengine/reader_styles.h.in reader_styles.h @ONLY)
    file(GLOB READER_HYPHENATION_PATTERNS RELATIVE "${CRENGINE_SOURCE}/cr3gui/data/hyph" "${CRENGINE_SOURCE}/cr3gui/data/hyph/*.pattern")
    list(SORT READER_HYPHENATION_PATTERNS)
    list(TRANSFORM READER_HYPHENATION_PATTERNS PREPEND "\"")
    list(TRANSFORM READER_HYPHENATION_PATTERNS APPEND "\"")
    list(JOIN READER_HYPHENATION_PATTERNS ", " READER_HYPHENATION_PATTERNS)
    configure_file(src/engine/crengine/reader_hyphenation.h.in reader_hyphenation.h @ONLY)
    target_include_directories(reader-model PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    set_target_properties(reader-model PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(reader-model SYSTEM PRIVATE "${CRENGINE_SOURCE}/crengine/include" "${CRENGINE_BUILD}"
        "${KOBALT_DEPENDENCY_PREFIX}/include/harfbuzz" "${KOBALT_DEPENDENCY_PREFIX}/include" "${KOBALT_FREETYPE_INCLUDE}")
    target_link_libraries(reader-model PUBLIC reader-publication reader-fonts Qt${KOBALT_QT_MAJOR}::Gui "${READER_ENGINE}"
        "${KOBALT_DEPENDENCY_PREFIX}/lib/libharfbuzz.a" "${KOBALT_DEPENDENCY_PREFIX}/lib/libxxhash.a"
        "${KOBALT_DEPENDENCY_PREFIX}/lib/libutf8proc.a" "${KOBALT_DEPENDENCY_PREFIX}/lib/libjpeg.a" "${KOBALT_DEPENDENCY_PREFIX}/lib/liblunasvg.a" "${KOBALT_DEPENDENCY_PREFIX}/lib/libfreetype.a" png z)
    target_link_libraries(reader-model PUBLIC "${KOBALT_DEPENDENCY_PREFIX}/lib/libfribidi.a"
        "${KOBALT_DEPENDENCY_PREFIX}/lib/libunibreak.a")
    target_compile_options(reader-model PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)
    kobalt_reading_library(reader-reading reader-model reader-publication "${LAYOUT_POLICY}")
    kobalt_nickel_position_library(nickel-crengine-positions reader-model)
    target_link_libraries(kobalt PRIVATE nickel-crengine-positions)
    target_link_libraries(kobalt PRIVATE reader-ui reader-reading reader-model reader-orientation Qt${KOBALT_QT_MAJOR}::Network z)
    # Nickel's older HarfBuzz objects cannot be passed to our glyph-path
    # callbacks. Keep the bundled text and image libraries private.
    target_link_options(kobalt PRIVATE -Wl,--exclude-libs,libharfbuzz.a:libjpeg.a:liblunasvg.a:libutf8proc.a:libfribidi.a:libunibreak.a:libfreetype.a)
    if(KOBALT_QT_MAJOR EQUAL 5)
        # The firmware 4 flush hook reaches the platform screen.
        target_link_libraries(kobalt PRIVATE Qt5::GuiPrivate)
    endif()
endif()


# Source-root quoted includes do not shadow C library angle-bracket headers.
get_property(kobalt_source_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
list(APPEND kobalt_source_targets reader-widgets)
list(REMOVE_DUPLICATES kobalt_source_targets)
foreach(target IN LISTS kobalt_source_targets)
    get_target_property(kind ${target} TYPE)
    if(kind MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
        target_compile_options(${target} PRIVATE "-iquote${KOBALT_ROOT}/src")
    endif()
endforeach()

# SDK regression target; it is not included in release packages.
add_executable(refresh-scope-check tests/nickel/refresh_scope.cpp ${KOBALT_REFRESH_SOURCE})
target_link_libraries(refresh-scope-check PRIVATE Qt${KOBALT_QT_MAJOR}::Widgets ${CMAKE_DL_LIBS})
target_compile_options(refresh-scope-check PRIVATE -Wall -Wextra -Werror "-iquote${KOBALT_ROOT}/src")

option(KOBALT_WORD_COUNT_BENCHMARK "Build the word-count measurement program" OFF)
if(KOBALT_WORD_COUNT_BENCHMARK)
    add_executable(word-count-benchmark tests/model/word_count_benchmark.cpp)
    target_link_libraries(word-count-benchmark PRIVATE reader-model)
    target_compile_options(word-count-benchmark PRIVATE -Wall -Wextra -Werror
        "-iquote${KOBALT_ROOT}/src" "-iquote${KOBALT_ROOT}/tests/support")
    target_compile_definitions(word-count-benchmark PRIVATE KOBALT_VERSION="${KOBALT_VERSION}")
endif()
