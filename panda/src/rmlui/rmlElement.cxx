/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlElement.cxx
 * @author rdb
 * @date 2011-11-30
 */

#include "rmlElement.h"
#include "throw_event.h"
#include <RmlUi/Core/Element.h>

std::string RmlElement::
get_id() const {
  return _el->GetId();
}

void RmlElement::
set_inner_rml(const std::string &rml) {
  _el->SetInnerRML(rml);
}

std::string RmlElement::
get_inner_rml() const {
  return _el->GetInnerRML();
}

void RmlElement::
set_attribute(const std::string &name, const std::string &value) {
  _el->SetAttribute(name, value);
}

std::string RmlElement::
get_attribute(const std::string &name, const std::string &default_value) const {
  return _el->GetAttribute<Rml::String>(name, default_value);
}

void RmlElement::
set_class(const std::string &class_name, bool activate) {
  _el->SetClass(class_name, activate);
}

bool RmlElement::
is_class_set(const std::string &class_name) const {
  return _el->IsClassSet(class_name);
}

void RmlElement::
click() {
  _el->Click();
}

void RmlElement::
focus() {
  _el->Focus();
}

void RmlElement::
add_event_listener(const std::string &dom_event, const std::string &panda_event) {
  auto *listener = new PandaEventListener(panda_event);
  _listeners.push_back(listener);
  _el->AddEventListener(dom_event, listener);
}

RmlElement::
~RmlElement() {
  // Listeners are self-deleting via OnDetach; just clear our references.
  _listeners.clear();
}

void RmlElement::PandaEventListener::
ProcessEvent(Rml::Event &) {
  throw_event(panda_event);
}
