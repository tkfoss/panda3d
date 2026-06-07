"""
Panda3D RmlUi Feature Showcase
================================
Demonstrates the panda3d.rmlui module across six tabs:

  Welcome      — source guide: what to look at in each file
  Font & Text  — font-effect: glow, outline, shadow, blur; emoji; sizes
  Animations   — @keyframes 3D cube, transition: on :hover, easing gallery
  Decorators   — gradients, tiled-box, spritesheet, image-color, fit modes
  Filters & FX — filter:, backdrop-filter:, box-shadow:, mask-image:
  Forms        — all input types; submit wired to Panda3D Messenger
  Live/Python  — per-frame DOM updates via set_inner_rml + set_attribute

Controls
--------
F1     : toggle RmlUi debugger (hover any element to inspect)
Escape : quit

See features.rml for all the markup and features.rcss for all the styles.
This file (main.py) handles only: setup, per-frame updates, and event wiring.
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
loadPrcFileData("", "window-title RmlUi Feature Showcase")


class FeatureDemo(ShowBase):
    def __init__(self):
        ShowBase.__init__(self)
        self.disableMouse()
        self.win.setClearColor(LVecBase4f(0.08, 0.09, 0.11, 1))

        self._ping_count = 0
        self._toggle_state = [False, False, False, False]

        self._setup_rmlui()
        self._setup_input()
        self.taskMgr.add(self._update, "rmlui-update")

    # ------------------------------------------------------------------
    # Setup
    # ------------------------------------------------------------------

    def _setup_rmlui(self):
        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self.rml = RmlRegion.make("features", self.win)
        self.rml.setInputHandler(self.input_handler)

        ctx = self.rml.getContext()
        # Load all four weights so RCSS font-weight/style works correctly
        for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                    "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf"):
            ctx.loadFontFace(os.path.join(ASSETS, ttf))

        self._doc = ctx.loadDocument(os.path.join(ASSETS, "features.rml"))
        if not self._doc:
            raise RuntimeError("features.rml not found")
        self._doc.show()

        self._cache_elements()
        self._wire_events()

    def _cache_elements(self):
        """Cache frequently-updated elements to avoid repeated getElementById calls."""
        ids = ["fps", "frame-count", "time-val", "sin-val",
               "progress-fill", "color-swatch",
               "ping-count", "form-output",
               "toggle-status",
               "tog0", "tog1", "tog2", "tog3"]
        self._el = {k: self._doc.getElementById(k) for k in ids}

    def _wire_events(self):
        """
        Wire DOM events to Panda3D Messenger events.

        RmlElement.add_event_listener(dom_event, panda_event) registers a
        C++ Rml::EventListener that calls throw_event(panda_event) when fired.
        Python then handles it with base.accept() — no scripting plugin needed.
        """
        # Ping button (Live tab)
        btn = self._doc.getElementById("ping-btn")
        if btn:
            btn.addEventListener("click", "rmlui-ping")
            self.accept("rmlui-ping", self._on_ping)

        # Toggle squares (Live tab)
        for i in range(4):
            el = self._el.get(f"tog{i}")
            if el:
                ev = f"rmlui-tog{i}"
                el.addEventListener("click", ev)
                self.accept(ev, self._on_toggle, [i])

        # Form submit (Forms tab)
        form = self._doc.getElementById("demo-form")
        if form:
            form.addEventListener("submit", "rmlui-submit")
            self.accept("rmlui-submit", self._on_form_submit)

        # Speed slider label update (Forms tab)
        # The slider fires "change" on every drag tick
        slider = self._doc.getElementById("speed-slider")
        if slider:
            slider.addEventListener("change", "rmlui-speed")
            self.accept("rmlui-speed", self._on_speed)

    def _setup_input(self):
        self.accept("escape", self.userExit)
        self.accept("f1", lambda: self.rml.setDebuggerVisible(
            not self.rml.isDebuggerVisible()))

    # ------------------------------------------------------------------
    # Event handlers
    # ------------------------------------------------------------------

    def _on_ping(self):
        self._ping_count += 1
        self._set("ping-count", str(self._ping_count))

    def _on_toggle(self, idx):
        self._toggle_state[idx] = not self._toggle_state[idx]
        el = self._el.get(f"tog{idx}")
        if el:
            # set_class(name, activate) adds or removes a CSS class
            el.setClass("active", self._toggle_state[idx])
        active = [chr(65 + i) for i, s in enumerate(self._toggle_state) if s]
        status = f"Active: {', '.join(active)}" if active else "click a square"
        self._set("toggle-status", html.escape(status))

    def _on_form_submit(self):
        # We can't read input values back yet (getAttribute on input
        # returns the initial value, not the current user-entered value —
        # that would need a get_value() extension on RmlElement).
        # Show what we know and note the limitation.
        el = self._el.get("form-output")
        if el:
            el.setInnerRml(
                "Form submitted!\n\n"
                "Note: reading back current input values requires a\n"
                "get_value() extension on RmlElement (not yet in the\n"
                "wrapper). The submit event itself arrives correctly."
            )

    def _on_speed(self):
        # Same limitation as above — we know the event fired but can't
        # read the slider value without get_value().
        label = self._doc.getElementById("speed-label")
        if label:
            label.setInnerRml("?")

    # ------------------------------------------------------------------
    # Per-frame update  (this is the core of the Live tab)
    # ------------------------------------------------------------------

    def _update(self, task):
        t = globalClock.getFrameTime()
        frame = globalClock.getFrameCount()
        fps = globalClock.getAverageFrameRate()
        s = math.sin(t * 1.5) * 0.5 + 0.5    # oscillates 0..1

        # Live tab counters
        self._set("fps",         f"{fps:.1f}")
        self._set("frame-count", str(frame))
        self._set("time-val",    f"{t:.2f}")
        self._set("sin-val",     f"{s:.3f}")

        # Progress bar — set width via inline style attribute
        fill = self._el.get("progress-fill")
        if fill:
            fill.setAttribute("style", f"width: {s * 100:.1f}%;")

        # HSV-cycling color swatch
        r, g, b = _hsv_to_rgb((t * 40) % 360, 0.75, 0.95)
        swatch = self._el.get("color-swatch")
        if swatch:
            swatch.setAttribute(
                "style",
                f"background-color: rgb({int(r*255)},{int(g*255)},{int(b*255)});")

        return task.cont

    # ------------------------------------------------------------------

    def _set(self, key, text):
        el = self._el.get(key)
        if el:
            el.setInnerRml(text)


def _hsv_to_rgb(h, s, v):
    h = h % 360
    i = int(h / 60)
    f = h / 60 - i
    p, q, t_ = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    return [(v, t_, p), (q, v, p), (p, v, t_),
            (p, q, v), (t_, p, v), (v, p, q)][i]


FeatureDemo().run()
