"""
Panda3D RmlUi — Feature Showcase
=================================
Seven-tab reference for the CSS / RmlUi feature set:

  Welcome      — source guide
  Font & Text  — font-effect: glow, outline, shadow, blur; emoji; sizes
  Animations   — @keyframes cube, transition: :hover, easing gallery
  Decorators   — gradients, tiled-box, spritesheet, image-color, fit modes
  Transforms   — translate, rotate, scale, skew, 3D perspective
  Filters      — filter: blur, grayscale, sepia, brightness, …
  Forms        — all input types; submit wired to Panda3D Messenger
  Live/Python  — per-frame DOM updates via set_inner_rml / set_attribute
  Data Binding — see 03_data_binding.py for DataModel / bind_func usage

Controls
--------
F1 / ` : toggle RmlUi debugger
Escape  : quit
"""

import os
import math
import html

from direct.showbase.ShowBase import ShowBase
from panda3d.core import loadPrcFileData, LVecBase4f
from panda3d.rmlui import RmlRegion, RmlInputHandler

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
loadPrcFileData("", f"model-path {ASSETS}")
loadPrcFileData("", "win-size 1280 800")
loadPrcFileData("", "window-title RmlUi — Feature Showcase")


def _hsv_to_rgb(h, s, v):
    h = h % 360
    i = int(h / 60)
    f = h / 60 - i
    p, q, t_ = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    return [(v, t_, p), (q, v, p), (p, v, t_),
            (p, q, v), (t_, p, v), (v, p, q)][i]


class FeatureShowcase(ShowBase):
    def __init__(self):
        ShowBase.__init__(self)
        self.disableMouse()
        self.win.setClearColor(LVecBase4f(0.08, 0.09, 0.11, 1))

        self._ping_count = 0
        self._toggle_state = [False, False, False, False]

        self._setup_rmlui()
        self._setup_input()
        self.taskMgr.add(self._update, "rmlui-update")

    def _setup_rmlui(self):
        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self.rml = RmlRegion.make("features", self.win)
        self.rml.setInputHandler(self.input_handler)

        ctx = self.rml.getContext()
        for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                    "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf"):
            ctx.loadFontFace(os.path.join(ASSETS, ttf))

        self._doc = ctx.loadDocument(os.path.join(ASSETS, "features.rml"))
        if not self._doc:
            raise RuntimeError("features.rml not found")
        self._doc.show()
        self.rml.initDebugger()

        ids = ["fps", "frame-count", "time-val", "sin-val",
               "progress-fill", "color-swatch",
               "ping-count", "form-output",
               "toggle-status",
               "tog0", "tog1", "tog2", "tog3"]
        self._el = {k: self._doc.getElementById(k) for k in ids}

        btn = self._doc.getElementById("ping-btn")
        if btn:
            btn.addEventListener("click", "rmlui-ping")
            self.accept("rmlui-ping", self._on_ping)

        for i in range(4):
            el = self._el.get(f"tog{i}")
            if el:
                ev = f"rmlui-tog{i}"
                el.addEventListener("click", ev)
                self.accept(ev, self._on_toggle, [i])

        form = self._doc.getElementById("demo-form")
        if form:
            form.addEventListener("submit", "rmlui-submit")
            self.accept("rmlui-submit", self._on_form_submit)
        btn = self._doc.getElementById("submit-btn")
        if btn:
            btn.addEventListener("click", "rmlui-submit-btn")
            self.accept("rmlui-submit-btn", self._on_form_submit)

    def _setup_input(self):
        self.accept("escape", self.userExit)
        def toggle_dbg():
            self.rml.setDebuggerVisible(not self.rml.isDebuggerVisible())
        self.accept("f1", toggle_dbg)
        self.accept("`", toggle_dbg)

    def _on_ping(self):
        self._ping_count += 1
        self._set("ping-count", str(self._ping_count))

    def _on_toggle(self, idx):
        self._toggle_state[idx] = not self._toggle_state[idx]
        el = self._el.get(f"tog{idx}")
        if el:
            el.setClass("active", self._toggle_state[idx])
        active = [chr(65 + i) for i, s in enumerate(self._toggle_state) if s]
        status = f"Active: {', '.join(active)}" if active else "click a square"
        self._set("toggle-status", html.escape(status))

    def _on_form_submit(self):
        el = self._el.get("form-output")
        if el:
            el.setInnerRml("<p>Form submitted!</p>")

    def _update(self, task):
        t = globalClock.getFrameTime()
        fps = globalClock.getAverageFrameRate()
        s = math.sin(t * 1.5) * 0.5 + 0.5

        self._set("fps",         f"{fps:.1f}")
        self._set("frame-count", str(globalClock.getFrameCount()))
        self._set("time-val",    f"{t:.2f}")
        self._set("sin-val",     f"{s:.3f}")

        fill = self._el.get("progress-fill")
        if fill:
            fill.setAttribute("style", f"width: {s * 100:.1f}%;")

        r, g, b = _hsv_to_rgb((t * 40) % 360, 0.75, 0.95)
        swatch = self._el.get("color-swatch")
        if swatch:
            swatch.setAttribute(
                "style",
                f"background-color: rgb({int(r*255)},{int(g*255)},{int(b*255)});")

        return task.cont

    def _set(self, key, text):
        el = self._el.get(key)
        if el:
            el.setInnerRml(text)


FeatureShowcase().run()
