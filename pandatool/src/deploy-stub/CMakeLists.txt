if(NOT HAVE_PYTHON)
  return()
endif()

add_executable(deploy-stub deploy-stub.c)

if(IS_OSX)
  # -no_adhoc_codesign: build_apps inserts the frozen app into the __PANDA
  # segment and slides __LINKEDIT, but FreezeTool does not fix up an existing
  # LC_CODE_SIGNATURE, so the arm64 linker's default ad-hoc signature ends up
  # overlapping the inserted blob (malformed, unlaunchable). Leave the stub
  # unsigned; build_apps signs the final frozen executable.
  target_link_options(deploy-stub PRIVATE -sectcreate __PANDA __panda /dev/null
    -Wl,-no_adhoc_codesign)
  set_target_properties(deploy-stub PROPERTIES
    INSTALL_RPATH "@executable_path"
    BUILD_WITH_INSTALL_RPATH ON)

elseif(WIN32)
  target_sources(deploy-stub PRIVATE frozen_dllmain.c)

elseif(IS_LINUX OR IS_FREEBSD)
  set_target_properties(deploy-stub PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH ON)
  target_link_options(deploy-stub PRIVATE -Wl,--disable-new-dtags -Wl,-z,origin -rdynamic)
  target_link_libraries(deploy-stub m)
endif()

target_link_libraries(deploy-stub Python::Python)
install(TARGETS deploy-stub)

if(WIN32 OR IS_OSX)
  add_executable(deploy-stubw WIN32 deploy-stub.c)

  if(IS_OSX)
    target_link_options(deploy-stubw PRIVATE -sectcreate __PANDA __panda /dev/null
      -Wl,-no_adhoc_codesign)  # see deploy-stub above
    set_target_properties(deploy-stubw PROPERTIES
      INSTALL_RPATH "@executable_path/../Frameworks"
      BUILD_WITH_INSTALL_RPATH ON)
    target_compile_definitions(deploy-stubw PRIVATE MACOS_APP_BUNDLE=1)
  elseif(WIN32)
    target_sources(deploy-stubw PRIVATE frozen_dllmain.c)
  endif()

  target_link_libraries(deploy-stubw Python::Python)
  install(TARGETS deploy-stubw)
endif()
