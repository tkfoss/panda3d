/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlContext.h
 * @author rdb
 * @date 2011-11-30
 */

#ifndef RML_CONTEXT_H
#define RML_CONTEXT_H

#include "config_rmlui.h"
#include "referenceCount.h"
#include "pointerTo.h"

class RmlDocument;

#ifndef CPPPARSER
#include <RmlUi/Core/Context.h>
#endif

/**
 * Thin Python-accessible wrapper around Rml::Context.
 *
 * Obtained via RmlRegion::get_context().  The context is owned by RmlUi;
 * this object holds a non-owning pointer.  Do not use after the owning
 * RmlRegion has been destroyed.
 */
class EXPCL_PANDARMUI RmlContext : public ReferenceCount {
PUBLISHED:
  PT(RmlDocument) load_document(const std::string &path);
  bool load_font_face(const std::string &path, bool fallback = false);

  void update();
  void render();

  int get_width() const;
  int get_height() const;
  std::string get_name() const;

  MAKE_PROPERTY(width, get_width);
  MAKE_PROPERTY(height, get_height);
  MAKE_PROPERTY(name, get_name);

public:
  RmlContext() = default;
#ifndef CPPPARSER
  explicit RmlContext(Rml::Context *ctx) : _ctx(ctx) {}
  Rml::Context *get_raw() const { return _ctx; }
private:
  Rml::Context *_ctx = nullptr;
#endif
};

#endif
