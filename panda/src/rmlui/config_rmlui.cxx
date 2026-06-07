/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file config_rmlui.cxx
 * @author rdb
 * @date 2011-11-04
 */

#include "config_rmlui.h"
#include "rmlInputHandler.h"
#include "rmlRegion.h"
#include "configVariableBool.h"

#ifndef CPPPARSER
#include "rmlFileInterface.h"
#include "rmlSystemInterface.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/StyleTypes.h>
#endif

#ifdef COMPILE_IN_DEFAULT_FONT
#ifdef HAVE_FREETYPE
#include "default_font.h"
#endif
#endif

#include "pandaSystem.h"
#include "dconfig.h"

#if !defined(CPPPARSER) && !defined(LINK_ALL_STATIC) && !defined(BUILDING_PANDARMUI)
  #error Buildsystem error: BUILDING_PANDARMUI not defined
#endif

Configure(config_rmlui);
NotifyCategoryDef(rmlui, "");

ConfigVariableBool rmlui_layer_debug
("rmlui-layer-debug", false,
 PRC_DESC("Set true to emit a debug line for every RmlUi layer flush and "
          "filter blit (FBO name, draw count, filter chain).  Requires "
          "notify-level-rmlui debug (or spam) to be set in the same PRC."));

ConfigureFn(config_rmlui) {
  init_librmlui();
}

/**
 * Initializes the library.  This must be called at least once before any of
 * the functions or classes in this library can be used.  Normally it will be
 * called by the static initializers and need not be called explicitly, but
 * special cases exist.
 */
void
init_librmlui() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  RmlInputHandler::init_type();
  RmlRegion::init_type();

  if (rmlui_cat->is_debug()) {
    rmlui_cat->debug() << "Initializing RmlUi library.\n";
  }

  static RmlFileInterface fi;
  Rml::SetFileInterface(&fi);

  static RmlSystemInterface si;
  Rml::SetSystemInterface(&si);

  Rml::Initialise();

#ifdef COMPILE_IN_DEFAULT_FONT
#ifdef HAVE_FREETYPE
  // Load Panda's compiled-in default font (Perspective Sans) so that RmlUi
  // has at least one font available without any user configuration.
  Rml::LoadFontFace(
    Rml::Span<const Rml::byte>(
      reinterpret_cast<const Rml::byte *>(default_font_data),
      default_font_size),
    "Panda Default",
    Rml::Style::FontStyle::Normal,
    Rml::Style::FontWeight::Normal,
    true);  // fallback_face = true
#endif
#endif

  PandaSystem *ps = PandaSystem::get_global_ptr();
  ps->add_system("RmlUi");
}
