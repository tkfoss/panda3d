"""
Panda3D RmlUi console sample
============================
Restored from the original libRocket rocket-console sample, updated to use
the panda3d.rmlui module (RmlUi v6).

A loading screen appears while the 3D models load.  Once ready, a DOS-style
console is rendered into a GraphicsBuffer that is mapped onto the monitor
model in the scene.  Type 'help' for available commands.

Controls
--------
Escape     : quit
Mouse      : orbit camera (after loading)
"""
import os
import sys
import random

from panda3d.core import (
    AmbientLight, DirectionalLight, PointLight,
    Texture, Vec4, Mat4,
    loadPrcFileData,
)
from panda3d.rmlui import RmlRegion, RmlInputHandler
from direct.showbase.ShowBase import ShowBase
from direct.interval.LerpInterval import LerpFunc

import console as consolemod

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
loadPrcFileData("", f"model-path {ASSETS}")


class MyApp(ShowBase):

    def __init__(self):
        ShowBase.__init__(self)

        self.win.setClearColor(Vec4(0.2, 0.2, 0.2, 1))
        self.disableMouse()
        self.render.setShaderAuto()

        # Lighting
        dlight = DirectionalLight("dlight")
        dlight.setColor((0.8, 0.8, 0.5, 1))
        dlnp = self.render.attachNewNode(dlight)
        dlnp.setHpr(0, -60, 0)
        self.render.setLight(dlnp)

        alight = AmbientLight("alight")
        alight.setColor((0.2, 0.2, 0.2, 1))
        alnp = self.render.attachNewNode(alight)
        self.render.setLight(alnp)

        plight = PointLight("plight")
        plnp = self.render.attachNewNode(plight)
        plnp.setPos(0, 0, 10)
        self.render.setLight(plnp)

        # Shared RmlUi input handler
        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self._monitor_np = None
        self._keyboard_np = None
        self._loading_doc = None
        self._console = None
        self._spew_active = False

        self._start_loading()
        self._open_loading_screen()

    # ------------------------------------------------------------------
    # Asset loading
    # ------------------------------------------------------------------

    def _start_loading(self):
        self.taskMgr.doMethodLater(0.5, self._load_models, "load-models")

    def _load_models(self, task):
        self._monitor_np = self.loader.loadModel("monitor")
        self._keyboard_np = self.loader.loadModel("takeyga_kb")
        return task.done

    # ------------------------------------------------------------------
    # Loading screen
    # ------------------------------------------------------------------

    def _open_loading_screen(self):
        self._loading_region = RmlRegion.make("loading", self.win)
        self._loading_region.setInputHandler(self.input_handler)

        ctx = self._loading_region.getContext()
        ctx.loadFontFace(os.path.join(ASSETS, "modenine.ttf"))

        self._loading_doc = ctx.loadDocument(os.path.join(ASSETS, "loading.rml"))
        if not self._loading_doc:
            raise RuntimeError("loading.rml not found")
        self._loading_doc.show()

        self._label_el = self._loading_doc.getElementById("loading-label")
        self._loading_body = self._loading_doc.getElementById("loading-body")

        # Close on click or Enter/Space/Escape via Panda3D events
        self._loading_body.addEventListener("click", "rmlui-loading-click")
        self.accept("rmlui-loading-click", self._user_confirmed)
        self.accept("enter", self._user_confirmed)
        self.accept("space", self._user_confirmed)
        self.accept("escape", self._user_confirmed)

        self._user_wants_close = False
        self._stop_loading_time = 0   # set when models are ready

        self.taskMgr.add(self._cycle_loading, "cycle-loading")

    def _user_confirmed(self):
        self._user_wants_close = True

    def _cycle_loading(self, task):
        ready = self._monitor_np is not None and self._keyboard_np is not None
        t = globalClock.getFrameTime()

        if ready:
            if self._stop_loading_time == 0:
                self._stop_loading_time = t + 1.5   # brief "Ready" pause
            if self._label_el:
                self._label_el.setInnerRml("Ready")
            if self._user_wants_close or t >= self._stop_loading_time:
                self._dismiss_loading()
                return task.done
        else:
            dots = int(t * 4) % 5
            if self._label_el:
                self._label_el.setInnerRml("Loading" + "." * (dots + 1))

        return task.cont

    def _dismiss_loading(self):
        self.ignore("enter")
        self.ignore("space")
        self.ignore("escape")
        self.ignore("rmlui-loading-click")
        if self._loading_doc:
            self._loading_doc.close()
            self._loading_doc = None
        self._loading_region.setActive(False)
        self._create_console()

    # ------------------------------------------------------------------
    # Console
    # ------------------------------------------------------------------

    def _create_console(self):
        self._monitor_np.reparentTo(self.render)
        self._monitor_np.setScale(1.5)
        self._monitor_np.setPos(0, 0, 1)

        self._keyboard_np.reparentTo(self.render)
        self._keyboard_np.setHpr(-90, 0, 15)
        self._keyboard_np.setScale(20)
        self._keyboard_np.setPos(0, -5, -2.5)

        self.camera.setPos(0, -20, 0)
        self.camera.setHpr(0, 0, 0)
        self.win.setClearColor(Vec4(0.5, 0.5, 0.8, 1))

        faceplate = self._monitor_np.find("**/Faceplate")
        if not faceplate:
            faceplate = self._monitor_np.find("**/*faceplate*")

        mybuffer = self.win.makeTextureBuffer("Console Buffer", 1024, 512)
        tex = mybuffer.getTexture()
        tex.setMagfilter(Texture.FTLinear)
        tex.setMinfilter(Texture.FTLinear)
        if faceplate:
            faceplate.setTexture(tex, 1)

        rml_region = RmlRegion.make("console", mybuffer)
        rml_region.setInputHandler(self.input_handler)

        ctx = rml_region.getContext()
        ctx.loadFontFace(os.path.join(ASSETS, "dos437.ttf"))

        doc = ctx.loadDocument(os.path.join(ASSETS, "console.rml"))
        if not doc:
            raise RuntimeError("console.rml not found")
        doc.show()

        self._console = consolemod.Console(self, doc, 40, 13, self._handle_command)
        self._console.add_line("Panda DOS")
        self._console.add_line("type 'help'")
        self._console.add_line("")
        self._console.allow_editing(True)

        self._setup_console_input()

        # Re-enable mouse for camera orbit
        mat = Mat4(self.camera.getMat())
        mat.invertInPlace()
        self.mouseInterfaceNode.setMat(mat)
        self.enableMouse()

    def _setup_console_input(self):
        self.accept("escape", sys.exit)

        # Printable characters
        chars = "abcdefghijklmnopqrstuvwxyz0123456789 !@#$%^&*()-_=+[]{}\\|;:'\",.<>/?"
        for ch in chars:
            self.accept(ch, self._console.handle_char, [ch])
        for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
            self.accept(ch, self._console.handle_char, [ch])

        self.accept("backspace", self._console.handle_backspace)
        self.accept("enter", self._console.handle_enter)
        self.accept("control-c", self._console.handle_break)

    # ------------------------------------------------------------------
    # Commands
    # ------------------------------------------------------------------

    def _handle_command(self, command):
        if command is None:
            self._spew_active = False
            self._console.add_line("*** break ***")
            self._console.allow_editing(True)
            return

        command = command.strip()
        if not command:
            return

        tokens = command.split()
        cmd = tokens[0].lower()

        if cmd == "help":
            self._console.add_lines([
                "Sorry, this is utter fakery.",
                "You won't get much more",
                "out of this simulation unless",
                "you program it yourself. :)",
                "",
            ])
        elif cmd == "dir":
            self._console.add_lines([
                "Directory of C:\\:",
                "HELP     COM    72 05-06-2015 14:07",
                "DIR      COM   121 05-06-2015 14:11",
                "SPEW     COM   666 05-06-2015 15:02",
                "   2 Files(s)  859 Bytes.",
                "   0 Dirs(s)  7333 Bytes free.",
                "",
            ])
        elif cmd == "cls":
            self._console.cls()
        elif cmd == "echo":
            self._console.add_line(" ".join(tokens[1:]))
        elif cmd == "ver":
            from panda3d.core import PandaSystem
            self._console.add_line("Panda DOS v0.01 in Panda3D " + PandaSystem.getVersionString())
        elif cmd == "spew":
            self._start_spew()
        elif cmd == "exit":
            self._console.set_prompt("System is shutting down NOW!")
            self._terminate()
        else:
            self._console.add_line("command not found")

    def _start_spew(self):
        self._console.allow_editing(False)
        self._console.add_line("LINE NOISE 1.0")
        self._console.add_line("")
        self._spew_active = True
        self._queue_spew(2)

    def _queue_spew(self, delay=0.1):
        self.taskMgr.doMethodLater(delay, self._spew, "spew")

    def _spew(self, task):
        if not self._spew_active:
            return task.done

        def randchr():
            return chr(32 if random.random() < 0.25 else random.randint(33, 126))

        self._console.add_line("".join(randchr() for _ in range(40)))
        self._queue_spew()
        return task.done

    def _terminate(self):
        def done():
            sys.exit(0)

        LerpFunc(lambda _: None, duration=2).start()
        self.taskMgr.doMethodLater(2, lambda t: sys.exit(0) or t.done, "quit")


MyApp().run()
