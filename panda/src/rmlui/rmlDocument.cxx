/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlDocument.cxx
 * @author rdb
 * @date 2011-11-30
 */

#include "rmlDocument.h"
#include "rmlElement.h"
#include <RmlUi/Core/ElementDocument.h>

void RmlDocument::
show() {
  _doc->Show();
}

void RmlDocument::
hide() {
  _doc->Hide();
}

void RmlDocument::
close() {
  _doc->Close();
}

PT(RmlElement) RmlDocument::
get_element_by_id(const std::string &id) {
  Rml::Element *el = _doc->GetElementById(id);
  if (el == nullptr) {
    return nullptr;
  }
  return new RmlElement(el);
}

std::string RmlDocument::
get_title() const {
  return _doc->GetTitle();
}

void RmlDocument::
set_title(const std::string &title) {
  _doc->SetTitle(title);
}
