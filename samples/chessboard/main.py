#!/usr/bin/env python

# Author: Shao Zhang and Phil Saltzman
# Models: Eddie Canaan
# Last Updated: 2015-03-13
#
# This sample shows mouse-picking on a 3D chessboard, wrapped in a modern RmlUi
# front-end. A dark "glassmorphism" main menu, game-creation lobby, puzzles,
# profile and settings screens are layered over the 3D scene using an RmlRegion.
# Choosing "Start game" hides the UI and reveals the interactive 3D board; the
# in-game "Menu" / "Resign" buttons bring the menu back.
#
# The 3D picking part: a collision ray extends from the mouse into the scene and
# we pick the closest square it collides with, then drag pieces between squares.

import os
import sys

from direct.showbase.ShowBase import ShowBase
from panda3d.core import CollisionTraverser, CollisionNode
from panda3d.core import CollisionHandlerQueue, CollisionRay
from panda3d.core import AmbientLight, DirectionalLight
from panda3d.core import LPoint3, LVector3, BitMask32
from panda3d.core import loadPrcFileData
from direct.showbase.DirectObject import DirectObject
from direct.task.Task import Task

from panda3d.rmlui import RmlRegion, RmlInputHandler

SAMPLE_DIR = os.path.dirname(os.path.abspath(__file__))
UI_DIR = os.path.join(SAMPLE_DIR, "ui")
# Add the sample directory (for the 3D models/) and the UI directory (so RmlUi's
# file interface resolves the relative rcss link and any UI images) to the model
# search path. This lets the sample run from any working directory.
loadPrcFileData("", f"model-path {SAMPLE_DIR}")
loadPrcFileData("", f"model-path {UI_DIR}")
loadPrcFileData("", "win-size 1600 900")
loadPrcFileData("", "window-title Aether Chess")

# First we define some constants for the colors
BLACK = (0, 0, 0, 1)
WHITE = (1, 1, 1, 1)
HIGHLIGHT = (0, 1, 1, 1)
PIECEBLACK = (.15, .15, .15, 1)


# This function, given a line (vector plus origin point) and a desired z value,
# will give us the point on the line where the desired z value is what we want.
# This is how we know where to position an object in 3D space based on a 2D mouse
# position. It also assumes that we are dragging in the XY plane.
def PointAtZ(z, point, vec):
    return point + vec * ((z - point.getZ()) / vec.getZ())


