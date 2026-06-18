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
#include "pointerTo.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

/**
 * Returns the element's id attribute.
 */
std::string RmlElement::
get_id() const {
  nassertr(_el != nullptr, std::string());
  return _el->GetId();
}

/**
 * Replaces the element's inner content with the given RML markup string.
 */
void RmlElement::
set_inner_rml(const std::string &rml) {
  nassertv(_el != nullptr);
  _el->SetInnerRML(rml);
}

/**
 * Returns the element's inner content as an RML markup string.
 */
std::string RmlElement::
get_inner_rml() const {
  nassertr(_el != nullptr, std::string());
  return _el->GetInnerRML();
}

/**
 * Sets an attribute on the element.
 */
void RmlElement::
set_attribute(const std::string &name, const std::string &value) {
  nassertv(_el != nullptr);
  _el->SetAttribute(name, value);
}

/**
 * Returns the value of the named attribute, or default_value if the attribute
 * is not present.
 */
std::string RmlElement::
get_attribute(const std::string &name, const std::string &default_value) const {
  nassertr(_el != nullptr, default_value);
  return _el->GetAttribute<Rml::String>(name, default_value);
}

/**
 * Adds or removes a CSS class from the element.
 */
void RmlElement::
set_class(const std::string &class_name, bool activate) {
  nassertv(_el != nullptr);
  _el->SetClass(class_name, activate);
}

/**
 * Returns true if the element currently has the given CSS class.
 */
bool RmlElement::
is_class_set(const std::string &class_name) const {
  nassertr(_el != nullptr, false);
  return _el->IsClassSet(class_name);
}

/**
 * Simulates a mouse click on the element.
 */
void RmlElement::
click() {
  nassertv(_el != nullptr);
  _el->Click();
}

/**
 * Moves keyboard focus to this element.
 */
void RmlElement::
focus() {
  nassertv(_el != nullptr);
  _el->Focus();
}

/**
 * Returns the current value of a form control element (<input>, <select>,
 * <textarea>).  Returns an empty string if the element is not a form control.
 */
std::string RmlElement::
get_value() const {
  nassertr(_el != nullptr, std::string());
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
  nassertv(_el != nullptr);
  Rml::ElementFormControl *fc =
    rmlui_dynamic_cast<Rml::ElementFormControl *>(_el);
  if (fc) fc->SetValue(value);
}

// ---------------------------------------------------------------------------
// Group A — additions
// ---------------------------------------------------------------------------

/**
 * Returns the element's offset position relative to its offset parent.
 */
LVecBase2f RmlElement::
get_relative_offset() const {
  nassertr(_el != nullptr, LVecBase2f(0, 0));
  Rml::Vector2f v = _el->GetRelativeOffset();
  return LVecBase2f(v.x, v.y);
}

/**
 * Returns the element's offset position in document (screen) space.
 */
LVecBase2f RmlElement::
get_absolute_offset() const {
  nassertr(_el != nullptr, LVecBase2f(0, 0));
  Rml::Vector2f v = _el->GetAbsoluteOffset();
  return LVecBase2f(v.x, v.y);
}

/**
 * Returns the rendered box width of the element in dp.
 */
float RmlElement::
get_offset_width() const {
  nassertr(_el != nullptr, 0.0f);
  return _el->GetOffsetWidth();
}

/**
 * Returns the rendered box height of the element in dp.
 */
float RmlElement::
get_offset_height() const {
  nassertr(_el != nullptr, 0.0f);
  return _el->GetOffsetHeight();
}

/**
 * Returns true if the element has the named attribute.
 */
bool RmlElement::
has_attribute(const std::string &name) const {
  nassertr(_el != nullptr, false);
  return _el->HasAttribute(name);
}

/**
 * Removes the named attribute from the element.
 */
void RmlElement::
remove_attribute(const std::string &name) {
  nassertv(_el != nullptr);
  _el->RemoveAttribute(name);
}

/**
 * Sets an inline CSS property on the element.  Returns false on parse error.
 */
bool RmlElement::
set_property(const std::string &name, const std::string &value) {
  nassertr(_el != nullptr, false);
  return _el->SetProperty(name, value);
}

