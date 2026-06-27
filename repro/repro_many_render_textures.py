"""Standalone repro: the Vulkan backend deadlocked (100%-CPU infinite spin) when
many render-texture-backed offscreen buffers are created within a single clock
frame, before the first frame is rendered.

No RenderPipeline involved. Deterministic: pre-fix, the loop gets to target 18
then hangs forever on the 19th make_output(). (RenderPipeline hit this at init
creating 30+ render targets -- PSSM shadow stages + scattering envmap + the
CubemapFilter specular mip chain -- and deadlocked in CubemapFilter at the ~19th
target.)

Mechanism: with no render_frame() between buffers the clock frame never advances,
so end_frame() parks (does not submit) each offscreen buffer's work; its command
buffers accumulate and its timeline-semaphore watermark stays 0 (never submitted ->
never signalled). The fixed command-buffer pool drains ~1-2 per buffer; once it is
empty, create_command_buffer()'s reclaim loop walks the parked frames, each
wait_semaphore(sem, 0) returning instantly while reclaiming nothing -> infinite
spin, no buffer ever freed.

This is NOT MoltenVK-specific: the deferral logic, the fixed pool, and the
under-guarded reclaim loop are all platform-independent. A native Vulkan driver
hits the identical CPU spin (the timeline semaphore legitimately never reaches the
watermark of work that was never submitted).

The fix forces a flush + frame boundary in begin_frame() when the command-buffer
pool is running low -- exactly what a clock tick / render_frame() does, but
triggered by resource pressure instead -- which submits the parked work, advances
the timeline, and lets reclamation succeed.

Expected:
  * pre-fix : prints "target 0 OK" ... "target 18 OK" then hangs (kill it).
  * post-fix: prints "CREATED 40" and exits cleanly, with NO render_frame() added.

Controls (the discriminators that pinned the mechanism):
  * The same loop WITHOUT add_render_texture() completes fine pre-fix (no parking).
  * Calling graphics_engine.render_frame() between creations also completes fine
    pre-fix (the workaround that proved the mechanism).

Run (see repro/README.md for the environment):
    PYTHONPATH=<panda>/built \
    VK_ICD_FILENAMES=<...>/MoltenVK_icd.json \
    PYTHONUNBUFFERED=1 python3.13 repro/repro_many_render_textures.py
"""
from panda3d.core import load_prc_file_data
load_prc_file_data('', 'load-display p3vulkandisplay')
load_prc_file_data('', 'win-size 200 200')

from direct.showbase.ShowBase import ShowBase
from panda3d.core import (GraphicsPipe, FrameBufferProperties, WindowProperties,
                          Texture, GraphicsOutput)

b = ShowBase()
eng = b.graphics_engine
pipe = b.win.get_pipe()
bufs = []
for i in range(40):
    fbp = FrameBufferProperties()
    fbp.set_rgba_bits(11, 11, 10, 0)
    wp = WindowProperties.size(12, 2)
    buf = eng.make_output(pipe, 'b%d' % i, 1, fbp, wp,
        GraphicsPipe.BF_refuse_window | GraphicsPipe.BF_resizeable, b.win.gsg, b.win)
    if not buf:
        print(i, 'make_output FAIL')
        break
    # Binding a render texture (RTM_bind_or_copy) is required to trigger the bug;
    # the same loop without this line completes fine even pre-fix.
    t = Texture('c%d' % i)
    t.set_x_size(12)
    t.set_y_size(2)
    buf.add_render_texture(t, GraphicsOutput.RTM_bind_or_copy, GraphicsOutput.RTP_color)
    bufs.append(buf)
    print('target', i, 'OK', flush=True)
print('CREATED', len(bufs))
b.userExit()
