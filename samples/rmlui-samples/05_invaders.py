"""
Panda3D RmlUi — Invaders Menu System
====================================
Python port of the upstream RmlUi "invaders" sample's shell: the windowed menu
system, the high-score table (data binding), the options/start-game forms and a
game-screen placeholder.

What is faithful vs substituted
-------------------------------
Faithful
  • The window-template chrome (assets/window.rml + invader.rcss): title bar,
    icons, draggable/resizable handles, buttons, radios, checkboxes, <select>.
  • The menu navigation: main_menu -> start_game / high_score / options / help,
    each its own loadable document, with Back / Escape navigation.
  • High scores via data binding: ctx.create_data_model("high_scores") +
    bind_list("rows", ...) rendered with <tr data-for="row : rows"> and an
    empty-state <p data-if="count == 0">.  dirty_variable() refreshes the DOM.
  • The options & start-game forms: difficulty / colour / graphics / audio read
    back via the "submit"/"change" DOM events (ev.get_parameter*).

Substituted (and why)
  • Upstream wired navigation with inline onclick="goto X; close Y" handled by a
    C++ EventManager + EventListenerInstancer.  That instancer cannot be
    registered from Python, so the .rml files give every control an id and this
    MenuManager wires add_event_listener("click"/"submit"/"change", ...).
  • The high-score model used RegisterStruct<Score> (nested name/colour/wave/
    score).  The Python bind_list only accepts FLAT scalar lists, so each row is
    pre-formatted into one padded monospace string and bound as a single list.
    (The per-row coloured ship swatch is therefore a single static sprite.)
  • The actual gameplay used two custom C++ decorators (DecoratorDefender,
    DecoratorStarfield) and a custom <game> element (ElementGame) — none of which
    can be registered from Python.  The starfield is replaced with a gradient,
    and the game screen is a static HUD placeholder.  See the .rml comments.

Controls
--------
F1 / ` : toggle RmlUi debugger
Escape  : back to the menu (or quit from the main menu)
"""

import os
import random
import sys

from direct.showbase.ShowBase import ShowBase
from panda3d.core import loadPrcFileData, LVecBase4f
from panda3d.rmlui import RmlRegion, RmlInputHandler

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
loadPrcFileData("", f"model-path {ASSETS}")
loadPrcFileData("", "win-size 1024 768")
loadPrcFileData("", "window-title RmlUi — Invaders")
# The RmlUi filter/layer shaders need GLSL 1.50, which macOS only provides in
# a core-profile context.  Stencil bits enable border-radius / transform
# clipping on the main window.
if sys.platform == "darwin":
    loadPrcFileData("", "gl-version 3 2")
loadPrcFileData("", "framebuffer-stencil true")


# Column widths used to pre-format each high-score row into one monospace
# string (bind_list is flat-scalars-only, so we cannot bind real columns).
def _format_row(name, wave, score):
    return "{:<16}{:<7}{:>6}".format(name[:15], wave, score)


# A few seed scores so the table is not empty on first view.
_SEED_SCORES = [
    ("MARTIAN BANE", 7, 9800),
    ("AYRE", 5, 6400),
    ("NOVA", 4, 5100),
    ("RIFM", 2, 1700),
]

_DEMO_NAMES = ["VIPER", "ORION", "ZARA", "KESTREL", "DELTA", "ATLAS"]


