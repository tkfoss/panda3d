# Filename: FindRmlUi.cmake
#
# Usage:
#   find_package(RmlUi [REQUIRED] [QUIET])
#
# Once done this will define:
#   RmlUi_FOUND               - system has RmlUi
#   RmlUi_INCLUDE_DIR         - the RmlUi include directory
#   RmlUi_LIBRARY             - the RmlUi core library
#   RmlUi_DEBUGGER_LIBRARY    - the RmlUi debugger library (optional)
#   RmlUi::RmlUi              - imported target (core)
#   RmlUi::Debugger           - imported target (debugger, if found)
#

# Prefer the upstream CMake package config (installed via cmake --install)
find_package(RmlUi CONFIG QUIET)

if(RmlUi_FOUND AND TARGET RmlUi::RmlUi)
  # When using the upstream config, check for the Debugger component target
  # that is exported by RmlUi's own CMake install.
  if(NOT TARGET RmlUi::Debugger)
    find_library(RmlUi_DEBUGGER_LIBRARY
      NAMES "rmlui_debugger" "RmlUiDebugger" "rmlui_debuggerd" "RmlUiDebuggerd")
    if(RmlUi_DEBUGGER_LIBRARY)
      add_library(RmlUi::Debugger UNKNOWN IMPORTED GLOBAL)
      set_target_properties(RmlUi::Debugger PROPERTIES
        IMPORTED_LOCATION "${RmlUi_DEBUGGER_LIBRARY}"
        INTERFACE_LINK_LIBRARIES RmlUi::RmlUi)
    endif()
  endif()
  return()
endif()

# Fall back to manual detection
find_path(RmlUi_INCLUDE_DIR "RmlUi/Core.h")

find_library(RmlUi_LIBRARY
  NAMES "rmlui_core" "RmlUiCore" "rmlui" "RmlUi")

find_library(RmlUi_DEBUG_LIBRARY
  NAMES "rmlui_cored" "RmlUiCored" "rmluid")

find_library(RmlUi_DEBUGGER_LIBRARY
  NAMES "rmlui_debugger" "RmlUiDebugger")

find_library(RmlUi_DEBUGGER_DEBUG_LIBRARY
  NAMES "rmlui_debuggerd" "RmlUiDebuggerd")

mark_as_advanced(RmlUi_INCLUDE_DIR RmlUi_LIBRARY RmlUi_DEBUG_LIBRARY
                 RmlUi_DEBUGGER_LIBRARY RmlUi_DEBUGGER_DEBUG_LIBRARY)

if(RmlUi_INCLUDE_DIR AND (RmlUi_LIBRARY OR RmlUi_DEBUG_LIBRARY))
  if(NOT TARGET RmlUi::RmlUi)
    add_library(RmlUi::RmlUi UNKNOWN IMPORTED GLOBAL)
    set_target_properties(RmlUi::RmlUi PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${RmlUi_INCLUDE_DIR}")

    if(RmlUi_LIBRARY)
      set_property(TARGET RmlUi::RmlUi APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
      set_target_properties(RmlUi::RmlUi PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
        IMPORTED_LOCATION_RELEASE "${RmlUi_LIBRARY}")
    endif()

    if(RmlUi_DEBUG_LIBRARY)
      set_property(TARGET RmlUi::RmlUi APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
      set_target_properties(RmlUi::RmlUi PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
        IMPORTED_LOCATION_DEBUG "${RmlUi_DEBUG_LIBRARY}")
    endif()
  endif()

  if(NOT TARGET RmlUi::Debugger)
    if(RmlUi_DEBUGGER_LIBRARY OR RmlUi_DEBUGGER_DEBUG_LIBRARY)
      add_library(RmlUi::Debugger UNKNOWN IMPORTED GLOBAL)
      set_target_properties(RmlUi::Debugger PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${RmlUi_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES RmlUi::RmlUi)
      if(RmlUi_DEBUGGER_LIBRARY)
        set_property(TARGET RmlUi::Debugger APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
        set_target_properties(RmlUi::Debugger PROPERTIES
          IMPORTED_LOCATION_RELEASE "${RmlUi_DEBUGGER_LIBRARY}")
      endif()
      if(RmlUi_DEBUGGER_DEBUG_LIBRARY)
        set_property(TARGET RmlUi::Debugger APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
        set_target_properties(RmlUi::Debugger PROPERTIES
          IMPORTED_LOCATION_DEBUG "${RmlUi_DEBUGGER_DEBUG_LIBRARY}")
      endif()
    endif()
  endif()
endif()

include(FindPackageHandleStandardArgs)
# Require the include dir and at least one library (release or debug).
# Compute a helper variable that is set (to the found library path) when any
# library is available; find_package_handle_standard_args treats an unset or
# empty variable as missing.
if(RmlUi_LIBRARY)
  set(_RmlUi_ANY_LIBRARY "${RmlUi_LIBRARY}")
elseif(RmlUi_DEBUG_LIBRARY)
  set(_RmlUi_ANY_LIBRARY "${RmlUi_DEBUG_LIBRARY}")
endif()
find_package_handle_standard_args(RmlUi DEFAULT_MSG
  RmlUi_INCLUDE_DIR _RmlUi_ANY_LIBRARY)
