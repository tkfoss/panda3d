"""Standalone repro for the Vulkan transform-table null-deref crash (FIXED).

On the Vulkan/MoltenVK backend, binding a shader that declares a
`p3d_TransformTable` uniform (e.g. any panda3d-simplepbr shader, whose vertex
shader declares `uniform mat4 p3d_TransformTable[100];`) SIGSEGV'd with
EXC_BAD_ACCESS at offset 0x10.

Root cause: VulkanGraphicsStateGuardian::set_state_and_transform() calls
VulkanShaderContext::update_dynamic_uniforms(), which fetches every binding in
`_other_state_block` -- and the transform-table binding (state dep D_vertex_data)
is placed in that block. set_state_and_transform runs at STATE-BIND time, before
begin_draw_primitives() sets `_data_reader`, so the lambda in make_transform_table()
(panda/src/display/shaderInputBinding_impls.cxx) did:

    state.gsg->get_data_reader()->get_transform_table()

with get_data_reader() == nullptr -> null deref.  The GL backend doesn't hit this
because it fetches transform-table data during the actual draw (reader valid).

Fix: guard the data reader against null in make_transform_table (both the 4-row and
3-row branches) and make_slider_table -- treat a null reader the same as a missing
table (identity matrices / zero sliders).  See the comments in
panda/src/display/shaderInputBinding_impls.cxx.

This script crashed (exit 139 / SIGSEGV) BEFORE the fix.  AFTER it the null deref is
gone.  Note: driving simplepbr through the Vulkan path this way can still surface
separate, independent issues that are not this bug (a host-vs-shader vertex
attribute type mismatch reported by MoltenVK, and the alpha-test SPIR-V patch
offset landing on a non-comparison opcode for some shaders); those are unrelated to
the transform-table null deref this repro is about.

Run (see repro/README.md for the environment):
  PYTHONPATH=<panda>/built \
  VK_ICD_FILENAMES=<...>/MoltenVK_icd.json \
  PYTHONUNBUFFERED=1 python3.13 repro/repro_transform_table_null_deref.py
"""
from panda3d.core import load_prc_file_data

load_prc_file_data("", "load-display p3vulkandisplay\nwindow-type offscreen\nwin-size 640 360\n")

from direct.showbase.ShowBase import ShowBase
from panda3d.core import AmbientLight, PointLight
import simplepbr  # pip install --no-deps panda3d-simplepbr typing_extensions


def main():
    base = ShowBase()
    # use_330=True: the Vulkan/SPIR-V path needs modern GLSL. The simplepbr shaders
    # declare p3d_TransformTable, which is what triggers the crash.
    simplepbr.init(use_330=True, msaa_samples=0)

    # Any geometry suffices; the shader binding is what matters.
    model = base.loader.loadModel("models/panda")
    model.reparentTo(base.render)

    light = base.render.attachNewNode(PointLight("light"))
    light.setPos(20, -20, 30)
    base.render.setLight(light)
    amb = base.render.attachNewNode(AmbientLight("ambient"))
    amb.node().setColor((0.3, 0.3, 0.3, 1))
    base.render.setLight(amb)

    base.camera.setPos(0, -40, 8)
    base.camera.lookAt(0, 0, 4)

    # Step the task manager so simplepbr's update task runs and the scene is drawn.
    for i in range(8):
        base.taskMgr.step()
        print(f"step {i} OK", flush=True)
    print("ALL FRAMES OK", flush=True)


if __name__ == "__main__":
    main()
