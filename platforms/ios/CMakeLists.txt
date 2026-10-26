cmake_minimum_required(VERSION 3.22)
project(KobaltIOS LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "Configure this directory with the ios preset or the iOS kit's qt-cmake. See doc/building.md.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(qt_components Core Gui Widgets Network Sensors Sql)
find_package(Qt6 6.11.3 EXACT REQUIRED COMPONENTS ${qt_components})
qt_standard_project_setup(REQUIRES 6.11)
file(REAL_PATH "${Qt6_DIR}/.." qt_config_root)
foreach(component IN LISTS qt_components)
    file(REAL_PATH "${Qt6${component}_DIR}/.." component_root)
    if(NOT component_root STREQUAL qt_config_root)
        message(FATAL_ERROR "Mixed Qt SDK cache for ${component}. Configure a fresh output or clear cached Qt6 package paths with -UQt6* before selecting the SDK.")
    endif()
endforeach()
get_filename_component(KOBALT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
foreach(retired KOBALT_DEPENDENCIES KOBALT_DEPENDENCY_CACHE KOBALT_ASSETS)
    unset(${retired} CACHE)
endforeach()

add_subdirectory("${KOBALT_ROOT}/dependencies" "${CMAKE_CURRENT_BINARY_DIR}/dependencies")
add_subdirectory("${KOBALT_ROOT}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets")

file(READ "${KOBALT_ASSETS}/manifest.json" assets)
string(JSON KOBALT_VERSION GET "${assets}" version)
string(JSON count LENGTH "${assets}" files)
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
    string(JSON name GET "${assets}" files ${index} path)
    if(IS_ABSOLUTE "${name}" OR name MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "Invalid asset path: ${name}")
    endif()
    string(JSON expected GET "${assets}" files ${index} sha256)
    file(SHA256 "${KOBALT_ASSETS}/${name}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Prepared runtime asset changed: ${name}")
    endif()
endforeach()
string(REGEX REPLACE "^v([0-9]+\\.[0-9]+(\\.[0-9]+)?).*$" "\\1" KOBALT_BUNDLE_VERSION "${KOBALT_VERSION}")

set(source "${KOBALT_ROOT}/src")
file(READ "${CRENGINE_SOURCE}/cr3gui/data/html5.css" READER_HTML_CSS)
configure_file("${source}/engine/crengine/reader_styles.h.in" reader_styles.h @ONLY)
file(GLOB patterns RELATIVE "${CRENGINE_SOURCE}/cr3gui/data/hyph" "${CRENGINE_SOURCE}/cr3gui/data/hyph/*.pattern")
list(SORT patterns)
list(TRANSFORM patterns PREPEND "\"")
list(TRANSFORM patterns APPEND "\"")
list(JOIN patterns ", " READER_HYPHENATION_PATTERNS)
configure_file("${source}/engine/crengine/reader_hyphenation.h.in" reader_hyphenation.h @ONLY)
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${KOBALT_ROOT}"
    OUTPUT_VARIABLE KOBALT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND git status --porcelain WORKING_DIRECTORY "${KOBALT_ROOT}"
    OUTPUT_VARIABLE modifications COMMAND_ERROR_IS_FATAL ANY)
set(KOBALT_COMMIT_MODIFIED OFF)
if(modifications)
    set(KOBALT_COMMIT_MODIFIED ON)
endif()
configure_file("${source}/support/build_info.h.in" build_info.h @ONLY)

set(sample_book "${KOBALT_ROOT}/tests/fixtures/trevelyans-type-tester.kepub.epub")
file(SHA256 "${sample_book}" sample_book_hash)
if(NOT sample_book_hash STREQUAL "1050bc7af6a6b19267319c8b56965ca7cbf63abedfa8079ab5c4eacbb6c407ac")
    message(FATAL_ERROR "Type Tester fixture does not match the pinned v1.2 copy with the chapter-heading CSS fix")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sample_book}")

file(GLOB_RECURSE shared CONFIGURE_DEPENDS
    "${source}/dictionaries/*.cpp" "${source}/engine/*.cpp"
    "${source}/library/*.cpp" "${source}/model/*.cpp"
    "${source}/reading/*.cpp" "${source}/storage/*.cpp")
add_library(kobalt-core OBJECT ${shared} display.cpp
    "${source}/assets/assets.cpp" "${source}/assets/bundled_fonts.cpp" "${source}/assets/font_catalog.cpp"
    "${source}/assets/system_fonts.cpp"
    "${source}/support/diagnostics.cpp"
    "${source}/publication/publication.cpp" "${source}/publication/zip_resource.cpp"
    "${source}/publication/resource_cache.cpp" "${source}/publication/local_resources.cpp"
    "${source}/publication/image_policy.cpp" "${source}/publication/layout_compatibility.cpp"
    "${source}/publication/native_resources.cpp" "${source}/publication/resource_server.cpp"
    "${source}/publication/plain_archive.cpp")
