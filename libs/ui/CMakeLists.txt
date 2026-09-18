add_library(ui.hdll ui_stub.c)

if(WIN32)
    set_target_properties(ui.hdll PROPERTIES SOURCES ui_win.c)
endif()

set_as_hdll(ui)

target_link_libraries(ui.hdll
    libhl
)

# ui_choose_dir drives IFileDialog: ole32 for COM itself, uuid for the
# CLSID_FileOpenDialog / IID_IFileDialog symbols it names.
if(WIN32)
    target_link_libraries(ui.hdll ole32 uuid)
endif()

install(
    TARGETS
        ui.hdll
    DESTINATION ${HDLL_DESTINATION}
)
