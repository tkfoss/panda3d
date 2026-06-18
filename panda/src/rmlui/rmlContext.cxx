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
#include "rmlElement.h"
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
  nassertr(_ctx != nullptr, nullptr);
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
  nassertr(_ctx != nullptr, false);
  return Rml::LoadFontFace(path, fallback);
}

/**
 * Updates the RmlUi context, processing animations and layout.  This is
 * called automatically by RmlRegion::do_cull; only call manually when not
 * using RmlRegion.
 */
void RmlContext::
update() {
  nassertv(_ctx != nullptr);
  _ctx->Update();
}

/**
 * Renders the RmlUi context.  Only call this manually when not using
 * RmlRegion and you have set up a custom Rml::RenderInterface that does
 * not require an active CullTraverser.  Calling this while a RmlRegion
 * owns the context will crash because the render interface expects a live
 * CullTraverser (set during RmlRegion::do_cull).
 */
void RmlContext::
render() {
  nassertv(_ctx != nullptr);
  _ctx->Render();
}

/**
 * Returns the width of the context in pixels.
 */
int RmlContext::
get_width() const {
  nassertr(_ctx != nullptr, 0);
  return _ctx->GetDimensions().x;
}

/**
 * Returns the height of the context in pixels.
 */
int RmlContext::
get_height() const {
  nassertr(_ctx != nullptr, 0);
  return _ctx->GetDimensions().y;
}

/**
 * Returns the name this context was created with.
 */
std::string RmlContext::
get_name() const {
  nassertr(_ctx != nullptr, std::string());
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
  nassertr(_ctx != nullptr, nullptr);
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
  nassertr(_ctx != nullptr, nullptr);
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
  nassertr(_ctx != nullptr, false);
  return _ctx->RemoveDataModel(name);
}

/**
 * Returns true if the mouse cursor is currently over or interacting with any
 * element in this context.  Use to suppress game input while the UI is active.
 */
bool RmlContext::
is_mouse_interacting() const {
  nassertr(_ctx != nullptr, false);
  return _ctx->IsMouseInteracting();
}

/**
 * Returns the topmost element under the given screen-space point, or nullptr.
 * NOTE: The returned wrapper is non-owning.  Do not store it across a
 * ctx.update() call that may unload documents.
 */
PT(RmlElement) RmlContext::
get_element_at_point(float x, float y) const {
  nassertr(_ctx != nullptr, nullptr);
  Rml::Element *el = _ctx->GetElementAtPoint({x, y});
  return el ? new RmlElement(el) : nullptr;
}

/**
 * Returns the element currently under the mouse cursor, or nullptr.
 * NOTE: The returned wrapper is non-owning.  Do not store it across a
 * ctx.update() call that may unload documents.
 */
PT(RmlElement) RmlContext::
get_hover_element() const {
  nassertr(_ctx != nullptr, nullptr);
  Rml::Element *el = _ctx->GetHoverElement();
  return el ? new RmlElement(el) : nullptr;
}

/**
 * Returns the element that currently holds keyboard focus, or nullptr.
 * NOTE: The returned wrapper is non-owning.  Do not store it across a
 * ctx.update() call that may unload documents.
 */
PT(RmlElement) RmlContext::
get_focus_element() const {
  nassertr(_ctx != nullptr, nullptr);
  Rml::Element *el = _ctx->GetFocusElement();
  return el ? new RmlElement(el) : nullptr;
}

/**
 * Loads an RML document from an in-memory string.  source_url is used for
 * error reporting only; defaults to "[from memory]" if empty.
 */
PT(RmlDocument) RmlContext::
load_document_from_memory(const std::string &rml, const std::string &source_url) {
  nassertr(_ctx != nullptr, nullptr);
  Rml::ElementDocument *doc = _ctx->LoadDocumentFromMemory(
      rml, source_url.empty() ? "[from memory]" : source_url);
  return doc ? new RmlDocument(doc) : nullptr;
}

/**
 * Unloads a single document from this context.
 */
void RmlContext::
unload_document(RmlDocument *doc) {
  nassertv(_ctx != nullptr && doc != nullptr);
  _ctx->UnloadDocument(doc->get_raw());
}

/**
 * Unloads all documents from this context.
 */
void RmlContext::
unload_all_documents() {
  nassertv(_ctx != nullptr);
  _ctx->UnloadAllDocuments();
}

/**
 * Returns the number of documents currently loaded in this context.
 */
int RmlContext::
get_num_documents() const {
  nassertr(_ctx != nullptr, 0);
  return _ctx->GetNumDocuments();
}

/**
 * Sets the density-independent pixel ratio for HiDPI scaling.
 */
void RmlContext::
set_density_independent_pixel_ratio(float ratio) {
  nassertv(_ctx != nullptr);
  _ctx->SetDensityIndependentPixelRatio(ratio);
}

/**
 * Enables or disables the built-in RmlUi software mouse cursor.
 */
void RmlContext::
enable_mouse_cursor(bool enable) {
  nassertv(_ctx != nullptr);
  _ctx->EnableMouseCursor(enable);
}
