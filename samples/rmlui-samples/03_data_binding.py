"""
Panda3D RmlUi — Data Binding Sample
======================================
Demonstrates RmlUi DataModel binding from Python.

  Basics tab    — data-model "basics" with {{ }} templates, data-if, driven by
                  Python callables via ctx.create_data_model() + bind_func().
                  dirty_variable() triggers DOM re-evaluation.
  Events tab    — range slider polled each frame; mouse-click position from ev dict
  data-for tab  — bind_list() registers a Python list; data-for renders each item.
                  Add/Remove/Shuffle buttons mutate the list and call dirty_variable().
                  Also shows the static expandable tree via set_class("expanded", …).

Controls
--------
F1 / ` : toggle RmlUi debugger
Escape  : quit
"""

import os
import html
import random

from direct.showbase.ShowBase import ShowBase
from panda3d.core import loadPrcFileData, LVecBase4f
from panda3d.rmlui import RmlRegion, RmlInputHandler

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
loadPrcFileData("", f"model-path {ASSETS}")
loadPrcFileData("", "win-size 1000 680")
loadPrcFileData("", "window-title RmlUi — Data Binding Sample")

_WORDS = [
    "goblin", "troll", "dragon", "knight", "wizard",
    "archer", "rogue", "paladin", "ranger", "monk",
]


class DataBindingSample(ShowBase):
    def __init__(self):
        ShowBase.__init__(self)
        self.disableMouse()
        self.win.setClearColor(LVecBase4f(0.07, 0.08, 0.10, 1))

        # Python-side state — DataModel getters read from these
        self._animal    = "dog"
        self._show_text = True
        self._rating    = 50
        self._items     = ["goblin", "troll", "dragon"]
        self._tree_expanded = {
            "node-src":     True,
            "node-rmlui":   False,
            "node-samples": False,
        }

        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self.rml = RmlRegion.make("data-binding", self.win)
        self.rml.set_input_handler(self.input_handler)

        ctx = self.rml.get_context()
        for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                    "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf"):
            ctx.load_font_face(os.path.join(ASSETS, ttf))

        # --- Create DataModels before loading the document so that
        #     data-value / data-if / data-for expressions resolve on first layout. ---
        self._dm = ctx.create_data_model("basics")
        self._dm.bind_func("animal",    lambda: self._animal)
        self._dm.bind_func("show_text", lambda: self._show_text)

        self._dm_list = ctx.create_data_model("list-demo")
        self._dm_list.bind_list("items", lambda: self._items)
        self._dm_list.bind_func("count", lambda: len(self._items))

        self._doc = ctx.load_document(os.path.join(ASSETS, "data_binding.rml"))
        if not self._doc:
            raise RuntimeError("data_binding.rml not found")
        self._doc.show()
        self.rml.init_debugger()

        self._wire_events()
        self._refresh_tree()
        self.taskMgr.add(self._update, "db-update")

        self.accept("escape", self.userExit)
        def toggle_dbg():
            self.rml.set_debugger_visible(not self.rml.is_debugger_visible())
        self.accept("f1", toggle_dbg)
        self.accept("`", toggle_dbg)

    def _wire_events(self):
        def _wire(el_id, callback):
            el = self._doc.get_element_by_id(el_id)
            if el:
                el.add_event_listener("click", callback)

        # Basics tab
        for animal in ("dog", "cat", "narwhal"):
            _wire(f"btn-{animal}", lambda ev, a=animal: self._set_animal(a))
        _wire("btn-toggle-text", lambda ev: self._toggle_text())

        # Events tab
        _wire("mouse-box", self._on_mouse_click)

        # data-for tab — list mutation buttons
        _wire("btn-list-add", lambda ev: self._list_add())
        _wire("btn-list-remove", lambda ev: self._list_remove())
        _wire("btn-list-shuffle", lambda ev: self._list_shuffle())

        # data-for tab — tree toggle
        for node_id in self._tree_expanded:
            _wire(node_id, lambda ev, nid=node_id: self._toggle_tree_node(nid))

    # ── Basics tab ──────────────────────────────────────────────────

    def _set_animal(self, animal):
        self._animal = animal
        self._dm.dirty_variable("animal")

    def _toggle_text(self):
        self._show_text = not self._show_text
        self._dm.dirty_variable("show_text")

    # ── Events tab ──────────────────────────────────────────────────

    def _on_mouse_click(self, ev):
        mx = round(ev.mouse_x, 1)
        my = round(ev.mouse_y, 1)
        el = self._doc.get_element_by_id("mouse-positions")
        if el:
            el.set_inner_rml(html.escape(f"({mx}, {my}) px"))

    # ── data-for tab ────────────────────────────────────────────────

    def _list_dirty(self):
        self._dm_list.dirty_variable("items")
        self._dm_list.dirty_variable("count")

    def _list_add(self):
        self._items.append(random.choice(_WORDS))
        self._list_dirty()

    def _list_remove(self):
        if self._items:
            self._items.pop()
            self._list_dirty()

    def _list_shuffle(self):
        random.shuffle(self._items)
        self._list_dirty()

    def _toggle_tree_node(self, node_id):
        self._tree_expanded[node_id] = not self._tree_expanded[node_id]
        self._refresh_tree()

    def _refresh_tree(self):
        for node_id, expanded in self._tree_expanded.items():
            node = self._doc.get_element_by_id(node_id)
            if node:
                node.set_class("expanded", expanded)

    # ── Per-frame update ────────────────────────────────────────────

    def _update(self, task):
        slider = self._doc.get_element_by_id("rating-slider")
        if slider:
            val_str = slider.get_value()
            try:
                val = int(float(val_str))
            except ValueError:
                val = self._rating
            if val != self._rating:
                self._rating = val
                el = self._doc.get_element_by_id("rating-val")
                if el:
                    el.set_inner_rml(str(val))
                fb = self._doc.get_element_by_id("rating-feedback")
                if fb:
                    if val < 30:
                        msg = "Low rating — improvements needed!"
                    elif val < 70:
                        msg = "Decent rating."
                    else:
                        msg = "Great rating! Thanks!"
                    fb.set_inner_rml(html.escape(msg))
        return task.cont


DataBindingSample().run()
