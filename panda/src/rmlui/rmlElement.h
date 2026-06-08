/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlElement.h
 * @author rdb
 * @date 2011-11-30
 */

#ifndef RML_ELEMENT_H
#define RML_ELEMENT_H

#include "config_rmlui.h"
#include "referenceCount.h"

#ifndef CPPPARSER
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/EventListener.h>
#include <string>
#include <vector>
#endif

/**
 * Thin Python-accessible wrapper around Rml::Element.
 *
 * Obtained via RmlDocument::get_element_by_id().
 */
class EXPCL_PANDARMUI RmlElement : public ReferenceCount {
PUBLISHED:
  std::string get_id() const;
  void set_inner_rml(const std::string &rml);
  std::string get_inner_rml() const;
  void set_attribute(const std::string &name, const std::string &value);
  std::string get_attribute(const std::string &name, const std::string &default_value = std::string()) const;
  void set_class(const std::string &class_name, bool activate);
  bool is_class_set(const std::string &class_name) const;
  void click();
  void focus();

  // value property for form controls (<input>, <select>, <textarea>).
  // Returns an empty string if the element is not a form control.
  std::string get_value() const;
  void set_value(const std::string &value);

  // Attach a named Panda3D event that fires when this element receives
  // the given DOM event (e.g. "click").  The event name thrown on the
  // Panda3D Messenger is panda_event.  Listeners accumulate; call again
  // with a different dom_event/panda_event to attach more.
  void add_event_listener(const std::string &dom_event,
                          const std::string &panda_event);

  MAKE_PROPERTY(id, get_id);
  MAKE_PROPERTY(inner_rml, get_inner_rml, set_inner_rml);
  MAKE_PROPERTY(value, get_value, set_value);

public:
  RmlElement() = default;
#ifndef CPPPARSER
  explicit RmlElement(Rml::Element *el) : _el(el) {}
  Rml::Element *get_raw() const { return _el; }

  ~RmlElement();

protected:
  Rml::Element *_el = nullptr;

  struct PandaEventListener : public Rml::EventListener {
    std::string panda_event;
    explicit PandaEventListener(const std::string &ev) : panda_event(ev) {}
    void ProcessEvent(Rml::Event &) override;
    void OnDetach(Rml::Element *) override { delete this; }
  };
  std::vector<PandaEventListener *> _listeners;
#endif
};

#endif