add_dependencies(kobalt-core kobalt-dependencies)
target_include_directories(kobalt-core PUBLIC "${KOBALT_ROOT}" "${source}" "${CMAKE_CURRENT_BINARY_DIR}"
    "${source}/interfaces/reader/ui/controls")
target_include_directories(kobalt-core SYSTEM PUBLIC
    "${CRENGINE_SOURCE}/crengine/include" "${CRENGINE_BUILD}"
    "${KOBALT_TEST_DEPENDENCIES}/include" "${KOBALT_TEST_DEPENDENCIES}/include/harfbuzz"
    "${KOBALT_TEST_DEPENDENCIES}/include/freetype2" "${KOBALT_TEST_DEPENDENCIES}/include/fribidi"
    "${KOBALT_TEST_PNG_INCLUDE}")
target_compile_definitions(kobalt-core PUBLIC KOBALT_READER_HARDWARE KOBALT_VERSION="${KOBALT_VERSION}"
    KOBALT_LAYOUT_POLICY="native-public-v1" fdatasync=fsync)
target_compile_options(kobalt-core PRIVATE -Wall -Wextra)
option(KOBALT_SQLITE_ANNOTATIONS_DEFAULT "Use SQLite annotation storage by default" ON)
if(KOBALT_SQLITE_ANNOTATIONS_DEFAULT)
    set_source_files_properties("${source}/storage/annotation_database.cpp" PROPERTIES
        COMPILE_DEFINITIONS KOBALT_SQLITE_ANNOTATIONS_DEFAULT=1)
else()
    set_source_files_properties("${source}/storage/annotation_database.cpp" PROPERTIES
        COMPILE_DEFINITIONS KOBALT_SQLITE_ANNOTATIONS_DEFAULT=0)
endif()
target_link_libraries(kobalt-core PUBLIC Qt6::Widgets Qt6::Network Qt6::Sensors Qt6::Sql
    "${CRENGINE_BUILD}/libcrengine.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/libharfbuzz.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libxxhash.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/libutf8proc.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libjpeg.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/liblunasvg.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libfreetype.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/libfribidi.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libunibreak.a"
    "${KOBALT_TEST_PNG}" z)
# The engine and Qt must keep separate FreeType glyph implementations.
target_link_options(kobalt-core INTERFACE "LINKER:-force_load,${KOBALT_TEST_DEPENDENCIES}/lib/libfreetype.a")

file(GLOB_RECURSE interfaces CONFIGURE_DEPENDS "${source}/interfaces/*.cpp")
add_library(kobalt-ios OBJECT ${interfaces} services.cpp
    ../android/reader.cpp
    "${source}/platform/api/reader_orientation.cpp")
target_link_libraries(kobalt-ios PUBLIC kobalt-core)
target_compile_options(kobalt-ios PRIVATE -Wall -Wextra)

qt_add_executable(Kobalt main.cpp)
target_link_libraries(Kobalt PRIVATE kobalt-ios kobalt-core)
target_compile_definitions(Kobalt PRIVATE KOBALT_IOS_SAMPLE_BOOK="sample/trevelyans-type-tester.kepub.epub")
set(resources "${KOBALT_ROOT}/assets")
file(GLOB notices CONFIGURE_DEPENDS "${resources}/notices/*")
qt_add_resources(Kobalt notices PREFIX "/kobalt" BASE "${resources}" FILES "${resources}/acknowledgments.json" ${notices})

set(KOBALT_IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple Development Team ID. Empty compiles without signing.")
set_target_properties(Kobalt PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Kobalt"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.nicoverbruggen.kobalt.ios"
    MACOSX_BUNDLE_BUNDLE_VERSION "${KOBALT_BUNDLE_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${KOBALT_BUNDLE_VERSION}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.nicoverbruggen.kobalt.ios"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST NO
    XCODE_ATTRIBUTE_INFOPLIST_KEY_LSRequiresIPhoneOS YES
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UIRequiresFullScreen YES
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UIStatusBarHidden YES
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UILaunchScreen_Generation YES
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UISupportedInterfaceOrientations "UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UISupportedInterfaceOrientations_iPad "UIInterfaceOrientationPortrait UIInterfaceOrientationPortraitUpsideDown UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight")
if(KOBALT_IOS_DEVELOPMENT_TEAM)
    set_target_properties(Kobalt PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${KOBALT_IOS_DEVELOPMENT_TEAM}")
else()
    set_target_properties(Kobalt PROPERTIES XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
endif()

set(bundle_dir "$<TARGET_FILE_DIR:Kobalt>")
add_custom_command(TARGET Kobalt POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${KOBALT_ASSETS}" "${bundle_dir}/assets"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${bundle_dir}/sample"
    COMMAND "${CMAKE_COMMAND}" -E copy "${sample_book}" "${bundle_dir}/sample/trevelyans-type-tester.kepub.epub"
    VERBATIM)
