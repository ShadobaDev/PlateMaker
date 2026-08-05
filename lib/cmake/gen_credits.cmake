# ---------------------------------------------------------------------------
# gen_credits.cmake — generate the third-party NOTICES file and the bundled-dependency
# SBOM fragments from cmake/third_party.json + the libvips web-zip's versions.json.
#
# Included from lib/CMakeLists.txt on Windows (where we actually bundle the DLL graph). Reads from the
# caller scope and writes back to it:
#   in : _pm_credits_dir       — the staging credits/ directory (already created)
#        _pm_versions_file      — path to the web-zip versions.json (may not exist → versions NOASSERTION)
#   out: _pm_extra_packages     — JSON fragment: ",{…},{…}" appended into the SBOM packages[] array
#        _pm_extra_relationships — JSON fragment appended into the SBOM relationships[] array
#        (also writes _pm_credits_dir/THIRD-PARTY-NOTICES.txt)
#
# The component VERSIONS come from versions.json (authoritative, produced by whoever built the DLLs);
# the licence/copyright/homepage come from our curated cmake/third_party.json. The two strong-copyleft
# cases (libimagequant = GPL-3.0-or-later; MinGW libstdc++/libgcc = GPL-3.0 WITH GCC-exception) are in the
# table like any other. Legal accuracy of the table is curated best-effort — see its _comment.
# ---------------------------------------------------------------------------

set(_tp_file "${CMAKE_CURRENT_SOURCE_DIR}/cmake/third_party.json")
file(READ "${_tp_file}" _tp_json)

set(_versions_json "")
if(_pm_versions_file AND EXISTS "${_pm_versions_file}")
    file(READ "${_pm_versions_file}" _versions_json)
    message(STATUS "credits: using bundled-dependency versions from ${_pm_versions_file}")
else()
    message(STATUS "credits: versions.json not found — bundled versions will be NOASSERTION")
endif()

string(JSON _tp_count LENGTH "${_tp_json}" components)
math(EXPR _tp_last "${_tp_count} - 1")

set(_pm_extra_packages "")
set(_pm_extra_relationships "")

set(_notices
"Platemaker — THIRD-PARTY NOTICES
================================

Platemaker links libplatemaker (LGPL-3.0-or-later) and, on Windows, distributes the libvips runtime
DLL graph and the compiler runtime alongside it. The components below are bundled in this package; each
is listed with its version, upstream, copyright and licence. The full licence texts are in ./licenses/.

This file is generated at build time from the libvips web-build's versions.json and Platemaker's curated
attribution table (lib/cmake/third_party.json). SPDX ids and copyright lines are curated best-effort.
")

foreach(_i RANGE 0 ${_tp_last})
    string(JSON _c GET "${_tp_json}" components ${_i})
    string(JSON _name GET "${_c}" name)
    string(JSON _spdx GET "${_c}" spdx)
    string(JSON _lic  GET "${_c}" license_file)
    string(JSON _home GET "${_c}" homepage)
    string(JSON _copy GET "${_c}" copyright)
    string(JSON _vkey GET "${_c}" version_key)
    string(JSON _ndll LENGTH "${_c}" dlls)

    # Resolve the version: versions.json[version_key] first (authoritative), else an explicit "version"
    # field in the table, else NOASSERTION.
    set(_ver "NOASSERTION")
    if(NOT _vkey STREQUAL "" AND NOT _versions_json STREQUAL "")
        string(JSON _v ERROR_VARIABLE _verr GET "${_versions_json}" "${_vkey}")
        if(NOT _verr)
            set(_ver "${_v}")
        endif()
    endif()
    if(_ver STREQUAL "NOASSERTION")
        string(JSON _vx ERROR_VARIABLE _vxe GET "${_c}" version)
        if(NOT _vxe)
            set(_ver "${_vx}")
        endif()
    endif()

    # NOTICES: list every component (including libvips / nlohmann) — this is the human legal artifact.
    string(APPEND _notices "\n------------------------------------------------------------\n")
    string(APPEND _notices "${_name}  ${_ver}\n")
    string(APPEND _notices "  ${_home}\n")
    string(APPEND _notices "  ${_copy}\n")
    string(APPEND _notices "  Licence: ${_spdx}  (full text: licenses/${_lic})\n")

    # SBOM: add a package for every bundled DLL component, EXCEPT the two already in the core template
    # (libvips, nlohmann-json — the latter has no DLL anyway) to avoid duplicate SPDXIDs.
    if(_ndll GREATER 0 AND NOT _name STREQUAL "libvips")
        string(REGEX REPLACE "[^A-Za-z0-9.-]" "-" _sid "${_name}")
        set(_sid "SPDXRef-Package-${_sid}")
        string(APPEND _pm_extra_packages ",
    {
      \"SPDXID\": \"${_sid}\",
      \"name\": \"${_name}\",
      \"versionInfo\": \"${_ver}\",
      \"downloadLocation\": \"${_home}\",
      \"filesAnalyzed\": false,
      \"licenseConcluded\": \"${_spdx}\",
      \"licenseDeclared\": \"${_spdx}\",
      \"copyrightText\": \"${_copy}\"
    }")
        # The libvips runtime graph hangs off libvips; the toolchain runtimes (no version_key) off the lib.
        if(_vkey STREQUAL "")
            set(_parent "SPDXRef-Package-libplatemaker")
        else()
            set(_parent "SPDXRef-Package-libvips")
        endif()
        string(APPEND _pm_extra_relationships ",
    {
      \"spdxElementId\": \"${_parent}\",
      \"relationshipType\": \"DEPENDS_ON\",
      \"relatedSpdxElement\": \"${_sid}\"
    }")
    endif()
endforeach()

file(WRITE "${_pm_credits_dir}/THIRD-PARTY-NOTICES.txt" "${_notices}")
message(STATUS "credits: wrote THIRD-PARTY-NOTICES.txt (${_tp_count} components)")
