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
        self.rml.setInputHandler(self.input_handler)

        ctx = self.rml.getContext()
        for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                    "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf"):
            ctx.loadFontFace(os.path.join(ASSETS, ttf))

        self._doc = ctx.loadDocument(os.path.join(ASSETS, "benchmark.rml"))
        if not self._doc:
            raise RuntimeError("benchmark.rml not found")
        self._doc.show()
        self.rml.initDebugger()

        self._grid      = self._doc.getElementById("grid")
        self._el_count  = self._doc.getElementById("el-count")
        self._lit_count = self._doc.getElementById("lit-count")
        self._frame_el  = self._doc.getElementById("frame-count")
        self._fps_el    = self._doc.getElementById("fps-val")

        for btn_id, handler in (
            ("btn-add",    self._add_batch),
            ("btn-remove", self._remove_batch),
            ("btn-clear",  self._clear_all),
            ("btn-toggle", self._toggle_random),
        ):
            el = self._doc.getElementById(btn_id)
            if el:
                ev = f"rmlui-{btn_id}"
                el.addEventListener("click", ev)
                self.accept(ev, handler)

        self._add_batch()
        self.taskMgr.add(self._update, "benchmark-update")

        self.accept("escape", self.userExit)
        def toggle_dbg():
            self.rml.setDebuggerVisible(not self.rml.isDebuggerVisible())
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
        self._grid.setInnerRml(self._grid.getInnerRml() + markup)
        for i in range(self._counter - BATCH, self._counter):
            el = self._doc.getElementById(f"box-{i}")
            if el:
                self._boxes.append((f"box-{i}", el))
        self._update_counts()

    def _remove_batch(self):
        if not self._boxes:
            return
        keep   = self._boxes[:-BATCH]
        remove = self._boxes[-BATCH:]
        markup = "".join(
            f'<div class="box{"  lit" if bid in self._lit else ""}" id="{html.escape(bid)}">'
            f'{int(bid.split("-")[1]) % 100}</div>'
            for bid, _ in keep
        )
        if self._grid:
            self._grid.setInnerRml(markup)
        for bid, _ in remove:
            self._lit.discard(bid)
        self._boxes = []
        for bid, _ in keep:
            el = self._doc.getElementById(bid)
            if el:
                self._boxes.append((bid, el))
        self._update_counts()

    def _clear_all(self):
        self._boxes.clear()
        self._lit.clear()
        if self._grid:
            self._grid.setInnerRml("")
        self._update_counts()

    def _toggle_random(self):
        if not self._boxes:
            return
        for bid, el in random.sample(self._boxes, max(1, len(self._boxes) // 10)):
            is_lit = bid in self._lit
            el.setClass("lit", not is_lit)
            if is_lit:
                self._lit.discard(bid)
            else:
                self._lit.add(bid)
        self._update_counts()

    def _update_counts(self):
        if self._el_count:
            self._el_count.setInnerRml(str(len(self._boxes)))
        if self._lit_count:
            self._lit_count.setInnerRml(str(len(self._lit)))

    def _update(self, task):
        if self._frame_el:
            self._frame_el.setInnerRml(str(globalClock.getFrameCount()))
        if self._fps_el:
            self._fps_el.setInnerRml(f"{globalClock.getAverageFrameRate():.1f}")
        return task.cont


BenchmarkSample().run()
