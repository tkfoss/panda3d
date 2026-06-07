/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file rmlInputHandler.cxx
 * @author rdb
 * @date 2011-12-20
 */

#include "rmlInputHandler.h"
#include "buttonEventList.h"
#include "dataGraphTraverser.h"
#include "linmath_events.h"
#include "keyboardButton.h"
#include "mouseButton.h"

#ifndef CPPPARSER
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Context.h>
using namespace Rml::Input;
#endif

TypeHandle RmlInputHandler::_type_handle;

RmlInputHandler::
RmlInputHandler(const std::string &name) :
  DataNode(name),
  _mouse_xy(-1)
{
  _pixel_xy_input = define_input("pixel_xy", EventStoreVec2::get_class_type());
  _button_events_input = define_input("button_events", ButtonEventList::get_class_type());
}

RmlInputHandler::
~RmlInputHandler() {
}

/**
 * Maps a Panda ButtonHandle to an RmlUi KeyIdentifier, or KI_UNKNOWN.
 */
int RmlInputHandler::
get_rml_key(const ButtonHandle handle) {
  static pmap<int, int> keymap;
  if (!keymap.empty()) {
    auto it = keymap.find(handle.get_index());
    return (it != keymap.end()) ? it->second : 0;
  }

  keymap[KeyboardButton::space().get_index()]         = KI_SPACE;
  keymap[KeyboardButton::backspace().get_index()]     = KI_BACK;
  keymap[KeyboardButton::tab().get_index()]           = KI_TAB;
  keymap[KeyboardButton::enter().get_index()]         = KI_RETURN;
  keymap[KeyboardButton::escape().get_index()]        = KI_ESCAPE;
  keymap[KeyboardButton::end().get_index()]           = KI_END;
  keymap[KeyboardButton::home().get_index()]          = KI_HOME;
  keymap[KeyboardButton::left().get_index()]          = KI_LEFT;
  keymap[KeyboardButton::up().get_index()]            = KI_UP;
  keymap[KeyboardButton::right().get_index()]         = KI_RIGHT;
  keymap[KeyboardButton::down().get_index()]          = KI_DOWN;
  keymap[KeyboardButton::insert().get_index()]        = KI_INSERT;
  keymap[KeyboardButton::del().get_index()]           = KI_DELETE;
  keymap[KeyboardButton::caps_lock().get_index()]     = KI_CAPITAL;
  keymap[KeyboardButton::f1().get_index()]            = KI_F1;
  keymap[KeyboardButton::f2().get_index()]            = KI_F2;
  keymap[KeyboardButton::f3().get_index()]            = KI_F3;
  keymap[KeyboardButton::f4().get_index()]            = KI_F4;
  keymap[KeyboardButton::f5().get_index()]            = KI_F5;
  keymap[KeyboardButton::f6().get_index()]            = KI_F6;
  keymap[KeyboardButton::f7().get_index()]            = KI_F7;
  keymap[KeyboardButton::f8().get_index()]            = KI_F8;
  keymap[KeyboardButton::f9().get_index()]            = KI_F9;
  keymap[KeyboardButton::f10().get_index()]           = KI_F10;
  keymap[KeyboardButton::f11().get_index()]           = KI_F11;
  keymap[KeyboardButton::f12().get_index()]           = KI_F12;
  keymap[KeyboardButton::f13().get_index()]           = KI_F13;
  keymap[KeyboardButton::f14().get_index()]           = KI_F14;
  keymap[KeyboardButton::f15().get_index()]           = KI_F15;
  keymap[KeyboardButton::f16().get_index()]           = KI_F16;
  keymap[KeyboardButton::help().get_index()]          = KI_HELP;
  keymap[KeyboardButton::lcontrol().get_index()]      = KI_LCONTROL;
  keymap[KeyboardButton::lshift().get_index()]        = KI_LSHIFT;
  keymap[KeyboardButton::num_lock().get_index()]      = KI_NUMLOCK;
  keymap[KeyboardButton::page_down().get_index()]     = KI_NEXT;
  keymap[KeyboardButton::page_up().get_index()]       = KI_PRIOR;
  keymap[KeyboardButton::pause().get_index()]         = KI_PAUSE;
  keymap[KeyboardButton::print_screen().get_index()]  = KI_SNAPSHOT;
  keymap[KeyboardButton::rcontrol().get_index()]      = KI_RCONTROL;
  keymap[KeyboardButton::rshift().get_index()]        = KI_RSHIFT;
  keymap[KeyboardButton::scroll_lock().get_index()]   = KI_SCROLL;

  keymap[KeyboardButton::ascii_key(';').get_index()]  = KI_OEM_1;
  keymap[KeyboardButton::ascii_key('=').get_index()]  = KI_OEM_PLUS;
  keymap[KeyboardButton::ascii_key(',').get_index()]  = KI_OEM_COMMA;
  keymap[KeyboardButton::ascii_key('-').get_index()]  = KI_OEM_MINUS;
  keymap[KeyboardButton::ascii_key('.').get_index()]  = KI_OEM_PERIOD;
  keymap[KeyboardButton::ascii_key('/').get_index()]  = KI_OEM_2;
  keymap[KeyboardButton::ascii_key('`').get_index()]  = KI_OEM_3;
  keymap[KeyboardButton::ascii_key('[').get_index()]  = KI_OEM_4;
  keymap[KeyboardButton::ascii_key('\\').get_index()] = KI_OEM_5;
  keymap[KeyboardButton::ascii_key(']').get_index()]  = KI_OEM_6;
  keymap[KeyboardButton::ascii_key('<').get_index()]  = KI_OEM_102;

  for (char c = 'a'; c <= 'z'; ++c) {
    keymap[KeyboardButton::ascii_key(c).get_index()] = (c - 'a') + KI_A;
  }
  for (char c = '0'; c <= '9'; ++c) {
    keymap[KeyboardButton::ascii_key(c).get_index()] = (c - '0') + KI_0;
  }

  auto it = keymap.find(handle.get_index());
  return (it != keymap.end()) ? it->second : 0;
}

