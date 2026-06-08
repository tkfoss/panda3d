/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlDataModel.h
 * @author tkfoss
 * @date 2026-06-08
 */

#ifndef RML_DATA_MODEL_H
#define RML_DATA_MODEL_H

#include "config_rmlui.h"
#include "referenceCount.h"

#ifndef CPPPARSER
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Variant.h>
#include <string>
#include <functional>
#endif

/**
 * Handle to an existing RmlUi data model.  Obtained from
 * RmlContext::create_data_model() or RmlContext::get_data_model().
 * Use dirty_variable() / dirty_all() to notify RmlUi that Python-side
 * data has changed so the DOM re-evaluates data-value / data-if / data-for.
 */
class EXPCL_PANDARMUI RmlDataModel : public ReferenceCount {
PUBLISHED:
  bool is_valid() const;

  void dirty_variable(const std::string &name);
  void dirty_all();

#ifdef HAVE_PYTHON
  bool bind_func(const std::string &name,
                 PyObject *getter,
                 PyObject *setter = nullptr);

  bool bind_event_callback(const std::string &name, PyObject *callback);
#endif  // HAVE_PYTHON

public:
  RmlDataModel() = default;
#ifndef CPPPARSER
  explicit RmlDataModel(Rml::DataModelHandle handle,
                        Rml::DataModelConstructor constructor)
    : _valid(true), _handle(handle), _constructor(constructor) {}

private:
  bool _valid = false;
  Rml::DataModelHandle _handle;
  Rml::DataModelConstructor _constructor;
#endif
};

#endif
