"""Repro for a KNOWN-OPEN issue: `Invalid Resource` device-lost with
window-type=offscreen on Vulkan/MoltenVK.

This bug is NOT fixed -- the script is kept as the standalone reproduction and the
record of what has been ruled out.

Observed: rendering ANY scene to an OFFSCREEN window (window-type offscreen) on the
Vulkan backend triggers, after the frames otherwise render fine ("ALL FRAMES OK"):

  VK_ERROR_OUT_OF_DEVICE_MEMORY: Lost VkDevice after MTLCommandBuffer "#N" execution
  failed (code 9): Invalid Resource (00000009:kIOGPUCommandBufferCallbackErrorInvalidResource)

Scope / what it is NOT (all falsified by bisection):
  - NOT simplepbr-specific: reproduces with a bare `models/box` and no simplepbr.
  - NOT FilterManager / float buffers: reproduces with no FilterManager at all.
  - NOT the glTF chess textures: those upload + bind + sample correctly (pack_bgr8
    BGR->RGBA matches the GL backend's GL_BGR external format for F_rgb/F_srgb).
  - It is triggered specifically by `window-type offscreen`.  A REAL on-screen
    window renders the identical scene with NO Invalid Resource.

So this is an offscreen-output / headless command-buffer resource-lifetime issue in
the Vulkan backend, surfacing at frame/teardown.  It does NOT affect on-screen
rendering, which is why interactive windows render cleanly.

Run (see repro/README.md for the environment; shows the error):
  PYTHONPATH=<panda>/built \
  VK_ICD_FILENAMES=<...>/MoltenVK_icd.json \
  PYTHONUNBUFFERED=1 python3.13 repro/repro_offscreen_invalid_resource.py

Set OFFSCREEN=0 in the environment to render the same scene on-screen and confirm
the error vanishes.
"""
import os

OFFSCREEN = os.environ.get("OFFSCREEN", "1") != "0"

from panda3d.core import load_prc_file_data
load_prc_file_data("", "load-display p3vulkandisplay\n")
load_prc_file_data("", "win-size 320 240\n")
if OFFSCREEN:
    load_prc_file_data("", "window-type offscreen\n")

from direct.showbase.ShowBase import ShowBase


class App(ShowBase):
    def __init__(self):
        super().__init__()
        m = self.loader.loadModel("models/box")
        m.reparentTo(self.render)
        self.camera.setPos(5, -15, 8)
        self.camera.lookAt(0, 0, 0)
        self._n = 0

        if OFFSCREEN:
            # Drive frames directly; the device-lost prints after these.
            for _ in range(8):
                self.taskMgr.step()
            print("ALL FRAMES OK", flush=True)
        else:
            self.taskMgr.add(self._tick, "tick")

    def _tick(self, task):
        self._n += 1
        if self._n >= 8:
            print("ALL FRAMES OK", flush=True)
            self.userExit()
        return task.cont


App()
if not OFFSCREEN:
    __import__("builtins").base.run()
