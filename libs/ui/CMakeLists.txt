add_library(ui.hdll ui_stub.c)

if(WIN32)
    set_target_properties(ui.hdll PROPERTIES SOURCES "ui_win.c;utils.cpp")
	target_link_libraries(ui.hdll Shell32 Ole32)
endif()

set_as_hdll(ui)

target_link_libraries(ui.hdll
    libhl
)

# ui_choose_dir drives IFileDialog: ole32 for COM itself, uuid for the
# CLSID_FileOpenDialog / IID_IFileDialog symbols it names, and shell32
# for SHCreateItemFromParsingName, which turns the initial directory
# into the IShellItem the dialog wants.
if(WIN32)
    target_link_libraries(ui.hdll ole32 uuid shell32)
endif()

install(
    TARGETS
        ui.hdll
    DESTINATION ${HDLL_DESTINATION}
)
