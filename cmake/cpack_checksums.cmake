# cpack_checksums.cmake — CPACK_POST_BUILD_SCRIPTS hook (CMake 3.19+).
#
# CPack runs this after the archives are built; CPACK_PACKAGE_FILES holds their full paths. For each
# archive we write a sidecar "<archive>.sha256" in coreutils/sha256sum format ("<hash>  <name>", two
# spaces, filename only). One sidecar per archive — the dev and cli packages, in every config — so
# nothing collides when several are uploaded to one GitHub Release. Users verify with
# `sha256sum -c <archive>.sha256` (Linux/MSYS2) or `Get-FileHash` (Windows), and the hash can be
# pasted straight into the release notes.
# CPACK_PACKAGE_FILES points at the packages in CPack's temp dir (they are copied to
# CPACK_OUTPUT_FILE_PREFIX = dist/ *after* this script runs). The bytes are identical, so we hash the
# temp copy but write the sidecar into the output prefix, where the release archives end up.
foreach(_pkg IN LISTS CPACK_PACKAGE_FILES)
    if(EXISTS "${_pkg}")
        file(SHA256 "${_pkg}" _hash)
        get_filename_component(_name "${_pkg}" NAME)
        if(CPACK_OUTPUT_FILE_PREFIX)
            set(_out "${CPACK_OUTPUT_FILE_PREFIX}/${_name}.sha256")
        else()
            set(_out "${_pkg}.sha256")
        endif()
        file(WRITE "${_out}" "${_hash}  ${_name}\n")
        message(STATUS "checksum: ${_hash}  ${_name}")
    endif()
endforeach()
