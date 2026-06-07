# Filename: FindRmlUi.cmake
#
# Usage:
#   find_package(RmlUi [REQUIRED] [QUIET])
#
# Once done this will define:
#   RmlUi_FOUND          - system has RmlUi
#   RmlUi_INCLUDE_DIR    - the RmlUi include directory
#   RmlUi_LIBRARY        - the RmlUi core library
#   RmlUi::RmlUi         - imported target
#

# Prefer the upstream CMake package config (installed via cmake --install)
find_package(RmlUi CONFIG QUIET)

if(RmlUi_FOUND AND TARGET RmlUi::RmlUi)
  return()
endif()

# Fall back to manual detection
find_path(RmlUi_INCLUDE_DIR "RmlUi/Core.h")

find_library(RmlUi_LIBRARY
  NAMES "rmlui_core" "RmlUiCore" "rmlui" "RmlUi")

find_library(RmlUi_DEBUG_LIBRARY
  NAMES "rmlui_cored" "RmlUiCored" "rmluid")

mark_as_advanced(RmlUi_INCLUDE_DIR RmlUi_LIBRARY RmlUi_DEBUG_LIBRARY)

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
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RmlUi DEFAULT_MSG RmlUi_INCLUDE_DIR RmlUi_LIBRARY)
