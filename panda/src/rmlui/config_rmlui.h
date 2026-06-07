/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file config_rmlui.h
 * @author rdb
 * @date 2011-11-04
 */

#ifndef CONFIG_RMLUI_H
#define CONFIG_RMLUI_H

#include "pandabase.h"
#include "notifyCategoryProxy.h"
#include "configVariableBool.h"

NotifyCategoryDecl(rmlui, EXPCL_PANDARMUI, EXPTP_PANDARMUI);

// Set rmlui-layer-debug true in your PRC file (or PANDA_PRC_DIR) to log
// every flush_layer / blit call with FBO name, deferred draw count, and
// filter chain.  Requires the rmlui notify category to be at least "debug".
extern EXPCL_PANDARMUI ConfigVariableBool rmlui_layer_debug;

extern EXPCL_PANDARMUI void init_librmlui();

#endif
