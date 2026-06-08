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
  _doc->Show();
}

/**
 * Hides the document.
 */
void RmlDocument::
hide() {
  _doc->Hide();
}

/**
 * Closes and destroys the document.  Do not use this wrapper after calling
 * close().
 */
void RmlDocument::
close() {
  _doc->Close();
}

/**
 * Returns the element with the given id attribute, or nullptr if not found.
 */
PT(RmlElement) RmlDocument::
get_element_by_id(const std::string &id) {
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
  return _doc->GetTitle();
}

/**
 * Sets the document's title string.
 */
void RmlDocument::
set_title(const std::string &title) {
  _doc->SetTitle(title);
}
