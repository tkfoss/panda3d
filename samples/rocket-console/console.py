"""
Console widget for the rmlui rocket-console sample.

Manages a scrolling text buffer rendered via RmlUi's set_inner_rml.
Key input is forwarded from Panda3D's keyboard events.
"""
import html


class Console:
    def __init__(self, base, doc, cols, rows, command_handler):
        self.base = base
        self.doc = doc
        self.cols = cols
        self.rows = rows
        self.command_handler = command_handler

        self._lines = []          # list of plain-text strings
        self._input = ""
        self._prompt = "C:\\>"
        self._edit_mode = False
        self._blink = False

        self._content_el = doc.getElementById("content")
        assert self._content_el, "console.rml must have a div#content"

        self.allow_editing(True)

    # ------------------------------------------------------------------

    def get_prompt(self):
        return self._prompt

    def set_prompt(self, prompt):
        self._prompt = prompt
        self._render()

    def allow_editing(self, edit_mode):
        self._edit_mode = edit_mode
        if edit_mode:
            self._input = ""
        self._render()
        if edit_mode:
            self._queue_blink()

    def add_line(self, text):
        self._lines.append(text)
        while len(self._lines) > self.rows:
            self._lines.pop(0)
        self._render()

    def add_lines(self, lines):
        for line in lines:
            self.add_line(line)

    def cls(self):
        self._lines = []
        self._render()

    # ------------------------------------------------------------------

    def _render(self):
        parts = []
        for line in self._lines:
            parts.append(html.escape(line))
        if self._edit_mode:
            cursor = "_" if self._blink else " "
            parts.append(html.escape(self._prompt + self._input) + cursor)
        self._content_el.setInnerRml("\n".join(parts))

    def _queue_blink(self):
        self.base.taskMgr.doMethodLater(0.4, self._blink_tick, "console-blink")

    def _blink_tick(self, task):
        if not self._edit_mode:
            return task.done
        self._blink = not self._blink
        self._render()
        self._queue_blink()
        return task.done

    # ------------------------------------------------------------------
    # Keyboard input — called by main.py via Panda3D key events

    def handle_char(self, keyname):
        """Called for printable character keys."""
        if not self._edit_mode:
            return
        if len(keyname) == 1 and 32 <= ord(keyname) < 127:
            self._input += keyname
            self._render()

    def handle_backspace(self):
        if not self._edit_mode:
            return
        self._input = self._input[:-1]
        self._render()

    def handle_enter(self):
        if not self._edit_mode:
            return
        cmd = self._input
        self._input = ""
        self.add_line(self._prompt + cmd)
        self.command_handler(cmd)
        if self._edit_mode:
            self._render()

    def handle_break(self):
        self.command_handler(None)
