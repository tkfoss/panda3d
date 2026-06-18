/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlDataModel.cxx
 * @author tkfoss
 * @date 2026-06-08
 */

#include "rmlDataModel.h"

#ifndef CPPPARSER
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataTypes.h>
#include <RmlUi/Core/Variant.h>
#endif  // CPPPARSER

/**
 * Returns true if this handle refers to a live data model.
 */
bool RmlDataModel::
is_valid() const {
  return _valid;
}

/**
 * Marks the named variable dirty so RmlUi re-evaluates any DOM expressions
 * that reference it.  Call this after changing the value Python returns from
 * the getter registered via bind_func().
 */
void RmlDataModel::
dirty_variable(const std::string &name) {
  _handle.DirtyVariable(name);
}

/**
 * Marks every variable in the model dirty.  Use when many values change at
 * once and it is not worth enumerating them individually.
 */
void RmlDataModel::
dirty_all() {
  _handle.DirtyAllVariables();
}

#if defined(HAVE_PYTHON) && !defined(CPPPARSER)
#include "py_panda.h"
#include <memory>
#include <vector>
#include <RmlUi/Core/DataVariable.h>

#ifndef RML_PY_DECREF_WITH_GIL_DEFINED
#define RML_PY_DECREF_WITH_GIL_DEFINED
static void py_decref_with_gil(PyObject *obj) {
  if (obj) {
    PyGILState_STATE gs = PyGILState_Ensure();
    Py_DECREF(obj);
    PyGILState_Release(gs);
  }
}
#endif

// Helpers: convert between Rml::Variant and Python objects.

static PyObject *variant_to_python(const Rml::Variant &v) {
  switch (v.GetType()) {
  case Rml::Variant::BOOL:
    return PyBool_FromLong(v.Get<bool>() ? 1 : 0);
  case Rml::Variant::BYTE:
  case Rml::Variant::CHAR:
  case Rml::Variant::INT:
    return PyLong_FromLong(v.Get<int>());
  case Rml::Variant::INT64:
    return PyLong_FromLongLong(v.Get<int64_t>());
  case Rml::Variant::UINT:
    return PyLong_FromUnsignedLong(v.Get<unsigned int>());
  case Rml::Variant::UINT64:
    return PyLong_FromUnsignedLongLong(v.Get<uint64_t>());
  case Rml::Variant::FLOAT:
    return PyFloat_FromDouble(v.Get<float>());
  case Rml::Variant::DOUBLE:
    return PyFloat_FromDouble(v.Get<double>());
  case Rml::Variant::STRING:
    {
      Rml::String s = v.Get<Rml::String>();
      return PyUnicode_FromStringAndSize(s.data(), s.size());
    }
  default:
    Py_RETURN_NONE;
  }
}

static void python_to_variant(PyObject *obj, Rml::Variant &out) {
  if (obj == Py_None) {
    out.Clear();
  } else if (PyBool_Check(obj)) {
    out = (obj == Py_True);
  } else if (PyLong_Check(obj)) {
    out = (int)PyLong_AsLong(obj);
  } else if (PyFloat_Check(obj)) {
    out = (double)PyFloat_AsDouble(obj);
  } else if (PyUnicode_Check(obj)) {
    Py_ssize_t len;
    const char *s = PyUnicode_AsUTF8AndSize(obj, &len);
    if (s) {
      out = Rml::String(s, (size_t)len);
    }
  }
}

/**
 * Binds a named variable backed by Python callables.
 *
 * getter(variant_out)  — called by RmlUi to read the value.  The callable
 *   receives no arguments and must return a value that can be converted to an
 *   Rml::Variant (bool, int, float, str, or None).
 *
 * setter(value)  — called by RmlUi when a data-controller writes back a new
 *   value (e.g. two-way binding).  Receives one argument (bool/int/float/str).
 *   Pass None (the default) if the variable is read-only.
 *
 * Returns True on success, False if the constructor is invalid or the name is
 * already bound.
 */
bool RmlDataModel::
bind_func(const std::string &name, PyObject *getter, PyObject *setter) {
  if (!_constructor) {
    return false;
  }

  // Normalise setter: treat Py_None the same as nullptr (read-only variable).
  if (setter == Py_None) {
    setter = nullptr;
  }

  Py_XINCREF(getter);
  std::shared_ptr<PyObject> getter_ref(getter, py_decref_with_gil);

  std::shared_ptr<PyObject> setter_ref;
  if (setter != nullptr) {
    Py_XINCREF(setter);
    setter_ref.reset(setter, py_decref_with_gil);
  }

  Rml::DataGetFunc get_fn = [getter_ref](Rml::Variant &out) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject *result = PyObject_CallNoArgs(getter_ref.get());
    if (!result) {
      PyErr_Print();
    } else {
      python_to_variant(result, out);
      Py_DECREF(result);
    }
    PyGILState_Release(gstate);
  };

  Rml::DataSetFunc set_fn;
  if (setter_ref) {
    set_fn = [setter_ref](const Rml::Variant &v) {
      PyGILState_STATE gstate = PyGILState_Ensure();
      PyObject *arg = variant_to_python(v);
      PyObject *result = PyObject_CallOneArg(setter_ref.get(), arg);
      Py_DECREF(arg);
      if (!result) {
        PyErr_Print();
      } else {
        Py_DECREF(result);
      }
      PyGILState_Release(gstate);
    };
  }

  return _constructor.BindFunc(name, std::move(get_fn), std::move(set_fn));
}