void RmlInputHandler::
do_transmit_data(DataGraphTraverser *, const DataNodeTransmit &input,
                 DataNodeTransmit &) {
  MutexHolder holder(_lock);

  if (input.has_data(_pixel_xy_input)) {
    const EventStoreVec2 *pixel_xy;
    DCAST_INTO_V(pixel_xy, input.get_data(_pixel_xy_input).get_ptr());
    LVecBase2 p = pixel_xy->get_value();
    if (p != _mouse_xy) {
      _mouse_xy_changed = true;
      _mouse_xy = p;
    }
  }

  if (input.has_data(_button_events_input)) {
    const ButtonEventList *evts;
    DCAST_INTO_V(evts, input.get_data(_button_events_input).get_ptr());
    int n = evts->get_num_events();
    for (int i = 0; i < n; ++i) {
      const ButtonEvent &be = evts->get_event(i);
      int rml_key = KI_UNKNOWN;

      switch (be._type) {
      case ButtonEvent::T_down:
        if (be._button == KeyboardButton::control()) {
          _modifiers |= KM_CTRL;
        } else if (be._button == KeyboardButton::shift()) {
          _modifiers |= KM_SHIFT;
        } else if (be._button == KeyboardButton::alt()) {
          _modifiers |= KM_ALT;
        } else if (be._button == KeyboardButton::meta()) {
          _modifiers |= KM_META;
        } else if (be._button == MouseButton::wheel_up()) {
          _wheel_delta -= 1.0f;
        } else if (be._button == MouseButton::wheel_down()) {
          _wheel_delta += 1.0f;
        } else if (be._button == MouseButton::one()) {
          _mouse_buttons[0] = true;
        } else if (be._button == MouseButton::two()) {
          _mouse_buttons[1] = true;
        } else if (be._button == MouseButton::three()) {
          _mouse_buttons[2] = true;
        } else if (be._button == MouseButton::four()) {
          _mouse_buttons[3] = true;
        } else if (be._button == MouseButton::five()) {
          _mouse_buttons[4] = true;
        }
        rml_key = get_rml_key(be._button);
        if (rml_key != KI_UNKNOWN) _keys[rml_key] = true;
        break;

      case ButtonEvent::T_repeat:
        rml_key = get_rml_key(be._button);
        if (rml_key != KI_UNKNOWN) _repeated_keys.push_back(rml_key);
        break;

      case ButtonEvent::T_up:
        if (be._button == KeyboardButton::control()) {
          _modifiers &= ~KM_CTRL;
        } else if (be._button == KeyboardButton::shift()) {
          _modifiers &= ~KM_SHIFT;
        } else if (be._button == KeyboardButton::alt()) {
          _modifiers &= ~KM_ALT;
        } else if (be._button == KeyboardButton::meta()) {
          _modifiers &= ~KM_META;
        } else if (be._button == MouseButton::one()) {
          _mouse_buttons[0] = false;
        } else if (be._button == MouseButton::two()) {
          _mouse_buttons[1] = false;
        } else if (be._button == MouseButton::three()) {
          _mouse_buttons[2] = false;
        } else if (be._button == MouseButton::four()) {
          _mouse_buttons[3] = false;
        } else if (be._button == MouseButton::five()) {
          _mouse_buttons[4] = false;
        }
        rml_key = get_rml_key(be._button);
        if (rml_key != KI_UNKNOWN) _keys[rml_key] = false;
        break;

      case ButtonEvent::T_keystroke:
        // Filter control characters; pass everything else as Unicode
        if (be._keycode > 0x1F &&
            (be._keycode < 0x7F || be._keycode > 0x9F)) {
          _text_input.push_back(be._keycode);
        }
        break;

      default:
        break;
      }
    }
  }
}