class HighScores:
    """Flat-list high-score model, mirroring upstream HighScores.cpp behaviour:
    keep a sorted top-10 list, expose it as pre-formatted strings via bind_list,
    and dirty the bound variables when it changes."""

    MAX_SCORES = 10

    def __init__(self, ctx):
        # entries: list of (name, wave, score), kept sorted by score desc.
        self._entries = [list(e) for e in _SEED_SCORES]
        self._rows = []

        self._dm = ctx.create_data_model("high_scores")
        self._dm.bind_list("rows", lambda: self._rows)
        self._dm.bind_func("count", lambda: len(self._rows))
        self._rebuild()

    def _rebuild(self):
        self._entries.sort(key=lambda e: e[2], reverse=True)
        del self._entries[self.MAX_SCORES:]
        self._rows = [_format_row(n, w, s) for (n, w, s) in self._entries]

    def submit(self, name, wave, score):
        if score <= 0:
            return
        self._entries.append([name, wave, score])
        self._rebuild()
        self._dm.dirty_variable("rows")
        self._dm.dirty_variable("count")

    def high_score(self):
        return max((e[2] for e in self._entries), default=0)


class MenuManager:
    """Loads each screen as its own document and wires its controls, mirroring
    the upstream EventManager 'goto / load / close' navigation but driven by
    Python add_event_listener callbacks instead of inline onclick commands."""

    def __init__(self, app):
        self.app = app
        self.ctx = app.rml.get_context()
        self.high_scores = HighScores(self.ctx)

        # Game state collected by the forms (mirrors GameDetails).
        self.difficulty = "easy"
        self.colour = "255,255,240"
        self.graphics = "ok"
        self.reverb = True
        self.spatialisation = False

        self._docs = {}        # name -> RmlDocument currently open
        self._background = None

    # ── document helpers ────────────────────────────────────────────────

    def _load(self, name):
        """Load <name>.rml from assets, set its #title span from the document
        title (as upstream EventManager.LoadWindow did), show it, and wire it."""
        path = os.path.join(ASSETS, f"invaders_{name}.rml")
        doc = self.ctx.load_document(path)
        if not doc:
            raise RuntimeError(f"failed to load {path}")

        title_el = doc.get_element_by_id("title")
        if title_el:
            title_el.set_inner_rml(doc.get_title())

        doc.show()
        self._docs[name] = doc
        self._wire(name, doc)
        return doc

    def _close(self, name):
        doc = self._docs.pop(name, None)
        if doc:
            doc.close()

    def goto(self, name):
        """Close every open menu screen and open <name> (the background stays)."""
        for open_name in list(self._docs):
            self._close(open_name)
        return self._load(name)

    # ── wiring per screen ───────────────────────────────────────────────

    def _on(self, doc, el_id, event, cb):
        el = doc.get_element_by_id(el_id)
        if el:
            el.add_event_listener(event, cb)

    def _wire(self, name, doc):
        handler = getattr(self, f"_wire_{name}", None)
        if handler:
            handler(doc)

    def _wire_main_menu(self, doc):
        self._on(doc, "btn_start",     "click", lambda ev: self.goto("start_game"))
        self._on(doc, "btn_highscore", "click", lambda ev: self.goto("high_score"))
        self._on(doc, "btn_options",   "click", lambda ev: self.goto("options"))
        self._on(doc, "btn_help",      "click", lambda ev: self.goto("help"))
        self._on(doc, "btn_exit",      "click", lambda ev: self.app.userExit())

    def _wire_start_game(self, doc):
        self._on(doc, "btn_start_back", "click", lambda ev: self.goto("main_menu"))
        # Upstream's onsubmit="start" handler: read difficulty + colour, then
        # open the game screen.
        self._on(doc, "start_form", "submit", self._on_start_submit)

    def _on_start_submit(self, ev):
        self.difficulty = ev.get_parameter("difficulty", "easy")
        self.colour = ev.get_parameter("colour", "255,255,240")
        self.goto("game")

    def _wire_options(self, doc):
        self._on(doc, "btn_options_back", "click", lambda ev: self.goto("main_menu"))
        # Restore previously chosen options into the controls (upstream "restore").
        self._restore_options(doc)
        # onchange -> show/hide the bad-graphics warning + enable Accept.
        self._on(doc, "options_form", "change", self._on_options_change)
        # onsubmit -> store the chosen options and go back to the menu.
        self._on(doc, "options_form", "submit", self._on_options_submit)

    def _restore_options(self, doc):
        gfx = doc.get_element_by_id(self.graphics)
        if gfx:
            gfx.set_attribute("checked", "")
        for opt_id, flag in (("reverb", self.reverb), ("3d", self.spatialisation)):
            el = doc.get_element_by_id(opt_id)
            if el:
                if flag:
                    el.set_attribute("checked", "")
                else:
                    el.remove_attribute("checked")

    def _on_options_change(self, ev):
        doc = self._docs.get("options")
        if not doc:
            return
        # Enable the Accept button on any change.
        accept = doc.get_element_by_id("accept")
        if accept:
            accept.remove_attribute("disabled")
        # Toggle the bad-graphics warning from the current graphics value.
        warn = doc.get_element_by_id("bad_warning")
        if warn:
            is_bad = ev.get_parameter("value", "") == "bad"
            warn.set_property("display", "block" if is_bad else "none")

    def _on_options_submit(self, ev):
        self.graphics = ev.get_parameter("graphics", "ok")
        self.reverb = ev.get_parameter("reverb", "") == "true"
        self.spatialisation = ev.get_parameter("3d", "") == "true"
        self.goto("main_menu")

    def _wire_help(self, doc):
        self._on(doc, "btn_help_back", "click", lambda ev: self.goto("main_menu"))

    def _wire_game(self, doc):
        # Fill the placeholder HUD from the collected game state.
        diff = doc.get_element_by_id("difficulty")
        if diff:
            diff.set_inner_rml(self.difficulty.upper())
        ship = doc.get_element_by_id("ship_colour")
        if ship:
            r, g, b = (self.colour.split(",") + ["255", "255", "255"])[:3]
            ship.set_property("color", f"rgb({r},{g},{b})")
        hi = doc.get_element_by_id("hiscore")
        if hi:
            hi.set_inner_rml(str(self.high_scores.high_score()))
        # No real gameplay: simulate a finished game by submitting a demo score,
        # so returning to the High Scores screen shows a fresh entry.
        name = random.choice(_DEMO_NAMES)
        wave = random.randint(1, 9)
        score = random.randint(500, 9999)
        self.high_scores.submit(name, wave, score)
        sc = doc.get_element_by_id("score")
        if sc:
            sc.set_inner_rml(str(score))
        wv = doc.get_element_by_id("waves")
        if wv:
            wv.set_inner_rml(str(wave))

    def _wire_high_score(self, doc):
        self._on(doc, "btn_back", "click", lambda ev: self.goto("main_menu"))

    # ── lifecycle ───────────────────────────────────────────────────────

    def start(self):
        self._background = self.ctx.load_document(
            os.path.join(ASSETS, "invaders_background.rml"))
        if self._background:
            self._background.show()
        self.goto("main_menu")

    def on_escape(self):
        # Escape returns to the main menu, or quits if we are already there.
        if "main_menu" in self._docs and len(self._docs) == 1:
            self.app.userExit()
        else:
            self.goto("main_menu")


class InvadersSample(ShowBase):
    def __init__(self):
        ShowBase.__init__(self)
        self.disableMouse()
        self.win.setClearColor(LVecBase4f(0.02, 0.02, 0.06, 1))

        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self.rml = RmlRegion.make("invaders", self.win)
        self.rml.set_input_handler(self.input_handler)

        ctx = self.rml.get_context()
        for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                    "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf",
                    "RobotoMono-Regular.ttf", "RobotoMono-Bold.ttf"):
            ctx.load_font_face(os.path.join(ASSETS, ttf))

        self.menu = MenuManager(self)
        self.menu.start()
        self.rml.init_debugger()

        self.accept("escape", self.menu.on_escape)

        def toggle_dbg():
            self.rml.set_debugger_visible(not self.rml.is_debugger_visible())
        self.accept("f1", toggle_dbg)
        self.accept("`", toggle_dbg)


if __name__ == "__main__":
    InvadersSample().run()
