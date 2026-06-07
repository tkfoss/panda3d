/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlRegion.cxx
 * @author rdb
 * @date 2011-11-30
 */

#include "rmlRegion.h"
#include "rmlContext.h"
#include "graphicsOutput.h"
#include "orthographicLens.h"
#include "pStatTimer.h"

#ifndef CPPPARSER
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Context.h>
#ifdef HAVE_RMLUI_DEBUGGER
#include <RmlUi/Debugger.h>
#endif
#endif

TypeHandle RmlRegion::_type_handle;

PT(RmlContext) RmlRegion::
get_context() const {
  if (_context == nullptr) {
    return nullptr;
  }
  return new RmlContext(_context);
}

/**
 * In RmlUi v6 each context has its own render interface, passed at creation
 * time.  The interface lifetime must exceed the context's.
 */
RmlRegion::
RmlRegion(GraphicsOutput *window, const LVecBase4 &dr_dimensions,
          const std::string &context_name)
  : DisplayRegion(window, dr_dimensions)
{
  int pl, pr, pb, pt;
  get_pixels(pl, pr, pb, pt);
  Rml::Vector2i dimensions(pr - pl, pt - pb);

  if (rmlui_cat.is_debug()) {
    rmlui_cat.debug()
      << "Creating RmlUi context '" << context_name
      << "' at (" << dimensions.x << ", " << dimensions.y << ")\n";
  }

  _interface.init(window);
  _context = Rml::CreateContext(context_name, dimensions, &_interface);
  nassertv(_context != nullptr);

  _lens = new OrthographicLens;
  _lens->set_film_size(dimensions.x, -dimensions.y);
  _lens->set_film_offset(dimensions.x * 0.5f, dimensions.y * 0.5f);
  _lens->set_near_far(-1, 1);

  PT(Camera) cam = new Camera(context_name, _lens);
  set_camera(NodePath(cam));
}

RmlRegion::
~RmlRegion() {
  if (_context != nullptr) {
    Rml::RemoveContext(_context->GetName());
    _context = nullptr;
  }
}

/**
 * Cull callback: resize context if window dimensions changed, pump input,
 * then call Render() which dispatches into RmlRenderInterface.
 */
void RmlRegion::
do_cull(CullHandler *cull_handler, SceneSetup *scene_setup,
        GraphicsStateGuardian *gsg, Thread *current_thread) {

  PStatTimer timer(get_cull_region_pcollector(), current_thread);

  int pl, pr, pb, pt;
  get_pixels(pl, pr, pb, pt);
  Rml::Vector2i dimensions(pr - pl, pt - pb);

  if (_context->GetDimensions() != dimensions) {
    if (rmlui_cat.is_debug()) {
      rmlui_cat.debug()
        << "Resizing context to (" << dimensions.x << ", " << dimensions.y << ")\n";
    }
    _context->SetDimensions(dimensions);
    _lens->set_film_size(dimensions.x, -dimensions.y);
    _lens->set_film_offset(dimensions.x * 0.5f, dimensions.y * 0.5f);
  }

  if (_input_handler != nullptr) {
    _input_handler->update_context(_context, pl, pb);
  } else {
    _context->Update();
  }

  CullTraverser *trav = get_cull_traverser();
  trav->set_cull_handler(cull_handler);
  trav->set_scene(scene_setup, gsg, get_incomplete_render());
  trav->set_view_frustum(nullptr);

  _interface.render(_context, trav, gsg, current_thread);

  trav->end_traverse();
}

bool RmlRegion::
init_debugger() {
#ifdef HAVE_RMLUI_DEBUGGER
  return Rml::Debugger::Initialise(_context);
#else
  return false;
#endif
}

void RmlRegion::
set_debugger_visible(bool visible) {
#ifdef HAVE_RMLUI_DEBUGGER
  Rml::Debugger::SetVisible(visible);
#endif
}

bool RmlRegion::
is_debugger_visible() const {
#ifdef HAVE_RMLUI_DEBUGGER
  return Rml::Debugger::IsVisible();
#else
  return false;
#endif
}
