"""
Panda3D RmlUi Feature Showcase
===============================
Demonstrates panda3d.rmlui capabilities side-by-side:

  • Font effects  — glow, outline, shadow, blur (pure RCSS)
  • CSS animations — @keyframes 3D cube, :hover transitions (pure RCSS)
  • Decorators / sprites — gradients, image tiling, spritesheet icons
  • Python-driven live data — per-frame DOM updates, event listener bridge
  • Forms — text input, radio, checkbox, range, submit

Controls
--------
F1     : toggle RmlUi debugger
Escape : quit
"""

import os
import math

from direct.showbase.ShowBase import ShowBase
from panda3d.core import loadPrcFileData, LVecBase4f
from panda3d.rmlui import RmlRegion, RmlInputHandler

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
loadPrcFileData("", f"model-path {ASSETS}")
loadPrcFileData("", "win-size 1200 750")


class FeatureDemo(ShowBase):
    def __init__(self):
        ShowBase.__init__(self)
        self.disableMouse()
        self.win.setClearColor(LVecBase4f(0.1, 0.1, 0.13, 1))

        self._ping_count = 0

        self._setup_rmlui()
        self._setup_input()
        self.taskMgr.add(self._update, "update")

    # ------------------------------------------------------------------

    def _setup_rmlui(self):
        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self.rml = RmlRegion.make("features", self.win)
        self.rml.setInputHandler(self.input_handler)

        ctx = self.rml.getContext()
        ctx.loadFontFace(os.path.join(ASSETS, "LatoLatin-Regular.ttf"))
        ctx.loadFontFace(os.path.join(ASSETS, "LatoLatin-Bold.ttf"))
        ctx.loadFontFace(os.path.join(ASSETS, "LatoLatin-Italic.ttf"))
        ctx.loadFontFace(os.path.join(ASSETS, "LatoLatin-BoldItalic.ttf"))

        self._doc = ctx.loadDocument(os.path.join(ASSETS, "features.rml"))
        if not self._doc:
            raise RuntimeError("features.rml not found")
        self._doc.show()

        # Cache elements we update every frame
        self._el = {k: self._doc.getElementById(k) for k in [
            "fps", "frame-count", "sin-val",
            "progress-bar-fill", "color-swatch",
            "ping-count", "form-output", "speed-val",
        ]}

        # Ping button → Panda3D Messenger
        btn = self._doc.getElementById("ping-btn")
        if btn:
            btn.addEventListener("click", "rmlui-ping")
            self.accept("rmlui-ping", self._on_ping)

        # Speed slider → update label immediately
        slider = self._doc.getElementById("speed-slider")
        if slider:
            slider.addEventListener("change", "rmlui-speed-change")
            self.accept("rmlui-speed-change", self._on_speed_change)

        # Form submit
        form = self._doc.getElementById("demo-form")
        if form:
            form.addEventListener("submit", "rmlui-form-submit")
            self.accept("rmlui-form-submit", self._on_form_submit)

    def _setup_input(self):
        self.accept("escape", self.userExit)
        self.accept("f1", lambda: self.rml.setDebuggerVisible(
            not self.rml.isDebuggerVisible()))

    # ------------------------------------------------------------------

    def _on_ping(self):
        self._ping_count += 1
        el = self._el["ping-count"]
        if el:
            el.setInnerRml(str(self._ping_count))

    def _on_speed_change(self):
        # We can't read input values back through our wrapper yet,
        # so we just show that the event fired.
        el = self._el["speed-val"]
        if el:
            el.setInnerRml("?")   # value read not yet exposed

    def _on_form_submit(self):
        el = self._el["form-output"]
        if el:
            el.setInnerRml("Form submitted!\n(field values need getAttribute extension)")

    # ------------------------------------------------------------------

    def _update(self, task):
        t = globalClock.getFrameTime()
        frame = globalClock.getFrameCount()
        fps = globalClock.getAverageFrameRate()
        s = math.sin(t * 1.5) * 0.5 + 0.5    # 0..1

        # FPS + frame counter
        self._set("fps", f"{fps:.1f}")
        self._set("frame-count", str(frame))

        # Sin wave value + progress bar
        self._set("sin-val", f"{s:.3f}")
        fill = self._el["progress-bar-fill"]
        if fill:
            fill.setAttribute("style", f"width: {s*100:.1f}%;")

        # Cycling color swatch — hue via HSV→RGB
        r, g, b = _hsv_to_rgb((t * 40) % 360, 0.7, 0.9)
        swatch = self._el["color-swatch"]
        if swatch:
            swatch.setAttribute("style",
                f"background-color: rgb({int(r*255)},{int(g*255)},{int(b*255)});")

        return task.cont

    def _set(self, key, text):
        el = self._el.get(key)
        if el:
            el.setInnerRml(text)


def _hsv_to_rgb(h, s, v):
    h = h % 360
    i = int(h / 60)
    f = h / 60 - i
    p, q, t_ = v*(1-s), v*(1-s*f), v*(1-s*(1-f))
    return [(v,t_,p),(q,v,p),(p,v,t_),(p,q,v),(t_,p,v),(v,p,q)][i]


FeatureDemo().run()
