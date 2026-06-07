/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlDocument.h
 * @author rdb
 * @date 2011-11-30
 */

#ifndef RML_DOCUMENT_H
#define RML_DOCUMENT_H

#include "config_rmlui.h"
#include "referenceCount.h"
#include "pointerTo.h"

class RmlElement;

#ifndef CPPPARSER
#include <RmlUi/Core/ElementDocument.h>
#endif

/**
 * Thin Python-accessible wrapper around Rml::ElementDocument.
 *
 * Obtained via RmlContext::load_document().  The document is owned by
 * RmlUi; this wrapper holds a non-owning pointer.
 */
class EXPCL_PANDARMUI RmlDocument : public ReferenceCount {
PUBLISHED:
  void show();
  void hide();
  void close();

  PT(RmlElement) get_element_by_id(const std::string &id);

  std::string get_title() const;
  void set_title(const std::string &title);
  MAKE_PROPERTY(title, get_title, set_title);

public:
  RmlDocument() = default;
#ifndef CPPPARSER
  explicit RmlDocument(Rml::ElementDocument *doc) : _doc(doc) {}
  Rml::ElementDocument *get_raw() const { return _doc; }
private:
  Rml::ElementDocument *_doc = nullptr;
#endif
};

#endif