/**
 * Removes an inline CSS property from the element.
 */
void RmlElement::
remove_property(const std::string &name) {
  nassertv(_el != nullptr);
  _el->RemoveProperty(name);
}

/**
 * Returns true if the given pseudo-class (e.g. "hover", "active") is set.
 */
bool RmlElement::
is_pseudo_class_set(const std::string &pseudo_class) const {
  nassertr(_el != nullptr, false);
  return _el->IsPseudoClassSet(pseudo_class);
}

/**
 * Scrolls the element into the viewport.  If align_with_top is true the
 * element is aligned with the top of the scroll container.
 */
void RmlElement::
scroll_into_view(bool align_with_top) {
  nassertv(_el != nullptr);
  _el->ScrollIntoView(align_with_top);
}

/**
 * Returns the first element matching the CSS selector, or nullptr.
 */
PT(RmlElement) RmlElement::
query_selector(const std::string &selector) {
  nassertr(_el != nullptr, nullptr);
  Rml::Element *result = _el->QuerySelector(selector);
  return result ? new RmlElement(result) : nullptr;
}

/**
 * Returns the parent element, or nullptr if this is the root.
 */
PT(RmlElement) RmlElement::
get_parent_node() const {
  nassertr(_el != nullptr, nullptr);
  Rml::Element *p = _el->GetParentNode();
  return p ? new RmlElement(p) : nullptr;
}

/**
 * Returns the child element at index, or nullptr if out of range.
 */
PT(RmlElement) RmlElement::
get_child(int index) const {
  nassertr(_el != nullptr, nullptr);
  Rml::Element *c = _el->GetChild(index);
  return c ? new RmlElement(c) : nullptr;
}

/**
 * Returns the number of direct child elements.
 */
int RmlElement::
get_num_children() const {
  nassertr(_el != nullptr, 0);
  return _el->GetNumChildren();
}

/**
 * Appends child to this element's DOM subtree.  child must be a freshly-
 * created element (via RmlDocument.create_element) whose _owned pointer is
 * still set; calling this on an already-inserted element is unsupported.
 * Returns a non-owning wrapper to the appended element (now DOM-owned).
 */
PT(RmlElement) RmlElement::
append_child(RmlElement *child) {
  nassertr(_el != nullptr, nullptr);
  nassertr(child != nullptr && child->_el != nullptr, nullptr);
  nassertr(child->_owned != nullptr, nullptr);
  Rml::Element *appended = _el->AppendChild(std::move(child->_owned));
  child->_owned = nullptr;
  return appended ? new RmlElement(appended) : nullptr;
}

/**
 * Removes child from this element's DOM subtree and frees it.
 * The child wrapper must not be used after this call.
 */
void RmlElement::
remove_child(RmlElement *child) {
  nassertv(_el != nullptr);
  nassertv(child != nullptr && child->_el != nullptr);
#ifdef HAVE_PYTHON
  // Detach listeners explicitly before RemoveChild fires OnDetach, so the
  // destructor's else-branch (when _el is null) doesn't double-delete them.
  for (auto *listener : child->_listeners) {
    listener->_owner = nullptr;
    child->_el->RemoveEventListener(listener->_event, listener);
  }
  child->_listeners.clear();
#endif
  _el->RemoveChild(child->_el);
  child->_el = nullptr;
}

#if defined(HAVE_PYTHON) && !defined(CPPPARSER)
#include "py_panda.h"
#include <RmlUi/Core/Event.h>

#ifndef RML_PY_DECREF_WITH_GIL_DEFINED
#define RML_PY_DECREF_WITH_GIL_DEFINED
static void
py_decref_with_gil(PyObject *obj) {
  if (obj) {
    PyGILState_STATE gs = PyGILState_Ensure();
    Py_DECREF(obj);
    PyGILState_Release(gs);
  }
}
#endif

/**
 * Attaches a Python callable to a DOM event on this element.
 * When dom_event fires, callback(event_dict) is invoked with a Python dict
 * containing all event parameters plus "type" (the DOM event type string).
 * Multiple listeners may be attached by calling this method repeatedly.
 */
