/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlContext.cxx
 * @author rdb
 * @date 2011-11-30
 */

#include "rmlContext.h"
#include "rmlDocument.h"
#include "rmlDataModel.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>

/**
 * Loads an RML document from the given path and returns a Python-accessible
 * wrapper.  Returns nullptr on failure.
 */
PT(RmlDocument) RmlContext::
load_document(const std::string &path) {
  Rml::ElementDocument *doc = _ctx->LoadDocument(path);
  if (doc == nullptr) {
    return nullptr;
  }
  return new RmlDocument(doc);
}

/**
 * Loads a font face from the given file path.  If fallback is true, the face
 * will be used as a fallback for characters not found in other fonts.
 */
bool RmlContext::
load_font_face(const std::string &path, bool fallback) {
  return Rml::LoadFontFace(path, fallback);
}

/**
 * Updates the RmlUi context, processing animations and layout.  This is
 * called automatically by RmlRegion::do_cull; only call manually when not
 * using RmlRegion.
 */
void RmlContext::
update() {
  _ctx->Update();
}

/**
 * Renders the RmlUi context.  This is called automatically by RmlRegion;
 * only call manually when not using RmlRegion.
 */
void RmlContext::
render() {
  _ctx->Render();
}

/**
 * Returns the width of the context in pixels.
 */
int RmlContext::
get_width() const {
  return _ctx->GetDimensions().x;
}

/**
 * Returns the height of the context in pixels.
 */
int RmlContext::
get_height() const {
  return _ctx->GetDimensions().y;
}

/**
 * Returns the name this context was created with.
 */
std::string RmlContext::
get_name() const {
  return _ctx->GetName();
}

/**
 * Creates a new named data model and returns a handle that can be used to
 * bind Python variables to it.  Returns nullptr if a model with that name
 * already exists.
 *
 * Elements reference the model with the attribute data-model="name".
 */
PT(RmlDataModel) RmlContext::
create_data_model(const std::string &name) {
  Rml::DataModelConstructor constructor = _ctx->CreateDataModel(name);
  if (!constructor) {
    return nullptr;
  }
  Rml::DataModelHandle handle = constructor.GetModelHandle();
  return new RmlDataModel(handle, constructor);
}

/**
 * Returns a handle to an existing data model, or nullptr if not found.
 * The returned handle can be used to bind additional variables or to mark
 * variables dirty.
 */
PT(RmlDataModel) RmlContext::
get_data_model(const std::string &name) {
  Rml::DataModelConstructor constructor = _ctx->GetDataModel(name);
  if (!constructor) {
    return nullptr;
  }
  Rml::DataModelHandle handle = constructor.GetModelHandle();
  return new RmlDataModel(handle, constructor);
}

/**
 * Removes the named data model.  All data views, controllers, and bindings
 * it contains are also removed.  Invalidates any existing RmlDataModel
 * handles pointing to the model.
 *
 * Returns true if the model was found and removed, false otherwise.
 */
bool RmlContext::
remove_data_model(const std::string &name) {
  return _ctx->RemoveDataModel(name);
}
