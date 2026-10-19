# Both the device and the native test host build the same engine adapter.
set(KOBALT_ENGINE_SOURCES
    model/page_refresh_regions
    engine/crengine/reader_model
    engine/crengine/reader_source
    engine/crengine/reader_text
    engine/crengine/reader_word_counts
    engine/crengine/reader_positions
    engine/crengine/reader_alignment
    engine/crengine/reader_page_layout
    engine/crengine/reader_links
    engine/crengine/font_weight
    assets/font_catalog)
list(TRANSFORM KOBALT_ENGINE_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/../src/")
list(TRANSFORM KOBALT_ENGINE_SOURCES APPEND ".cpp")

function(kobalt_link_private_freetype target dependency_prefix)
    if(APPLE)
        # Without the whole archive, initialization can come from Qt's bundled
        # FreeType while newer glyph operations use incompatible driver tables.
        target_link_options(${target} INTERFACE
            "LINKER:-force_load,${dependency_prefix}/lib/libfreetype.a")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Keep engine glyph calls on one FreeType build and hide it from Qt.
        target_link_options(${target} INTERFACE
            "LINKER:--whole-archive,${dependency_prefix}/lib/libfreetype.a,--no-whole-archive,--exclude-libs,libfreetype.a")
    endif()
endfunction()
