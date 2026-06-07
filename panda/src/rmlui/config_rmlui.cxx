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

#ifndef CPPPARSER
#include "rmlFileInterface.h"
#include "rmlSystemInterface.h"
#include <RmlUi/Core/Core.h>
#endif

#include "pandaSystem.h"
#include "dconfig.h"

#if !defined(CPPPARSER) && !defined(LINK_ALL_STATIC) && !defined(BUILDING_PANDARMUI)
  #error Buildsystem error: BUILDING_PANDARMUI not defined
#endif

Configure(config_rmlui);
NotifyCategoryDef(rmlui, "");

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

  PandaSystem *ps = PandaSystem::get_global_ptr();
  ps->add_system("RmlUi");
}
