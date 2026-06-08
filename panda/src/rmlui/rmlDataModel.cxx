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

// ---------------------------------------------------------------------------
// RmlDataModel (non-Python methods)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Helpers: convert between Rml::Variant and Python objects
// ---------------------------------------------------------------------------

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
  // other types produce an empty/unchanged variant
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
  // This ensures Py_XINCREF/Py_XDECREF below are always symmetric.
  if (setter == Py_None) {
    setter = nullptr;
  }

  // Increment ref-counts so the callables stay alive for the lifetime of the
  // data model (the lambdas below capture the raw pointers).
  Py_XINCREF(getter);
  Py_XINCREF(setter);

  Rml::DataGetFunc get_fn = [getter](Rml::Variant &out) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject *result = PyObject_CallNoArgs(getter);
    if (!result) {
      PyErr_Print();
    } else {
      python_to_variant(result, out);
      Py_DECREF(result);
    }
    PyGILState_Release(gstate);
  };

  Rml::DataSetFunc set_fn;
  if (setter != nullptr) {
    set_fn = [setter](const Rml::Variant &v) {
      PyGILState_STATE gstate = PyGILState_Ensure();
      PyObject *arg = variant_to_python(v);
      PyObject *result = PyObject_CallOneArg(setter, arg);
      Py_DECREF(arg);
      if (!result) {
        PyErr_Print();
      } else {
        Py_DECREF(result);
      }
      PyGILState_Release(gstate);
    };
  }

  bool ok = _constructor.BindFunc(name, std::move(get_fn), std::move(set_fn));

  // If BindFunc failed the lambdas were not stored; release the refs we took.
  if (!ok) {
    Py_XDECREF(getter);
    Py_XDECREF(setter);
  }

  return ok;
}

/**
 * Binds a named event callback to a Python callable.
 *
 * callback(handle, event_type, args)  — called when a data-event fires.
 *   Currently called with no arguments (future extension).
 *
 * Returns True on success.
 */
bool RmlDataModel::
bind_event_callback(const std::string &name, PyObject *callback) {
  if (!_constructor) {
    return false;
  }

  Py_XINCREF(callback);

  Rml::DataEventFunc fn = [callback](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject *result = PyObject_CallNoArgs(callback);
    if (!result) {
      PyErr_Print();
    } else {
      Py_DECREF(result);
    }
    PyGILState_Release(gstate);
  };

  bool ok = _constructor.BindEventCallback(name, std::move(fn));
  if (!ok) {
    Py_XDECREF(callback);
  }
  return ok;
}

#endif  // HAVE_PYTHON && !CPPPARSER