// PythonListDefinition is a VariableDefinition backed by a Python list getter.
// Size() calls the getter and caches the resulting Variant list.
// Child(index) returns a DataVariable pointing into that cache.
// RmlUi calls Size then iterates Child(0)..Child(n-1) within one update pass,
// so the cache is always populated before any Child call.

namespace {

struct VariantScalarDefinition final : public Rml::VariableDefinition {
  VariantScalarDefinition() : VariableDefinition(Rml::DataVariableType::Scalar) {}
  bool Get(void *ptr, Rml::Variant &out) override {
    out = *static_cast<const Rml::Variant *>(ptr);
    return true;
  }
  bool Set(void *ptr, const Rml::Variant &v) override {
    *static_cast<Rml::Variant *>(ptr) = v;
    return true;
  }
};

struct PythonListDefinition final : public Rml::VariableDefinition {
  std::shared_ptr<PyObject> _getter;
  VariantScalarDefinition   _scalar;
  std::vector<Rml::Variant> _cache;
  Rml::Variant              _size_variant;

  explicit PythonListDefinition(std::shared_ptr<PyObject> getter)
    : VariableDefinition(Rml::DataVariableType::Array)
    , _getter(std::move(getter))
  {}

  // Calls the Python getter, populates _cache, returns list length.
  int Size(void *) override {
    _cache.clear();

    PyGILState_STATE gs = PyGILState_Ensure();
    PyObject *list = PyObject_CallNoArgs(_getter.get());
    if (!list) {
      PyErr_Print();
      PyGILState_Release(gs);
      return 0;
    }

    if (!PyList_Check(list) && !PyTuple_Check(list)) {
      rmlui_cat.error() << "bind_list getter must return a list or tuple\n";
      Py_DECREF(list);
      PyGILState_Release(gs);
      return 0;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(list);
    _cache.reserve(n);
    PyObject **items = PySequence_Fast_ITEMS(list);
    for (Py_ssize_t i = 0; i < n; ++i) {
      Rml::Variant v;
      python_to_variant(items[i], v);
      _cache.push_back(std::move(v));
    }

    Py_DECREF(list);
    PyGILState_Release(gs);
    return (int)_cache.size();
  }

  Rml::DataVariable Child(void *, const Rml::DataAddressEntry &addr) override {
    const int index = addr.index;
    if (index < 0 || index >= (int)_cache.size()) {
      if (addr.name == "size") {
        _size_variant = (int)_cache.size();
        return Rml::DataVariable(&_scalar, &_size_variant);
      }
      rmlui_cat.warning() << "bind_list index " << index << " out of range\n";
      return Rml::DataVariable();
    }
    return Rml::DataVariable(&_scalar, &_cache[index]);
  }
};

} // anonymous namespace

/**
 * Binds a named list variable for use with data-for in RML templates.
 *
 * getter() must return a Python list (or tuple) of scalars: bool, int, float,
 * or str.  After mutating the underlying data, call dirty_variable(name) to
 * trigger a DOM re-evaluation of data-for expressions that reference it.
 *
 * Returns True on success, False if the constructor is invalid.
 */
bool RmlDataModel::
bind_list(const std::string &name, PyObject *getter) {
  if (!_constructor) {
    return false;
  }

  Py_XINCREF(getter);
  std::shared_ptr<PyObject> getter_ref(getter, py_decref_with_gil);

  // Push the definition into _custom_definitions before calling
  // BindCustomDataVariable so RmlUi never holds a pointer that isn't owned.
  auto owned = std::make_unique<PythonListDefinition>(std::move(getter_ref));
  auto *def = owned.get();
  _custom_definitions.push_back(std::move(owned));

  if (!_constructor.BindCustomDataVariable(name, Rml::DataVariable(def, nullptr))) {
    _custom_definitions.pop_back();
    return false;
  }
  return true;
}

/**
 * Binds a named event callback to a Python callable.
 *
 * callback()  — called with no arguments when the data-event fires.
 *
 * Returns True on success.
 */
bool RmlDataModel::
bind_event_callback(const std::string &name, PyObject *callback) {
  if (!_constructor) {
    return false;
  }

  Py_XINCREF(callback);
  std::shared_ptr<PyObject> cb_ref(callback, py_decref_with_gil);

  Rml::DataEventFunc fn = [cb_ref](Rml::DataModelHandle, Rml::Event &,
                                    const Rml::VariantList &) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject *result = PyObject_CallNoArgs(cb_ref.get());
    if (!result) {
      PyErr_Print();
    } else {
      Py_DECREF(result);
    }
    PyGILState_Release(gstate);
  };

  return _constructor.BindEventCallback(name, std::move(fn));
}

#endif  // HAVE_PYTHON && !CPPPARSER
