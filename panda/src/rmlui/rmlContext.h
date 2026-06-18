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
class RmlElement;
class RmlDataModel;

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

  PT(RmlDataModel) create_data_model(const std::string &name);
  PT(RmlDataModel) get_data_model(const std::string &name);
  bool remove_data_model(const std::string &name);

  MAKE_PROPERTY(width, get_width);
  MAKE_PROPERTY(height, get_height);
  MAKE_PROPERTY(name, get_name);

  // B1 — mouse interaction query
  bool is_mouse_interacting() const;

  // B2 — hit-test and hover/focus
  PT(RmlElement) get_element_at_point(float x, float y) const;
  PT(RmlElement) get_hover_element() const;
  PT(RmlElement) get_focus_element() const;

  MAKE_PROPERTY(hover_element, get_hover_element);
  MAKE_PROPERTY(focus_element, get_focus_element);

  // B3 — document lifecycle
  PT(RmlDocument) load_document_from_memory(const std::string &rml,
                                            const std::string &source_url = std::string());
  void unload_document(RmlDocument *doc);
  void unload_all_documents();
  int get_num_documents() const;

  MAKE_PROPERTY(num_documents, get_num_documents);

  // B4 — HiDPI and cursor
  void set_density_independent_pixel_ratio(float ratio);
  void enable_mouse_cursor(bool enable);

public:
  RmlContext() = default;
#ifndef CPPPARSER
  explicit RmlContext(Rml::Context *ctx) : _ctx(ctx) {}

  // Returns the underlying Rml::Context pointer.  The pointer is non-owning;
  // it must not be used after the owning RmlRegion has been destroyed.
  Rml::Context *get_raw() const { return _ctx; }

private:
  friend class RmlRegion;
  Rml::Context *_ctx = nullptr;
#endif
};

#endif
