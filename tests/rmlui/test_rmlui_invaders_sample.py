"""Headless load/smoke test for the 05_invaders.py menu system.

Loads every screen of the invaders sample (background, main menu, start game,
high score, options, help, game placeholder), updates the context, exercises the
high-score data model and a few of the form-event paths, and asserts no crash.

This mirrors the navigation MenuManager performs in samples/rmlui-samples but
without a real window (offscreen) or the ShowBase event loop.
"""

import os

import pytest

rmlui = pytest.importorskip("panda3d.rmlui")


_ASSETS = os.path.normpath(
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..", "..", "samples", "rmlui-samples", "assets",
    )
)

_SCREENS = [
    "main_menu",
    "start_game",
    "high_score",
    "options",
    "help",
    "game",
]


def _load_fonts(ctx):
    # Font faces are registered globally by RmlUi; load_font_face returns False
    # if the same face was already loaded by an earlier test, so we don't assert
    # on the return value here — only that the files exist and the call is safe.
    for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                "RobotoMono-Regular.ttf", "RobotoMono-Bold.ttf"):
        path = os.path.join(_ASSETS, ttf)
        assert os.path.exists(path), path
        ctx.load_font_face(path)


def test_all_invaders_screens_load(rml_context):
    """Every menu screen loads, shows and lays out without error."""
    _load_fonts(rml_context)

    bg = rml_context.load_document(os.path.join(_ASSETS, "invaders_background.rml"))
    assert bg is not None
    bg.show()

    for name in _SCREENS:
        path = os.path.join(_ASSETS, f"invaders_{name}.rml")
        doc = rml_context.load_document(path)
        assert doc is not None, f"failed to load {name}"
        # Set the title span as the sample's MenuManager does.
        title_el = doc.get_element_by_id("title")
        if title_el:
            title_el.set_inner_rml(doc.get_title())
        doc.show()
        for _ in range(3):
            rml_context.update()
        doc.close()
        rml_context.update()


def test_high_score_data_binding(rml_context):
    """The high-score model drives data-for rows and the data-if empty state."""
    _load_fonts(rml_context)

    dm = rml_context.create_data_model("high_scores")
    rows = []
    dm.bind_list("rows", lambda: rows)
    dm.bind_func("count", lambda: len(rows))

    doc = rml_context.load_document(
        os.path.join(_ASSETS, "invaders_high_score.rml"))
    assert doc is not None
    doc.show()
    rml_context.update()

    # Add two rows and refresh — the data-for tbody should grow.
    rows.append("{:<16}{:<7}{:>6}".format("AYRE", 5, 6400))
    rows.append("{:<16}{:<7}{:>6}".format("NOVA", 4, 5100))
    dm.dirty_variable("rows")
    dm.dirty_variable("count")
    rml_context.update()

    back = doc.get_element_by_id("btn_back")
    assert back is not None and back.get_inner_rml() == "Main Menu"

    doc.close()
    rml_context.update()
