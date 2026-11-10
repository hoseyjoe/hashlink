set(FREETYPE_DIR ${INCLUDES_BASE_DIR}/freetype)

# FreeType 2.13.3 (vendored, see include/freetype/README-hashlink.md),
# compiled straight into fontsys. Modules: see ftmodule.h.
set(FREETYPE_SOURCES
    ${FREETYPE_DIR}/src/base/ftsystem.c
    ${FREETYPE_DIR}/src/base/ftinit.c
    ${FREETYPE_DIR}/src/base/ftdebug.c
    ${FREETYPE_DIR}/src/base/ftbase.c
    ${FREETYPE_DIR}/src/base/ftbitmap.c
    ${FREETYPE_DIR}/src/base/ftglyph.c
    ${FREETYPE_DIR}/src/base/ftmm.c
    ${FREETYPE_DIR}/src/truetype/truetype.c
    ${FREETYPE_DIR}/src/sfnt/sfnt.c
    ${FREETYPE_DIR}/src/cff/cff.c
    ${FREETYPE_DIR}/src/psaux/psaux.c
    ${FREETYPE_DIR}/src/psnames/psnames.c
    ${FREETYPE_DIR}/src/pshinter/pshinter.c
    ${FREETYPE_DIR}/src/smooth/smooth.c
    ${FREETYPE_DIR}/src/sdf/sdf.c
    ${FREETYPE_DIR}/src/autofit/autofit.c
    ${FREETYPE_DIR}/src/gzip/ftgzip.c
)

# OS font lookup (fontsys_find); replaced per OS by later tasks.
set(FONTSYS_FIND_SOURCES fontsys_find_stub.c)

# Everything except the HashLink bindings: no libhl dependency, so the
# plain C test below can link it.
add_library(fontsys_core STATIC
    fontsys.c
    ${FONTSYS_FIND_SOURCES}
    ${FREETYPE_SOURCES}
)
set_target_properties(fontsys_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# ftmodule.h must be found before anything else of that name.
target_include_directories(fontsys_core BEFORE
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${FREETYPE_DIR}/include
)
target_compile_definitions(fontsys_core PRIVATE
    FT2_BUILD_LIBRARY
    "FT_CONFIG_MODULES_H=\"ftmodule.h\""
)
if(MSVC)
    # Vendored FreeType is kept unmodified: silence its CRT deprecation and
    # 64-bit size_t narrowing warnings (C4244/C4267 at /W3) on its sources only.
    set_source_files_properties(${FREETYPE_SOURCES} PROPERTIES
        COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS
        COMPILE_OPTIONS "/wd4244;/wd4267"
    )
endif()

if(BUILD_TESTING)
    add_executable(fontsys_test test/fontsys_test.c)
    target_link_libraries(fontsys_test fontsys_core)
    add_test(NAME fontsys COMMAND fontsys_test)
endif()