# A handy little function for getting the proper position for a given square
def SquarePos(i):
    return LPoint3((i % 8) - 3.5, int(i // 8) - 3.5, 0)


# Helper function for determining whether a square should be white or black
def SquareColor(i):
    if (i + ((i // 8) % 2)) % 2:
        return BLACK
    else:
        return WHITE


# ──────────────────────────────────────────────────────────────────────────────
#  RmlUi front-end
# ──────────────────────────────────────────────────────────────────────────────

# Maps each navigation button's element id (assigned in the rml) to the screen it
# should switch to. "quit" and "game" are handled specially.
NAV_MAP = {
    "nav_menu_1": "profile",
    "nav_menu_2": "play",
    "nav_menu_3": "puzzles",
    "nav_menu_4": "profile",
    "nav_menu_5": "settings",
    "nav_menu_6": "quit",
    "nav_play_1": "menu",
    "nav_play_2": "game",
    "nav_play_3": "menu",
    "nav_game_1": "menu",
    "nav_game_2": "settings",
    "nav_game_3": "menu",
    "nav_puzzles_1": "menu",
    "nav_puzzles_2": "profile",
    "nav_profile_1": "menu",
    "nav_profile_2": "play",
    "nav_profile_3": "settings",
    "nav_settings_1": "menu",
    "nav_settings_2": "menu",
}

SCREENS = ("menu", "play", "game", "puzzles", "profile", "settings")


class ChessUI:
    """Loads the RmlUi screens, layers them over the 3D scene, and routes
    navigation between them. Calls back into the demo to show/hide the board."""

    def __init__(self, base, demo):
        self.base = base
        self.demo = demo

        self.input_handler = RmlInputHandler()
        base.mouseWatcher.attach_new_node(self.input_handler)

        self.region = RmlRegion.make("chess-ui", base.win)
        self.region.set_input_handler(self.input_handler)

        ctx = self.region.get_context()
        for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                    "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf"):
            ctx.load_font_face(os.path.join(UI_DIR, ttf))
        # DejaVu Sans provides the chess pieces and geometric symbols that Lato
        # lacks; registered as a fallback face so those glyphs still render.
        ctx.load_font_face(os.path.join(UI_DIR, "DejaVuSans.ttf"), True)

        # Load every screen up front and keep them hidden until navigated to.
        self.docs = {}
        for name in SCREENS:
            doc = ctx.load_document(os.path.join(UI_DIR, f"{name}.rml"))
            if not doc:
                raise RuntimeError(f"Failed to load UI screen '{name}.rml'")
            doc.hide()
            self.docs[name] = doc

        self.region.init_debugger()

        self._bind_navigation()
        self._bind_settings_tabs()
        self.current = None
        self.show_screen("menu")

    def _bind_navigation(self):
        for element_id, target in NAV_MAP.items():
            for doc in self.docs.values():
                el = doc.get_element_by_id(element_id)
                if el:
                    el.add_event_listener(
                        "click", lambda ev, t=target: self.goto(t))
                    break

    def _bind_settings_tabs(self):
        # The settings screen has a left nav whose items switch the content panel
        # on the right. Wire each nav item to show its panel and mark itself active.
        doc = self.docs["settings"]
        self._settings_tabs = [
            ("tab_board", "panel_board"),
            ("tab_pieces", "panel_pieces"),
            ("tab_sound", "panel_sound"),
            ("tab_gameplay", "panel_gameplay"),
            ("tab_graphics", "panel_graphics"),
            ("tab_controls", "panel_controls"),
        ]
        for tab_id, panel_id in self._settings_tabs:
            tab = doc.get_element_by_id(tab_id)
            if tab:
                tab.add_event_listener(
                    "click", lambda ev, p=panel_id: self._show_settings_panel(p))

    def _show_settings_panel(self, panel_id):
        doc = self.docs["settings"]
        for tab_id, pid in self._settings_tabs:
            panel = doc.get_element_by_id(pid)
            if panel:
                panel.set_class("hidden", pid != panel_id)
            tab = doc.get_element_by_id(tab_id)
            if tab:
                tab.set_class("active", pid == panel_id)

    def show_screen(self, name):
        if self.current is self.docs.get(name):
            return
        if self.current:
            self.current.hide()
        self.current = self.docs[name]
        self.current.show()
        self.current.pull_to_front()

    def goto(self, target):
        if target == "quit":
            self.base.userExit()
        elif target == "game":
            # Reveal the interactive 3D board and lay the transparent game HUD
            # (clocks, move list, Menu/Resign) over it. Empty areas of the HUD
            # let clicks fall through to the board for piece picking.
            self.demo.enter_game()
            self.show_screen("game")
        else:
            # Any normal screen implies leaving the live game, so put the board
            # back to its idle (hidden) state behind the opaque menu screens.
            self.demo.exit_game()
            self.show_screen(target)


# ──────────────────────────────────────────────────────────────────────────────
#  3D chessboard
# ──────────────────────────────────────────────────────────────────────────────

class ChessboardDemo(ShowBase):
    def __init__(self):
        ShowBase.__init__(self)

        self.disableMouse()  # Disable mouse camera control
        camera.setPosHpr(0, -12, 8, 0, -35, 0)  # Set the camera
        # Dark backdrop matching the UI theme, shown behind the board in-game.
        self.win.setClearColor((0.04, 0.05, 0.09, 1))
        self.setupLights()

        # Collision setup for mouse-picking the squares.
        self.picker = CollisionTraverser()
        self.pq = CollisionHandlerQueue()
        self.pickerNode = CollisionNode('mouseRay')
        self.pickerNP = camera.attachNewNode(self.pickerNode)
        self.pickerNode.setFromCollideMask(BitMask32.bit(1))
        self.pickerRay = CollisionRay()
        self.pickerNode.addSolid(self.pickerRay)
        self.picker.addCollider(self.pickerNP, self.pq)

        # Build the board and pieces under a single root so we can show/hide and
        # collide against just the squares.
        self.squareRoot = render.attachNewNode("squareRoot")
        self.pieceRoot = render.attachNewNode("pieceRoot")

        self.squares = [None for i in range(64)]
        self.pieces = [None for i in range(64)]
        for i in range(64):
            self.squares[i] = loader.loadModel("models/square")
            self.squares[i].reparentTo(self.squareRoot)
            self.squares[i].setPos(SquarePos(i))
            self.squares[i].setColor(SquareColor(i))
            self.squares[i].find("**/polygon").node().setIntoCollideMask(
                BitMask32.bit(1))
            self.squares[i].find("**/polygon").node().setTag('square', str(i))

        pieceOrder = (Rook, Knight, Bishop, Queen, King, Bishop, Knight, Rook)
        for i in range(8, 16):
            self.pieces[i] = Pawn(i, WHITE, self.pieceRoot)
        for i in range(48, 56):
            self.pieces[i] = Pawn(i, PIECEBLACK, self.pieceRoot)
        for i in range(8):
            self.pieces[i] = pieceOrder[i](i, WHITE, self.pieceRoot)
            self.pieces[i + 56] = pieceOrder[i](i + 56, PIECEBLACK, self.pieceRoot)

        self.hiSq = False
        self.dragging = False
        self.in_game = False

        # The board starts hidden behind the menu.
        self.squareRoot.hide()
        self.pieceRoot.hide()

        # Picking task and click handlers run continuously but only act in-game.
        self.mouseTask = taskMgr.add(self.mouseTask, 'mouseTask')
        self.accept("mouse1", self.grabPiece)
        self.accept("mouse1-up", self.releasePiece)

        # Build the UI overlay last, once the 3D scene exists.
        self.ui = ChessUI(self, self)

        # Keys: Escape quits, F1/` toggles the RmlUi debugger.
        self.accept("escape", self.userExit)

        def toggle_dbg():
            self.ui.region.set_debugger_visible(
                not self.ui.region.is_debugger_visible())
        self.accept("f1", toggle_dbg)
        self.accept("`", toggle_dbg)

    # ── Game state transitions driven by the UI ────────────────────────────

    def enter_game(self):
        self.in_game = True
        self.squareRoot.show()
        self.pieceRoot.show()

    def exit_game(self):
        if not self.in_game and self.squareRoot.isHidden():
            return
        self.in_game = False
        # Drop any piece being dragged and clear highlight.
        self.dragging = False
        if self.hiSq is not False:
            self.squares[self.hiSq].setColor(SquareColor(self.hiSq))
            self.hiSq = False
        self.squareRoot.hide()
        self.pieceRoot.hide()

    # ── Picking / dragging ─────────────────────────────────────────────────

    def swapPieces(self, fr, to):
        temp = self.pieces[fr]
        self.pieces[fr] = self.pieces[to]
        self.pieces[to] = temp
        if self.pieces[fr]:
            self.pieces[fr].square = fr
            self.pieces[fr].obj.setPos(SquarePos(fr))
        if self.pieces[to]:
            self.pieces[to].square = to
            self.pieces[to].obj.setPos(SquarePos(to))

    def mouseTask(self, task):
        # Picking only matters while the live board is shown.
        if not self.in_game:
            return Task.cont

        if self.hiSq is not False:
            self.squares[self.hiSq].setColor(SquareColor(self.hiSq))
            self.hiSq = False

        if self.mouseWatcherNode.hasMouse():
            mpos = self.mouseWatcherNode.getMouse()
            self.pickerRay.setFromLens(self.camNode, mpos.getX(), mpos.getY())

            if self.dragging is not False:
                nearPoint = render.getRelativePoint(
                    camera, self.pickerRay.getOrigin())
                nearVec = render.getRelativeVector(
                    camera, self.pickerRay.getDirection())
                self.pieces[self.dragging].obj.setPos(
                    PointAtZ(.5, nearPoint, nearVec))

            self.picker.traverse(self.squareRoot)
            if self.pq.getNumEntries() > 0:
                self.pq.sortEntries()
                i = int(self.pq.getEntry(0).getIntoNode().getTag('square'))
                self.squares[i].setColor(HIGHLIGHT)
                self.hiSq = i

        return Task.cont

    def grabPiece(self):
        if not self.in_game:
            return
        if self.hiSq is not False and self.pieces[self.hiSq]:
            self.dragging = self.hiSq
            self.hiSq = False

    def releasePiece(self):
        if not self.in_game:
            return
        if self.dragging is not False:
            if self.hiSq is False:
                self.pieces[self.dragging].obj.setPos(SquarePos(self.dragging))
            else:
                self.swapPieces(self.dragging, self.hiSq)
        self.dragging = False

    def setupLights(self):
        ambientLight = AmbientLight("ambientLight")
        ambientLight.setColor((.8, .8, .8, 1))
        directionalLight = DirectionalLight("directionalLight")
        directionalLight.setDirection(LVector3(0, 45, -45))
        directionalLight.setColor((0.2, 0.2, 0.2, 1))
        render.setLight(render.attachNewNode(directionalLight))
        render.setLight(render.attachNewNode(ambientLight))


# Class for a piece: loads the model, parents it, sets initial position and color.
class Piece(object):
    def __init__(self, square, color, parent):
        self.obj = loader.loadModel(self.model)
        self.obj.reparentTo(parent)
        self.obj.setColor(color)
        self.obj.setPos(SquarePos(square))
        self.square = square


class Pawn(Piece):
    model = "models/pawn"

class King(Piece):
    model = "models/king"

class Queen(Piece):
    model = "models/queen"

class Bishop(Piece):
    model = "models/bishop"

class Knight(Piece):
    model = "models/knight"

class Rook(Piece):
    model = "models/rook"


# Do the main initialization and start 3D rendering
demo = ChessboardDemo()
demo.run()
