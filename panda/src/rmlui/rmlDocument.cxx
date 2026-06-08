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

/**
 * Makes the document visible and gives it focus.
 */
void RmlDocument::
show() {
  if (_doc != nullptr) {
    _doc->Show();
  }
}

/**
 * Hides the document.
 */
void RmlDocument::
hide() {
  if (_doc != nullptr) {
    _doc->Hide();
  }
}

/**
 * Closes and destroys the document.  The underlying document is freed on
 * the next Context::Update(); this wrapper nulls _doc immediately so that
 * any subsequent method call on the wrapper safely returns without crashing.
 */
void RmlDocument::
close() {
  if (_doc == nullptr) {
    return;
  }
  _doc->Close();
  _doc = nullptr;
}

/**
 * Returns the element with the given id attribute, or nullptr if not found.
 */
PT(RmlElement) RmlDocument::
get_element_by_id(const std::string &id) {
  if (_doc == nullptr) {
    return nullptr;
  }
  Rml::Element *el = _doc->GetElementById(id);
  if (el == nullptr) {
    return nullptr;
  }
  return new RmlElement(el);
}

/**
 * Returns the document's title string (the contents of the <title> element).
 */
std::string RmlDocument::
get_title() const {
  if (_doc == nullptr) {
    return std::string();
  }
  return _doc->GetTitle();
}

/**
 * Sets the document's title string.
 */
void RmlDocument::
set_title(const std::string &title) {
  if (_doc != nullptr) {
    _doc->SetTitle(title);
  }
}
