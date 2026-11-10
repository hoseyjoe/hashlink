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

# OS font lookup (fontsys_os_find, see fontsys_internal.h). The stub finds
# nothing; per-OS lookups replace it.
if(WIN32)
    set(FONTSYS_FIND_SOURCES fontsys_find_win.cpp)   # DirectWrite
elseif(APPLE)
    set(FONTSYS_FIND_SOURCES fontsys_find_apple.c)   # CoreText (macOS, iOS)
elseif(ANDROID)
    # AFontMatcher (API 29+, looked up at run time), else /system/etc/fonts.xml
    set(FONTSYS_FIND_SOURCES fontsys_find_android.c fontsys_fontsxml.c fontsys_fontsxml.h)
elseif(UNIX)
    set(FONTSYS_FIND_SOURCES fontsys_find_linux.c)   # fontconfig
    find_package(Fontconfig REQUIRED)
else()
    set(FONTSYS_FIND_SOURCES fontsys_find_stub.c)
endif()

# Everything except the HashLink bindings: no libhl dependency, so the
# plain C test below can link it.
add_library(fontsys_core STATIC
    fontsys.c
    fontsys_internal.h
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
if(APPLE)
    # PUBLIC: a static library, so whatever links fontsys_core needs them.
    target_link_libraries(fontsys_core PUBLIC "-framework CoreText" "-framework CoreFoundation")
endif()
if(ANDROID)
    # PUBLIC: a static library, so whatever links fontsys_core needs them.
    # libandroid is linked for API 24; the API 29 AFontMatcher_* functions are
    # looked up with dlsym, so nothing here requires API 29.
    target_link_libraries(fontsys_core PUBLIC android log ${CMAKE_DL_LIBS})
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID)
    # PUBLIC: a static library, so whatever links fontsys_core needs it.
    target_include_directories(fontsys_core PRIVATE ${Fontconfig_INCLUDE_DIRS})
    target_link_libraries(fontsys_core PUBLIC ${Fontconfig_LIBRARIES})
endif()
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

    # The Android fonts.xml parser is plain C: tested on every platform.
    add_executable(fontsys_fontsxml_test test/fontsys_fontsxml_test.c fontsys_fontsxml.c)
    target_include_directories(fontsys_fontsxml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME fontsys_fontsxml
             COMMAND fontsys_fontsxml_test ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/fonts.xml)
endif()
