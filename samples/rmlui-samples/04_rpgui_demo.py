"""
Panda3D RmlUi — RPGUI Demo
===========================
Port of the RPGUI HTML/CSS RPG UI kit (https://github.com/RonenNess/RPGUI)
to RmlUi running inside Panda3D.

Shows:
  • Framed containers (standard, golden, golden-2, grey)
  • Text inputs and textarea
  • Standard and golden buttons wired to Panda3D Messenger
  • Radio buttons and checkboxes (class-toggled from Python)
  • Dropdown <select> and list <select size="N">
  • Icon sprites (sword, shield, potions, equipment slots)
  • Animated HP / Mana / Stamina progress bars
  • Range sliders with live readout
  • Character stats panel updated each frame

Controls
--------
F1 / `  : toggle RmlUi debugger
Escape  : quit
"""

import os
import math

from direct.showbase.ShowBase import ShowBase
from panda3d.core import (loadPrcFileData, LVecBase4f, WindowProperties,
                           TransparencyAttrib, TexturePool, CardMaker, NodePath)

from panda3d.rmlui import RmlRegion, RmlInputHandler

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
loadPrcFileData("", f"model-path {ASSETS}")
loadPrcFileData("", "win-size 1280 800")
loadPrcFileData("", "window-title RmlUi — RPGUI Demo")


