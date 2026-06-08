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
#include <RmlUi/Core/Elements/ElementFormControl.h>

/**
 * Returns the element's id attribute.
 */
std::string RmlElement::
get_id() const {
  return _el->GetId();
}

/**
 * Replaces the element's inner content with the given RML markup string.
 */
void RmlElement::
set_inner_rml(const std::string &rml) {
  _el->SetInnerRML(rml);
}

/**
 * Returns the element's inner content as an RML markup string.
 */
std::string RmlElement::
get_inner_rml() const {
  return _el->GetInnerRML();
}

/**
 * Sets an attribute on the element.
 */
void RmlElement::
set_attribute(const std::string &name, const std::string &value) {
  _el->SetAttribute(name, value);
}

/**
 * Returns the value of the named attribute, or default_value if the attribute
 * is not present.
 */
std::string RmlElement::
get_attribute(const std::string &name, const std::string &default_value) const {
  return _el->GetAttribute<Rml::String>(name, default_value);
}

/**
 * Adds or removes a CSS class from the element.
 */
void RmlElement::
set_class(const std::string &class_name, bool activate) {
  _el->SetClass(class_name, activate);
}

/**
 * Returns true if the element currently has the given CSS class.
 */
bool RmlElement::
is_class_set(const std::string &class_name) const {
  return _el->IsClassSet(class_name);
}

/**
 * Simulates a mouse click on the element.
 */
void RmlElement::
click() {
  _el->Click();
}

/**
 * Moves keyboard focus to this element.
 */
void RmlElement::
focus() {
  _el->Focus();
}

/**
 * Returns the current value of a form control element (<input>, <select>,
 * <textarea>).  Returns an empty string if the element is not a form control.
 */
std::string RmlElement::
get_value() const {
  Rml::ElementFormControl *fc =
    rmlui_dynamic_cast<Rml::ElementFormControl *>(_el);
  return fc ? fc->GetValue() : std::string();
}

/**
 * Sets the value of a form control element.  Has no effect if the element is
 * not a form control.
 */
void RmlElement::
set_value(const std::string &value) {
  Rml::ElementFormControl *fc =
    rmlui_dynamic_cast<Rml::ElementFormControl *>(_el);
  if (fc) fc->SetValue(value);
}

/**
 * Attaches a Panda3D Messenger event to a DOM event on this element.
 * When the DOM event dom_event fires, throw_event(panda_event) is called.
 * Multiple listeners may be attached by calling this method repeatedly.
 */
void RmlElement::
add_event_listener(const std::string &dom_event, const std::string &panda_event) {
  auto *listener = new PandaEventListener(panda_event);
  _listeners.push_back(listener);
  _el->AddEventListener(dom_event, listener);
}

/**
 * Listeners are self-deleting via OnDetach; we just clear our pointer list.
 */
RmlElement::
~RmlElement() {
  _listeners.clear();
}

/**
 * Throws the associated Panda3D Messenger event when the DOM event fires.
 */
void RmlElement::PandaEventListener::
ProcessEvent(Rml::Event &) {
  throw_event(_panda_event);
}