/**
 * Flush accumulated input into the RmlUi context, then call Update().
 * Called by RmlRegion::do_cull before calling Render().
 */
void RmlInputHandler::
update_context(Rml::Context *context, int xoffs, int yoffs) {
  MutexHolder holder(_lock);

  for (auto &[key, down] : _keys) {
    if (down) {
      context->ProcessKeyDown((KeyIdentifier) key, _modifiers);
    } else {
      context->ProcessKeyUp((KeyIdentifier) key, _modifiers);
    }
  }
  _keys.clear();

  for (int key : _repeated_keys) {
    context->ProcessKeyUp((KeyIdentifier) key, _modifiers);
    context->ProcessKeyDown((KeyIdentifier) key, _modifiers);
  }
  _repeated_keys.clear();

  for (int cp : _text_input) {
    context->ProcessTextInput((Rml::Character) cp);
  }
  _text_input.clear();

  if (_mouse_xy_changed) {
    _mouse_xy_changed = false;
    context->ProcessMouseMove(
      (int) _mouse_xy.get_x() - xoffs,
      (int) _mouse_xy.get_y() - yoffs,
      _modifiers);
  }

  for (auto &[btn, down] : _mouse_buttons) {
    if (down) {
      rmlui_cat.debug()
        << "MouseButtonDown btn=" << btn
        << " at rml=(" << (int)_mouse_xy.get_x() - xoffs
        << ", " << (int)_mouse_xy.get_y() - yoffs << ")\n";
      context->ProcessMouseButtonDown(btn, _modifiers);
    } else {
      context->ProcessMouseButtonUp(btn, _modifiers);
    }
  }
  _mouse_buttons.clear();

  if (_wheel_delta != 0.0f) {
    context->ProcessMouseWheel(_wheel_delta, _modifiers);
    _wheel_delta = 0.0f;
  }

  context->Update();
}
