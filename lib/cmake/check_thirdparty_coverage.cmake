# ---------------------------------------------------------------------------
# check_thirdparty_coverage.cmake — drift guard for the third-party notices.
#
# Fails if any *.dll in BIN_DIR is not mapped to a component in TABLE (cmake/third_party.json), so a
# libvips web-build bump that introduces a new bundled DLL cannot ship without a licence + copyright
# being added to the table (and thus to THIRD-PARTY-NOTICES.txt / the SBOM).
#
# Usage (script mode):
#   cmake -DBIN_DIR=<dir of *.dll> -DTABLE=<path/third_party.json> [-DIGNORE=a;b] \
#         -P lib/cmake/check_thirdparty_coverage.cmake
#
# BIN_DIR is the shipped DLL set — e.g. the libvips web-zip's bin/ (the Windows superset), or an
# installed bin/. The MinGW compiler runtime (libstdc++/libgcc/libwinpthread) is not in the web zip but
# IS in the table, so it never trips this check; the check only flags shipped-but-unmapped DLLs.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.19) # string(JSON …)
cmake_policy(SET CMP0057 NEW)        # if(… IN_LIST …) in script mode

if(NOT BIN_DIR OR NOT TABLE)
    message(FATAL_ERROR "check_thirdparty_coverage: pass -DBIN_DIR=<dir> and -DTABLE=<third_party.json>")
endif()
if(NOT EXISTS "${TABLE}")
    message(FATAL_ERROR "check_thirdparty_coverage: TABLE not found: ${TABLE}")
endif()

file(READ "${TABLE}" _tp)
string(JSON _n LENGTH "${_tp}" components)
math(EXPR _last "${_n} - 1")

# Collect every DLL basename the table declares.
set(_mapped "")
foreach(_i RANGE 0 ${_last})
    string(JSON _c GET "${_tp}" components ${_i})
    string(JSON _nd LENGTH "${_c}" dlls)
    if(_nd GREATER 0)
        math(EXPR _dllast "${_nd} - 1")
        foreach(_j RANGE 0 ${_dllast})
            string(JSON _d GET "${_c}" dlls ${_j})
            list(APPEND _mapped "${_d}")
        endforeach()
    endif()
endforeach()

# Our own library + the libvips C++ binding are not third-party components to attribute here.
list(APPEND IGNORE "libplatemaker" "libvips-cpp-42")

file(GLOB _dlls "${BIN_DIR}/*.dll")
if(NOT _dlls)
    message(FATAL_ERROR "check_thirdparty_coverage: no *.dll found in BIN_DIR=${BIN_DIR}")
endif()

set(_unmapped "")
set(_checked 0)
foreach(_p IN LISTS _dlls)
    get_filename_component(_name "${_p}" NAME)
    string(REGEX REPLACE "\\.dll$" "" _b "${_name}")
    if(_b IN_LIST IGNORE)
        continue()
    endif()
    math(EXPR _checked "${_checked} + 1")
    if(NOT _b IN_LIST _mapped)
        list(APPEND _unmapped "${_b}")
    endif()
endforeach()

if(_unmapped)
    list(JOIN _unmapped "\n  " _u)
    message(FATAL_ERROR
        "Third-party notice coverage FAILED — these bundled DLLs are not in cmake/third_party.json:\n"
        "  ${_u}\n"
        "Add each to the table with its licence + copyright (and vendor the licence text if new) so "
        "THIRD-PARTY-NOTICES.txt and the SBOM stay complete.")
endif()

message(STATUS "third-party coverage OK: all ${_checked} bundled DLL(s) in ${BIN_DIR} are mapped.")
