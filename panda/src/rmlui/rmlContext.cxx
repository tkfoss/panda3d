/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlContext.cxx
 * @author rdb
 * @date 2011-11-30
 */

#include "rmlContext.h"
#include "rmlDocument.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Context.h>

PT(RmlDocument) RmlContext::
load_document(const std::string &path) {
  Rml::ElementDocument *doc = _ctx->LoadDocument(path);
  if (doc == nullptr) {
    return nullptr;
  }
  return new RmlDocument(doc);
}

bool RmlContext::
load_font_face(const std::string &path, bool fallback) {
  return Rml::LoadFontFace(path, fallback);
}

void RmlContext::
update() {
  _ctx->Update();
}

void RmlContext::
render() {
  _ctx->Render();
}

int RmlContext::
get_width() const {
  return _ctx->GetDimensions().x;
}

int RmlContext::
get_height() const {
  return _ctx->GetDimensions().y;
}

std::string RmlContext::
get_name() const {
  return _ctx->GetName();
}
