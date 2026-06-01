cmake_minimum_required(VERSION 3.21)

add_executable(platemaker
    main.cpp
)

target_link_libraries(platemaker
    PRIVATE
        Platemaker::platemaker
)

# ---------------------------------------------------------------------------
# Windows-specific: ensure the binary finds Qt DLLs next to the executable
# ---------------------------------------------------------------------------
if(WIN32 AND PLATEMAKER_BUILD_GUI)
    add_custom_command(TARGET platemaker POST_BUILD
        COMMAND ${QT_DEPLOY_PREFIX}/bin/windeployqt.exe
                --no-translations
                $<TARGET_FILE:platemaker>
        COMMENT "Deploying Qt runtime alongside platemaker.exe"
    )
endif()
