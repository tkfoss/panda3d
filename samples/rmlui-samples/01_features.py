"""
Panda3D RmlUi — Feature Showcase
=================================
Eight-tab reference for the CSS / RmlUi feature set:

  Welcome      — source guide
  Font & Text  — font-effect: glow, outline, shadow, blur; emoji; sizes
  Animations   — @keyframes cube, transition: :hover, easing gallery
  Decorators   — gradients, tiled-box, spritesheet, image-color, fit modes
  Transforms   — translate, rotate, scale, skew, 3D perspective
  Filters      — filter: blur, grayscale, sepia, brightness, …
  Forms        — all input types; submit wired to Python callback
  Live/Python  — per-frame DOM updates via set_inner_rml / set_attribute
  DOM API      — geometry, traversal, create/append, context queries, scroll
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
        self._created_items = []   # RmlElement refs for the create/append demo
        self._create_counter = 0
        self._attr_flash = False
        self._last_hover_id = None
        self._last_focus_id = None

        self._setup_rmlui()
        self._setup_input()
        self._setup_domapi()
        self.taskMgr.add(self._update, "rmlui-update")

    def _setup_rmlui(self):
        self.input_handler = RmlInputHandler()
        self.mouseWatcher.attachNewNode(self.input_handler)

        self.rml = RmlRegion.make("features", self.win)
        self.rml.set_input_handler(self.input_handler)

        ctx = self.rml.get_context()
        for ttf in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf",
                    "LatoLatin-Italic.ttf", "LatoLatin-BoldItalic.ttf"):
            ctx.load_font_face(os.path.join(ASSETS, ttf))

        self._doc = ctx.load_document(os.path.join(ASSETS, "features.rml"))
        if not self._doc:
            raise RuntimeError("features.rml not found")
        self._doc.show()
        self.rml.init_debugger()

        ids = ["fps", "frame-count", "time-val", "sin-val",
               "progress-fill", "color-swatch",
               "ping-count", "form-output",
               "toggle-status",
               "tog0", "tog1", "tog2", "tog3",
               "speed-label", "speed-slider",
               # DOM API tab
               "geo-target", "geo-rel", "geo-abs", "geo-size",
               "attr-target", "attr-has", "attr-hover",
               "traverse-root", "trav-count", "trav-qs", "trav-parent",
               "create-container", "create-count",
               "ctx-interacting", "ctx-hover", "ctx-focus", "ctx-numdocs",
               "scroll-target", "scroll-top"]
        self._el = {k: self._doc.get_element_by_id(k) for k in ids}

        # Store every element we attach listeners to — RmlElement wrappers must
        # stay alive or RemoveEventListener fires on GC and silently drops the callback.
        for extra_id in ("ping-btn", "submit-btn", "demo-form",
                         "attr-flash-btn",
                         "create-add-btn", "create-remove-btn",
                         "scroll-to-btn", "scroll-top-btn"):
            if extra_id not in self._el:
                self._el[extra_id] = self._doc.get_element_by_id(extra_id)

        ping_btn = self._el.get("ping-btn")
        if ping_btn:
            ping_btn.add_event_listener("click", self._on_ping)

        for i in range(4):
            el = self._el.get(f"tog{i}")
            if el:
                el.add_event_listener("click", lambda ev, idx=i: self._on_toggle(idx))

        form = self._el.get("demo-form")
        if form:
            form.add_event_listener("submit", self._on_form_submit)
        submit_btn = self._el.get("submit-btn")
        if submit_btn:
            submit_btn.add_event_listener("click", self._on_form_submit)

    def _setup_input(self):
        self.accept("escape", self.userExit)
        def toggle_dbg():
            self.rml.set_debugger_visible(not self.rml.is_debugger_visible())
        self.accept("f1", toggle_dbg)
        self.accept("`", toggle_dbg)

    def _on_ping(self, ev):
        self._ping_count += 1
        self._set("ping-count", str(self._ping_count))

    def _on_toggle(self, idx):
        self._toggle_state[idx] = not self._toggle_state[idx]
        el = self._el.get(f"tog{idx}")
        if el:
            el.set_class("active", self._toggle_state[idx])
        active = [chr(65 + i) for i, s in enumerate(self._toggle_state) if s]
        status = f"Active: {', '.join(active)}" if active else "click a square"
        self._set("toggle-status", html.escape(status))

    def _on_form_submit(self, ev):
        el = self._el.get("form-output")
        if el:
            el.set_inner_rml("<p>Form submitted!</p>")

    # ── DOM API tab setup ──────────────────────────────────────────────

    def _setup_domapi(self):
        # All elements are already in self._el (pre-populated in _setup_rmlui).

        # A3 flash button
        flash_btn = self._el.get("attr-flash-btn")
        if flash_btn:
            def _on_flash(ev):
                t = self._el.get("attr-target")
                if not t:
                    return
                self._attr_flash = not self._attr_flash
                t.set_class("api-flash", self._attr_flash)
            flash_btn.add_event_listener("click", _on_flash)

        # C4 + A8 — add/remove dynamically created elements
        add_btn = self._el.get("create-add-btn")
        if add_btn:
            def _on_add(ev):
                container = self._el.get("create-container")
                if not container:
                    return
                self._create_counter += 1
                el = self._doc.create_element("div")
                if not el:
                    return
                el.set_class("api-item", True)
                el.set_inner_rml(f"Dynamic item #{self._create_counter}")
                appended = container.append_child(el)
                if appended:
                    self._created_items.append(appended)
                self._set("create-count", str(len(self._created_items)))
            add_btn.add_event_listener("click", _on_add)

        remove_btn = self._el.get("create-remove-btn")
        if remove_btn:
            def _on_remove(ev):
                container = self._el.get("create-container")
                if not container or not self._created_items:
                    return
                last = self._created_items.pop()
                container.remove_child(last)
                self._set("create-count", str(len(self._created_items)))
            remove_btn.add_event_listener("click", _on_remove)

        # A5 — scroll_into_view buttons
        scroll_to = self._el.get("scroll-to-btn")
        if scroll_to:
            def _on_scroll_to(ev):
                t = self._el.get("scroll-target")
                if t:
                    t.scroll_into_view(False)
            scroll_to.add_event_listener("click", _on_scroll_to)

        scroll_top_btn = self._el.get("scroll-top-btn")
        if scroll_top_btn:
            def _on_scroll_top(ev):
                t = self._el.get("scroll-top")
                if t:
                    t.scroll_into_view(True)
            scroll_top_btn.add_event_listener("click", _on_scroll_top)

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
            fill.set_attribute("style", f"width: {s * 100:.1f}%;")

        r, g, b = _hsv_to_rgb((t * 40) % 360, 0.75, 0.95)
        swatch = self._el.get("color-swatch")
        if swatch:
            swatch.set_attribute(
                "style",
                f"background-color: rgb({int(r*255)},{int(g*255)},{int(b*255)});")

        # Range slider label
        slider = self._el.get("speed-slider")
        label = self._el.get("speed-label")
        if slider and label:
            label.set_inner_rml(slider.get_value().split(".")[0])

        self._update_domapi()
        return task.cont

    def _update_domapi(self):
        # A1 — geometry queries
        geo = self._el.get("geo-target")
        if geo:
            rel = geo.get_relative_offset()
            abs_ = geo.get_absolute_offset()
            w = geo.get_offset_width()
            h = geo.get_offset_height()
            self._set("geo-rel",  f"({rel.x:.0f}, {rel.y:.0f})")
            self._set("geo-abs",  f"({abs_.x:.0f}, {abs_.y:.0f})")
            self._set("geo-size", f"{w:.0f} × {h:.0f} dp")

        # A2 + A4 — attribute check and pseudo-class
        attr = self._el.get("attr-target")
        if attr:
            self._set("attr-has",   str(attr.has_attribute("data-demo")))
            self._set("attr-hover", str(attr.is_pseudo_class_set("hover")))

        # A6 + A7 — traversal
        root = self._el.get("traverse-root")
        if root:
            count = root.get_num_children()
            self._set("trav-count", str(count))
            first = root.query_selector(".tspan")
            self._set("trav-qs", first.get_inner_rml() if first else "none")
            parent = root.get_parent_node()
            if parent:
                pid = parent.get_id()
                self._set("trav-parent", f'"{pid}"' if pid else "(anonymous div)")
            else:
                self._set("trav-parent", "none")

        # B1 + B2 — context queries
        ctx = self.rml.get_context()
        self._set("ctx-interacting", str(ctx.is_mouse_interacting()))
        hover = ctx.get_hover_element()
        hover_id = hover.get_id() if hover else ""
        if hover_id != self._last_hover_id:
            self._last_hover_id = hover_id
            self._set("ctx-hover", f'"{hover_id}"' if hover_id else "(none)")
        focus = ctx.get_focus_element()
        focus_id = focus.get_id() if focus else ""
        if focus_id != self._last_focus_id:
            self._last_focus_id = focus_id
            self._set("ctx-focus", f'"{focus_id}"' if focus_id else "(none)")

        # B3 — num_documents (includes debugger docs)
        self._set("ctx-numdocs", str(ctx.get_num_documents()))

    def _set(self, key, text):
        el = self._el.get(key)
        if el:
            el.set_inner_rml(text)


FeatureShowcase().run()
