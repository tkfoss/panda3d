if(NOT HAVE_RMLUI)
  return()
endif()

set(P3RMLUI_HEADERS
  config_rmlui.h
  rmlFileInterface.h
  rmlInputHandler.h
  rmlRegion.I rmlRegion.h
  rmlRenderInterface.h
  rmlSystemInterface.h
)

set(P3RMLUI_SOURCES
  config_rmlui.cxx
  rmlFileInterface.cxx
  rmlInputHandler.cxx
  rmlRegion.cxx
  rmlRenderInterface.cxx
  rmlSystemInterface.cxx
)

composite_sources(p3rmlui P3RMLUI_SOURCES)
add_library(p3rmlui ${P3RMLUI_SOURCES} ${P3RMLUI_HEADERS})
set_target_properties(p3rmlui PROPERTIES DEFINE_SYMBOL BUILDING_PANDARMUI)
target_link_libraries(p3rmlui panda RmlUi::RmlUi)
target_interrogate(p3rmlui ALL)

if(HAVE_RMLUI_DEBUGGER)
  target_compile_definitions(p3rmlui PRIVATE HAVE_RMLUI_DEBUGGER)
endif()

install(TARGETS p3rmlui
  EXPORT RmlUi COMPONENT RmlUi
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/panda3d
  ARCHIVE COMPONENT RmlUiDevel)
install(FILES ${P3RMLUI_HEADERS}
  COMPONENT RmlUiDevel
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/panda3d)

export_targets(RmlUi COMPONENT RmlUiDevel)
