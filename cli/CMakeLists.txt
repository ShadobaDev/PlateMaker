cmake_minimum_required(VERSION 3.21)

if(WIN32)
    # app.rc supplies the icon; version.rc adds VERSIONINFO (Explorer Details / Process Explorer),
    # generated from PROJECT_VERSION so it stays in sync. MinGW compiles both via windres.
    enable_language(RC)
    configure_file(version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)
    set(RESOURCE_FILES app.rc "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
endif()

add_executable(platemaker-cli
    main.cpp
    ${RESOURCE_FILES}
)

# CLI uses vips directly (VIPS_INIT, vips_image_new_from_file, vips_error_buffer,
# vips_shutdown) and nlohmann/json directly — declare them explicitly since
# platemaker-lib exposes neither in its public API (both are PRIVATE there).
target_link_libraries(platemaker-cli
    PRIVATE
        Platemaker::platemaker
        VIPS::vips-cpp
        nlohmann_json::nlohmann_json
)

# Windows: CommandLineToArgvW (UTF-8 argv reconstruction in main()) lives in shell32.
if(WIN32)
    target_link_libraries(platemaker-cli PRIVATE shell32)
endif()

# ---------------------------------------------------------------------------
# Install — the CLI ships in its own CPack component ("cli"), packaged as the
# standalone platemaker-cli-<ver>-<sys>-<type> archive (see root CMakeLists.txt).
# The matching runtime libraries (libplatemaker + bundled DLLs on Windows /
# libplatemaker.so on Linux) are added to the same component from lib/.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS platemaker-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT cli
)
