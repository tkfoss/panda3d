"""
Panda3D RmlUi demo
==================
Shows how to use the panda3d.rmlui module to render an HTML/CSS HUD overlay
on top of a 3D scene.

Controls
--------
Arrow keys  : orbit camera
+/-         : zoom
F1          : toggle RmlUi debugger
Esc         : quit
"""

import os
from math import sin, cos, radians

from direct.showbase.ShowBase import ShowBase
from panda3d.core import (
    AmbientLight, DirectionalLight,
    LVecBase4f,
    loadPrcFileData,
)
from panda3d.rmlui import RmlRegion, RmlInputHandler

ASSETS = os.path.join(os.path.dirname(__file__), "assets")

loadPrcFileData("", f"model-path {ASSETS}")


class Demo(ShowBase):
    def __init__(self):
        ShowBase.__init__(self)

        self.disableMouse()
        self.win.setClearColor(LVecBase4f(0.1, 0.12, 0.15, 1))

        self._cam_h = 0.0
        self._cam_p = -15.0
        self._cam_dist = 20.0
        self._keys = {}
        self._click_count = 0

        self._setup_scene()
        self._setup_rmlui()
        self._setup_input()

        self.taskMgr.add(self._update, "update")

    # ------------------------------------------------------------------
    # Scene
    # ------------------------------------------------------------------

    def _setup_scene(self):
        alight = AmbientLight("ambient")
        alight.setColor((0.3, 0.3, 0.3, 1))
        self.render.setLight(self.render.attachNewNode(alight))

        dlight = DirectionalLight("sun")
        dlight.setColor((0.9, 0.85, 0.7, 1))
        dlnp = self.render.attachNewNode(dlight)
        dlnp.setHpr(-45, -45, 0)
        self.render.setLight(dlnp)

        self.model = self.loader.loadModel("panda")
        if self.model:
            self.model.reparentTo(self.render)
            self.model.setPos(0, 0, -2)

        self._update_camera()

    # ------------------------------------------------------------------
    # RmlUi
    # ------------------------------------------------------------------

    def _setup_rmlui(self):
        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self.rml_region = RmlRegion.make("hud", self.win)
        self.rml_region.setInputHandler(self.input_handler)

        ctx = self.rml_region.getContext()
        ctx.loadFontFace(os.path.join(ASSETS, "LatoLatin-Regular.ttf"))
        ctx.loadFontFace(os.path.join(ASSETS, "LatoLatin-Bold.ttf"))

        self._doc = ctx.loadDocument(os.path.join(ASSETS, "demo.rml"))
        if self._doc:
            self._doc.show()
            btn = self._doc.getElementById("btn-click")
            if btn:
                btn.addEventListener("click", "rmlui-btn-click")
                self.accept("rmlui-btn-click", self.on_button)

        self._ctx = ctx

    # ------------------------------------------------------------------
    # Input
    # ------------------------------------------------------------------

    def _setup_input(self):
        self.accept("escape", self.userExit)
        self.accept("f1", self._toggle_debugger)
        for key in ("arrow_left", "arrow_right", "arrow_up", "arrow_down", "+", "-"):
            self.accept(key,        self._key, [key, True])
            self.accept(key + "-up", self._key, [key, False])

    def _key(self, name, state):
        self._keys[name] = state

    def _toggle_debugger(self):
        self.rml_region.setDebuggerVisible(not self.rml_region.isDebuggerVisible())

    def on_button(self):
        self._click_count += 1

    # ------------------------------------------------------------------
    # Update loop
    # ------------------------------------------------------------------

    def _update(self, task):
        dt = globalClock.getDt()

        if self._keys.get("arrow_left"):   self._cam_h += 60 * dt
        if self._keys.get("arrow_right"):  self._cam_h -= 60 * dt
        if self._keys.get("arrow_up"):     self._cam_p = min(self._cam_p + 40 * dt, 80)
        if self._keys.get("arrow_down"):   self._cam_p = max(self._cam_p - 40 * dt, -80)
        if self._keys.get("+"):            self._cam_dist = max(self._cam_dist - 8 * dt, 3)
        if self._keys.get("-"):            self._cam_dist = min(self._cam_dist + 8 * dt, 60)

        self._update_camera()

        if self._doc:
            el = self._doc.getElementById("fps")
            if el:
                el.setInnerRml(f"{globalClock.getAverageFrameRate():.1f}")
            el = self._doc.getElementById("frame")
            if el:
                el.setInnerRml(str(globalClock.getFrameCount()))
            el = self._doc.getElementById("count")
            if el:
                el.setInnerRml(str(self._click_count))

        return task.cont

    def _update_camera(self):
        h = radians(self._cam_h)
        p = radians(self._cam_p)
        d = self._cam_dist
        self.camera.setPos(d * cos(p) * sin(h),
                           -d * cos(p) * cos(h),
                           d * sin(p))
        self.camera.lookAt(0, 0, 0)


Demo().run()