class RpguiDemo(ShowBase):
    def __init__(self):
        ShowBase.__init__(self)
        self.disableMouse()
        self.win.setClearColor(LVecBase4f(0.13, 0.13, 0.20, 1))

        # ── State ────────────────────────────────────────────────────
        self._gender       = "male"     # "male" | "female"
        self._chk          = [True, False]
        self._hp           = 0.4
        self._mana         = 0.8
        self._stamina      = 0.2
        self._slider_val   = 8
        self._hero_class   = "Warrior"
        self._name         = "Bob The Destroyer"

        # ── RmlUi setup ──────────────────────────────────────────────
        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self.rml = RmlRegion.make("rpgui", self.win)
        self.rml.setInputHandler(self.input_handler)

        ctx = self.rml.getContext()
        for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                    "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf",
                    "PressStart2P-Regular.ttf"):
            ctx.loadFontFace(os.path.join(ASSETS, ttf))

        self._doc = ctx.loadDocument(os.path.join(ASSETS, "rpgui_demo.rml"))
        if not self._doc:
            raise RuntimeError("rpgui_demo.rml not found")
        self._doc.show()
        self.rml.initDebugger()

        self._setup_cursor()
        self._wire_events()
        self._refresh_bars()
        self._refresh_stats()

        self.taskMgr.add(self._update, "rpgui-update")

        self.accept("escape", self.userExit)
        def _toggle_dbg():
            self.rml.setDebuggerVisible(not self.rml.isDebuggerVisible())
        self.accept("f1", _toggle_dbg)
        self.accept("`",  _toggle_dbg)

    # ── Cursor ──────────────────────────────────────────────────────

    def _setup_cursor(self):
        # Hide the OS cursor — must setForeground so properties apply immediately.
        props = WindowProperties()
        props.setCursorHidden(True)
        props.setForeground(True)
        self.win.requestProperties(props)

        # Load cursor textures; ATS_none prevents power-of-two padding so the
        # card can be sized to the true pixel dimensions.
        from panda3d.core import SamplerState, LoaderOptions, ATS_none
        opts = LoaderOptions()
        opts.setAutoTextureScale(ATS_none)
        cursor_dir = os.path.join(ASSETS, "cursor")
        self._cursor_textures = {}
        self._cursor_sizes = {}   # (width_px, height_px) per cursor name
        for name in ("default", "point", "select", "grab-open", "grab-close"):
            path = os.path.join(cursor_dir, f"{name}.png")
            if os.path.exists(path):
                tex = TexturePool.loadTexture(path, 0, False, opts)
                tex.setMagfilter(SamplerState.FT_nearest)
                tex.setMinfilter(SamplerState.FT_nearest)
                self._cursor_textures[name] = tex
                # get_x_size() is the true size when ATS_none is used
                self._cursor_sizes[name] = (tex.getXSize(), tex.getYSize())

        # Cursor card — one persistent card, resized per texture in _set_cursor.
        cm = CardMaker("cursor-card")
        cm.setFrame(0, 1, -1, 0)  # placeholder; resized in _set_cursor
        self._cursor_np = self.render2d.attachNewNode(cm.generate())
        self._cursor_np.setTransparency(TransparencyAttrib.MAlpha)
        self._cursor_np.setBin("fixed", 100)
        self._cursor_np.setDepthTest(False)
        self._set_cursor("default")

        self.accept("rmlui-cursor", self._set_cursor)

    def _set_cursor(self, name):
        key = name if name in self._cursor_textures else "default"
        tex = self._cursor_textures.get(key)
        if not tex:
            return
        w, h = self._cursor_sizes[key]
        # render2d: half-window-width = 1.0, so 1px = 1/(win_width/2)
        win_half_w = self.win.getXSize() / 2.0
        sx = w / win_half_w
        sy = h / win_half_w
        self._cursor_np.setTexture(tex, 1)
        self._cursor_np.setScale(sx, 1, sy)

    # ── Event wiring ────────────────────────────────────────────────

    def _wire_events(self):
        doc = self._doc

        # Buttons
        for btn_id, ev in (("btn-normal", "rpgui-btn-normal"),
                            ("btn-golden", "rpgui-btn-golden")):
            el = doc.getElementById(btn_id)
            if el:
                el.addEventListener("click", ev)
        self.accept("rpgui-btn-normal", self._on_button, ["normal"])
        self.accept("rpgui-btn-golden", self._on_button, ["golden"])

        # Radio buttons
        for rid, gender in (("radio-male", "male"), ("radio-female", "female")):
            el = doc.getElementById(rid)
            if el:
                ev = f"rpgui-radio-{gender}"
                el.addEventListener("click", ev)
                self.accept(ev, self._on_radio, [gender])

        # Checkboxes
        for i in range(2):
            el = doc.getElementById(f"chk{i+1}")
            if el:
                ev = f"rpgui-chk{i}"
                el.addEventListener("click", ev)
                self.accept(ev, self._on_checkbox, [i])

        # Class dropdown — poll value each frame (no change event needed)
        # Profession list — same

    # ── Event handlers ──────────────────────────────────────────────

    def _on_button(self, kind):
        out = self._doc.getElementById("btn-output")
        if out:
            out.setInnerRml(f"Clicked: {kind} button")
        stat = self._doc.getElementById("stat-output")
        if stat:
            stat.setInnerRml(f"Button '{kind}' clicked!")

    def _on_radio(self, gender):
        self._gender = gender
        for rid, g in (("radio-male", "male"), ("radio-female", "female")):
            el = self._doc.getElementById(rid)
            if el:
                el.setClass("checked", g == self._gender)

    def _on_checkbox(self, idx):
        self._chk[idx] = not self._chk[idx]
        el = self._doc.getElementById(f"chk{idx+1}")
        if el:
            el.setClass("checked", self._chk[idx])

    # ── DOM helpers ─────────────────────────────────────────────────

    def _refresh_bars(self):
        for fill_id, pct in (("hp-fill",      self._hp),
                              ("mana-fill",    self._mana),
                              ("stamina-fill", self._stamina)):
            el = self._doc.getElementById(fill_id)
            if el:
                el.setAttribute("style", f"width: {pct * 100:.1f}%;")

    def _refresh_stats(self):
        def _set(el_id, text):
            el = self._doc.getElementById(el_id)
            if el:
                el.setInnerRml(text)

        _set("stat-name",    self._name)
        _set("stat-class",   self._hero_class)
        _set("stat-hp",      f"{int(self._hp * 100)} / 100")
        _set("stat-mana",    f"{int(self._mana * 100)} / 100")
        _set("stat-stamina", f"{int(self._stamina * 100)} / 100")
        _set("stat-slider",  str(self._slider_val))

    # ── Per-frame update ────────────────────────────────────────────

    def _update(self, task):
        # Move cursor sprite to match the mouse.
        if self.mouseWatcherNode.hasMouse():
            mx = self.mouseWatcherNode.getMouseX()
            my = self.mouseWatcherNode.getMouseY()
            self._cursor_np.setPos(mx, 0, my)

        t = globalClock.getFrameTime()

        # Animate the bars with gentle sine waves
        self._hp      = 0.5 + 0.45 * math.sin(t * 0.3)
        self._mana    = 0.5 + 0.45 * math.sin(t * 0.5 + 1.0)
        self._stamina = 0.5 + 0.45 * math.sin(t * 0.7 + 2.1)

        # Poll class dropdown
        sel = self._doc.getElementById("class-select")
        if sel:
            v = sel.getValue()
            if v and v != self._hero_class:
                self._hero_class = v

        # Poll name inputs
        inp_name = self._doc.getElementById("inp-name")
        inp_last = self._doc.getElementById("inp-lastname")
        if inp_name and inp_last:
            first = inp_name.getValue()
            last  = inp_last.getValue()
            full  = f"{first} {last}".strip()
            if full:
                self._name = full

        # Poll slider
        sl = self._doc.getElementById("slider-normal")
        if sl:
            try:
                self._slider_val = int(float(sl.getValue()))
            except ValueError:
                pass

        self._refresh_bars()
        self._refresh_stats()
        return task.cont


RpguiDemo().run()
