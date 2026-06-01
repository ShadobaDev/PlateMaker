cmake_minimum_required(VERSION 3.21)

# This sub-directory is only added when PLATEMAKER_BUILD_GUI=ON (see root CMakeLists.txt)

set(PLATEMAKER_GUI_SOURCES
    main.cpp
)

qt_add_executable(platemaker-gui
    ${PLATEMAKER_GUI_SOURCES}
)

target_link_libraries(platemaker-gui
    PRIVATE
        Platemaker::platemaker
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# ---------------------------------------------------------------------------
# Windows: bundle Qt DLLs alongside the GUI binary
# ---------------------------------------------------------------------------
if(WIN32)
    add_custom_command(TARGET platemaker-gui POST_BUILD
        COMMAND ${QT_DEPLOY_PREFIX}/bin/windeployqt.exe
                --no-translations
                $<TARGET_FILE:platemaker-gui>
        COMMENT "Deploying Qt runtime alongside platemaker-gui.exe"
    )
endif()

# ---------------------------------------------------------------------------
# Linux: AppImage packaging handled externally via linuxdeploy (see docs)
# ---------------------------------------------------------------------------
