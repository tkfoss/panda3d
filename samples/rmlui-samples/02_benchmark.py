"""
Panda3D RmlUi — Benchmark Sample
===================================
DOM stress-test: add, remove, and toggle batches of elements to measure
how many the renderer can handle at interactive frame rates.

Buttons
-------
Add 50      : append 50 new box elements via set_inner_rml
Remove 50   : rebuild the grid without the last 50 elements
Clear       : empty the grid entirely
Toggle random : toggle the "lit" CSS class on ~10% of boxes

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
loadPrcFileData("", "win-size 1100 720")
loadPrcFileData("", "window-title RmlUi — Benchmark Sample")

BATCH = 50


class BenchmarkSample(ShowBase):
    def __init__(self):
        ShowBase.__init__(self)
        self.disableMouse()
        self.win.setClearColor(LVecBase4f(0.06, 0.06, 0.08, 1))

        self._boxes   = []
        self._counter = 0
        self._lit     = set()

        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self.rml = RmlRegion.make("benchmark", self.win)
        self.rml.set_input_handler(self.input_handler)

        ctx = self.rml.get_context()
        for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                    "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf"):
            ctx.load_font_face(os.path.join(ASSETS, ttf))

        self._doc = ctx.load_document(os.path.join(ASSETS, "benchmark.rml"))
        if not self._doc:
            raise RuntimeError("benchmark.rml not found")
        self._doc.show()
        self.rml.init_debugger()

        self._grid      = self._doc.get_element_by_id("grid")
        self._el_count  = self._doc.get_element_by_id("el-count")
        self._lit_count = self._doc.get_element_by_id("lit-count")
        self._frame_el  = self._doc.get_element_by_id("frame-count")
        self._fps_el    = self._doc.get_element_by_id("fps-val")

        for btn_id, handler in (
            ("btn-add",    self._add_batch),
            ("btn-remove", self._remove_batch),
            ("btn-clear",  self._clear_all),
            ("btn-toggle", self._toggle_random),
        ):
            el = self._doc.get_element_by_id(btn_id)
            if el:
                el.add_event_listener("click", lambda ev, h=handler: h())

        self._add_batch()
        self.taskMgr.add(self._update, "benchmark-update")

        self.accept("escape", self.userExit)
        def toggle_dbg():
            self.rml.set_debugger_visible(not self.rml.is_debugger_visible())
        self.accept("f1", toggle_dbg)
        self.accept("`", toggle_dbg)

    def _add_batch(self):
        if not self._grid:
            return
        markup = ""
        for _ in range(BATCH):
            bid = f"box-{self._counter}"
            self._counter += 1
            markup += f'<div class="box" id="{html.escape(bid)}">{self._counter % 100}</div>'
        self._grid.set_inner_rml(self._grid.get_inner_rml() + markup)
        for i in range(self._counter - BATCH, self._counter):
            el = self._doc.get_element_by_id(f"box-{i}")
            if el:
                self._boxes.append((f"box-{i}", el))
        self._update_counts()

    def _remove_batch(self):
        if not self._boxes:
            return
        keep   = self._boxes[:-BATCH]
        remove = self._boxes[-BATCH:]
        markup = "".join(
            f'<div class="box{" lit" if bid in self._lit else ""}" id="{html.escape(bid)}">'
            f'{int(bid.split("-")[1]) % 100}</div>'
            for bid, _ in keep
        )
        if self._grid:
            self._grid.set_inner_rml(markup)
        for bid, _ in remove:
            self._lit.discard(bid)
        self._boxes = []
        for bid, _ in keep:
            el = self._doc.get_element_by_id(bid)
            if el:
                self._boxes.append((bid, el))
        self._update_counts()

    def _clear_all(self):
        self._boxes.clear()
        self._lit.clear()
        if self._grid:
            self._grid.set_inner_rml("")
        self._update_counts()

    def _toggle_random(self):
        if not self._boxes:
            return
        for bid, el in random.sample(self._boxes, max(1, len(self._boxes) // 10)):
            is_lit = bid in self._lit
            el.set_class("lit", not is_lit)
            if is_lit:
                self._lit.discard(bid)
            else:
                self._lit.add(bid)
        self._update_counts()

    def _update_counts(self):
        if self._el_count:
            self._el_count.set_inner_rml(str(len(self._boxes)))
        if self._lit_count:
            self._lit_count.set_inner_rml(str(len(self._lit)))

    def _update(self, task):
        if self._frame_el:
            self._frame_el.set_inner_rml(str(globalClock.getFrameCount()))
        if self._fps_el:
            self._fps_el.set_inner_rml(f"{globalClock.getAverageFrameRate():.1f}")
        return task.cont


BenchmarkSample().run()