void RmlElement::
add_event_listener(const std::string &dom_event, PyObject *callback) {
  nassertv(_el != nullptr);
  nassertv(callback != nullptr && callback != Py_None);

  Py_INCREF(callback);
  std::shared_ptr<PyObject> cb(callback, py_decref_with_gil);

  auto *listener = new PandaEventListener(std::move(cb), this, dom_event);
  _listeners.push_back(listener);
  _el->AddEventListener(dom_event, listener);
}

/**
 * Explicitly remove each listener from the DOM element before clearing the
 * list.  This triggers OnDetach (which calls delete this on each listener)
 * while _el is still valid, preventing dangling _owner dereferences later
 * if RmlUi outlives this wrapper.
 */
RmlElement::
~RmlElement() {
  if (_el != nullptr) {
    for (auto *listener : _listeners) {
      listener->_owner = nullptr;
      _el->RemoveEventListener(listener->_event, listener);
    }
  } else {
    for (auto *listener : _listeners) {
      listener->_owner = nullptr;
      delete listener;
    }
  }
  _listeners.clear();
  _el = nullptr;
}

/**
 * Builds a Python dict from the Rml::Event parameters and calls the callback.
 * Keys are the parameter names; values are bool, int, float, or str.
 * The key "type" always holds the DOM event type string (e.g. "click").
 */
void RmlElement::PandaEventListener::
ProcessEvent(Rml::Event &event) {
  PyGILState_STATE gs = PyGILState_Ensure();

  PyObject *d = PyDict_New();
  if (!d) {
    PyErr_Print();
    PyGILState_Release(gs);
    return;
  }

  // "type" is always present.
  const Rml::String &type = event.GetType();
  PyObject *type_str = PyUnicode_FromStringAndSize(type.data(), type.size());
  if (type_str) {
    PyDict_SetItemString(d, "type", type_str);
    Py_DECREF(type_str);
  }

  // All remaining parameters from the event dictionary.
  for (const auto &kv : event.GetParameters()) {
    const Rml::String &key = kv.first;
    const Rml::Variant &val = kv.second;

    PyObject *py_val = nullptr;
    switch (val.GetType()) {
    case Rml::Variant::BOOL:
      py_val = PyBool_FromLong(val.Get<bool>() ? 1 : 0);
      break;
    case Rml::Variant::BYTE:
    case Rml::Variant::CHAR:
    case Rml::Variant::INT:
      py_val = PyLong_FromLong(val.Get<int>());
      break;
    case Rml::Variant::INT64:
      py_val = PyLong_FromLongLong(val.Get<int64_t>());
      break;
    case Rml::Variant::UINT:
      py_val = PyLong_FromUnsignedLong(val.Get<unsigned int>());
      break;
    case Rml::Variant::UINT64:
      py_val = PyLong_FromUnsignedLongLong(val.Get<uint64_t>());
      break;
    case Rml::Variant::FLOAT:
      py_val = PyFloat_FromDouble(val.Get<float>());
      break;
    case Rml::Variant::DOUBLE:
      py_val = PyFloat_FromDouble(val.Get<double>());
      break;
    case Rml::Variant::STRING: {
      Rml::String s = val.Get<Rml::String>();
      py_val = PyUnicode_FromStringAndSize(s.data(), s.size());
      break;
    }
    default:
      py_val = Py_NewRef(Py_None);
      break;
    }

    PyObject *py_key = PyUnicode_FromStringAndSize(key.data(), key.size());
    if (py_key && py_val) {
      PyDict_SetItem(d, py_key, py_val);
    }
    Py_XDECREF(py_key);
    Py_XDECREF(py_val);
  }

  PyObject *result = PyObject_CallOneArg(_callback.get(), d);
  Py_DECREF(d);
  if (!result) {
    PyErr_Print();
  } else {
    Py_DECREF(result);
  }

  PyGILState_Release(gs);
}

#else  // !HAVE_PYTHON || CPPPARSER

RmlElement::~RmlElement() {
  _el = nullptr;
}

#endif  // HAVE_PYTHON && !CPPPARSER
