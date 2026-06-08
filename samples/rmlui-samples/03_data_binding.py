"""
Panda3D RmlUi — Data Binding Sample
======================================
Demonstrates RmlUi DataModel binding from Python.

  Basics tab   — data-model "basics" with data-value / data-if driven by
                 Python callables via ctx.create_data_model() + bind_func().
                 dirty_variable() / dirty_all() trigger DOM re-evaluation.
  Events tab   — range slider polled each frame; mouse-click position tracker
  Tree View tab— expandable tree toggled with set_class("expanded", …)

Controls
--------
F1 / ` : toggle RmlUi debugger
Escape  : quit
"""

import os
import html

from direct.showbase.ShowBase import ShowBase
from panda3d.core import loadPrcFileData, LVecBase4f
from panda3d.rmlui import RmlRegion, RmlInputHandler

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
loadPrcFileData("", f"model-path {ASSETS}")
loadPrcFileData("", "win-size 1000 680")
loadPrcFileData("", "window-title RmlUi — Data Binding Sample")


class DataBindingSample(ShowBase):
    def __init__(self):
        ShowBase.__init__(self)
        self.disableMouse()
        self.win.setClearColor(LVecBase4f(0.07, 0.08, 0.10, 1))

        # Python-side state — the DataModel getters read from these
        self._animal    = "dog"
        self._show_text = True
        self._rating    = 50
        self._tree_expanded = {
            "node-src":     True,
            "node-rmlui":   False,
            "node-samples": False,
        }

        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self.rml = RmlRegion.make("data-binding", self.win)
        self.rml.setInputHandler(self.input_handler)

        ctx = self.rml.getContext()
        for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                    "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf"):
            ctx.loadFontFace(os.path.join(ASSETS, ttf))

        # --- Create DataModel before loading the document so that
        #     data-value / data-if expressions resolve on first layout. ---
        self._dm = ctx.createDataModel("basics")
        self._dm.bind_func("animal",    lambda: self._animal)
        self._dm.bind_func("show_text", lambda: self._show_text)

        self._doc = ctx.loadDocument(os.path.join(ASSETS, "data_binding.rml"))
        if not self._doc:
            raise RuntimeError("data_binding.rml not found")
        self._doc.show()
        self.rml.initDebugger()

        self._wire_events()
        self._refresh_tree()
        self.taskMgr.add(self._update, "db-update")

        self.accept("escape", self.userExit)
        def toggle_dbg():
            self.rml.setDebuggerVisible(not self.rml.isDebuggerVisible())
        self.accept("f1", toggle_dbg)
        self.accept("`", toggle_dbg)

    def _wire_events(self):
        for animal in ("dog", "cat", "narwhal"):
            btn = self._doc.getElementById(f"btn-{animal}")
            if btn:
                ev = f"rmlui-animal-{animal}"
                btn.addEventListener("click", ev)
                self.accept(ev, self._set_animal, [animal])

        btn_text = self._doc.getElementById("btn-toggle-text")
        if btn_text:
            btn_text.addEventListener("click", "rmlui-toggle-text")
            self.accept("rmlui-toggle-text", self._toggle_text)

        mouse_box = self._doc.getElementById("mouse-box")
        if mouse_box:
            mouse_box.addEventListener("click", "rmlui-mouse-click")
            self.accept("rmlui-mouse-click", self._on_mouse_click)

        for node_id in self._tree_expanded:
            node = self._doc.getElementById(node_id)
            if node:
                ev = f"rmlui-tree-{node_id}"
                node.addEventListener("click", ev)
                self.accept(ev, self._toggle_tree_node, [node_id])

    def _set_animal(self, animal):
        self._animal = animal
        # Tell RmlUi that "animal" changed; DOM re-evaluates data-value="animal"
        self._dm.dirty_variable("animal")

    def _toggle_text(self):
        self._show_text = not self._show_text
        self._dm.dirty_variable("show_text")

    def _on_mouse_click(self):
        if base.mouseWatcherNode.hasMouse():
            mx = round(base.mouseWatcherNode.getMouseX(), 3)
            my = round(base.mouseWatcherNode.getMouseY(), 3)
            el = self._doc.getElementById("mouse-positions")
            if el:
                el.setInnerRml(html.escape(f"({mx}, {my})"))

    def _toggle_tree_node(self, node_id):
        self._tree_expanded[node_id] = not self._tree_expanded[node_id]
        self._refresh_tree()

    def _refresh_tree(self):
        for node_id, expanded in self._tree_expanded.items():
            node = self._doc.getElementById(node_id)
            if node:
                node.setClass("expanded", expanded)

    def _update(self, task):
        slider = self._doc.getElementById("rating-slider")
        if slider:
            val_str = slider.getValue()
            try:
                val = int(float(val_str))
            except ValueError:
                val = self._rating
            if val != self._rating:
                self._rating = val
                el = self._doc.getElementById("rating-val")
                if el:
                    el.setInnerRml(str(val))
                fb = self._doc.getElementById("rating-feedback")
                if fb:
                    if val < 30:
                        msg = "Low rating — improvements needed!"
                    elif val < 70:
                        msg = "Decent rating."
                    else:
                        msg = "Great rating! Thanks!"
                    fb.setInnerRml(html.escape(msg))
        return task.cont


DataBindingSample().run()
