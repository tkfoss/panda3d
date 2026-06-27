"""Standalone repro: multi-strip GeomTristrips (primitive-restart / strip-cut) on
the Vulkan backend.

A single GeomTristrips can hold SEVERAL triangle strips, joined in one index
buffer by a "strip cut" index (the highest value for the index type: 0xFF / 0xFFFF
/ 0xFFFFFFFF).  The GPU must be told to treat that index as a primitive-restart
marker (VkPipelineInputAssemblyStateCreateInfo::primitiveRestartEnable = TRUE on
strip/fan topologies, the Vulkan "fixed" restart index).  Panda advertises this
capability as Geom::GR_strip_cut_index.

If the Vulkan backend advertises GR_strip_cut_index but then draws strips with
primitiveRestartEnable = FALSE, the cut index is read as a real vertex index
instead of a cut: a stray "bridging" triangle is drawn spanning the two strips
(and/or an out-of-range index is fetched).  On 8-bit indices there is a second
hazard: the cut value 0xFF must widen to 0xFFFF (the 16-bit fixed restart index),
not 0x00FF, or the cut is lost even with restart enabled.

IMPORTANT PLATFORM NOTE: on MoltenVK/Metal this repro PASSES even with the bug
present, because Metal physically cannot disable primitive restart on strip
topologies (0xFFFF always cuts), so a wrong primitiveRestartEnable=FALSE setting is
masked -- you only get the (largely cosmetic) "Metal does not support disabling
primitive restart" warning (see KhronosGroup/MoltenVK#2279).  Per the Vulkan spec,
on a CONFORMANT desktop Vulkan driver (NVIDIA/AMD/Intel) FALSE makes the cut index a
real vertex, which should produce a bridging primitive -- but that desktop failure
is SPEC-IMPLIED, not yet observed here.  This script is therefore the DESKTOP-Vulkan
oracle: run it on a conformant driver to actually confirm the artifact appears with
the bug and disappears with the fix.  On macOS it only confirms the index data
round-trips and (post-fix) that the warning is gone.

This builds TWO triangles as TWO separate one-triangle strips in ONE GeomTristrips,
placed in opposite corners with empty space between them, and renders to an
offscreen RGBA buffer.  It then samples a pixel in the middle gap:

  * CORRECT (restart honored): the gap pixel is the BACKGROUND clear color -- the
    two strips are independent, nothing is drawn between them.
  * BUGGED (restart disabled / cut lost): a bridging triangle connects the strips
    and FILLS the gap pixel with the geometry color -> non-background.

It prints PASS / FAIL accordingly, and also exercises 8-bit, 16-bit and 32-bit
index types (the 8-bit case additionally catches the 0xFF -> 0x00FF widening bug).

Run (see repro/README.md for the environment):
  PYTHONPATH=<panda>/built \
  VK_ICD_FILENAMES=<...>/MoltenVK_icd.json \
  PYTHONUNBUFFERED=1 python3.13 repro/repro_tristrip_cut_index.py
"""
from panda3d.core import load_prc_file_data
load_prc_file_data("", "load-display p3vulkandisplay\n")
load_prc_file_data("", "window-type offscreen\n")
load_prc_file_data("", "win-size 64 64\n")
# Keep going through any non-fatal teardown asserts so all three index types run.
load_prc_file_data("", "assert-abort #f\n")

from direct.showbase.ShowBase import ShowBase
from panda3d.core import (
    GeomVertexFormat, GeomVertexData, GeomVertexWriter, GeomTristrips, Geom,
    GeomNode, NodePath, Texture, GraphicsOutput, FrameBufferProperties,
    WindowProperties, GraphicsPipe, LColor, OrthographicLens, Camera,
)

# Two unit quads -> but we draw each as a 1-strip (2 triangles) strip.  Place one
# in the bottom-left of NDC and one in the top-right, leaving the centre empty.
# Coordinates are in the [-1, 1] plane of an orthographic camera looking down -Y.
STRIP_A = [(-0.9, -0.9), (-0.5, -0.9), (-0.9, -0.5), (-0.5, -0.5)]  # bottom-left quad
STRIP_B = [(0.5, 0.5), (0.9, 0.5), (0.5, 0.9), (0.9, 0.9)]          # top-right quad

GEOM_COLOR = (1.0, 0.0, 0.0, 1.0)   # red geometry
CLEAR_COLOR = (0.0, 0.0, 0.0, 1.0)  # black background


def make_two_strip_node(index_type):
    """One GeomTristrips holding two separate 4-vertex strips, joined by a cut."""
    vformat = GeomVertexFormat.get_v3()
    vdata = GeomVertexData("strips", vformat, Geom.UH_static)
    vdata.set_num_rows(8)
    vw = GeomVertexWriter(vdata, "vertex")
    for (x, z) in STRIP_A:
        vw.add_data3(x, 0.0, z)
    for (x, z) in STRIP_B:
        vw.add_data3(x, 0.0, z)

    prim = GeomTristrips(Geom.UH_static)
    prim.set_index_type(index_type)
    # First strip (vertices 0..3), then close it -> inserts a strip-cut index,
    # then the second strip (vertices 4..7).
    prim.add_vertices(0, 1, 2, 3)
    prim.close_primitive()
    prim.add_vertices(4, 5, 6, 7)
    prim.close_primitive()

    geom = Geom(vdata)
    geom.add_primitive(prim)
    node = GeomNode("two_strips")
    node.add_geom(geom)
    return NodePath(node), prim


def run_case(base, index_type, name):
    np_node, prim = make_two_strip_node(index_type)
    print(f"[{name}] strip_cut_index = {hex(prim.get_strip_cut_index())}", flush=True)

    # Offscreen RGBA target with RAM copy so we can read it back.
    fbp = FrameBufferProperties()
    fbp.set_rgba_bits(8, 8, 8, 8)
    fbp.set_depth_bits(1)
    tex = Texture(f"out_{name}")
    buf = base.graphics_engine.make_output(
        base.pipe, f"buf_{name}", -10, fbp, WindowProperties.size(64, 64),
        GraphicsPipe.BF_refuse_window, base.win.get_gsg(), base.win)
    if buf is None:
        print(f"[{name}] FAIL: could not create offscreen buffer", flush=True)
        return None
    buf.add_render_texture(tex, GraphicsOutput.RTM_copy_ram, GraphicsOutput.RTP_color)
    buf.set_clear_color(LColor(*CLEAR_COLOR))

    cam_np = base.make_camera(buf)
    lens = OrthographicLens()
    lens.set_film_size(2.0, 2.0)
    lens.set_near_far(-10, 10)
    cam_np.node().set_lens(lens)
    cam_np.set_pos(0, -5, 0)
    cam_np.look_at(0, 0, 0)

    scene = NodePath(f"scene_{name}")
    np_node.set_color(LColor(*GEOM_COLOR))
    np_node.reparent_to(scene)
    cam_np.reparent_to(scene)
    base.camera = cam_np
    cam_np.node().set_scene(scene)

    base.graphics_engine.render_frame()
    base.graphics_engine.render_frame()

    if not tex.has_ram_image():
        print(f"[{name}] INCONCLUSIVE: no RAM image (readback unavailable)", flush=True)
        return None

    # Sample the centre pixel (in the empty gap between the two strips).
    img = tex.get_ram_image_as("RGBA")
    w, h = tex.get_x_size(), tex.get_y_size()
    cx, cy = w // 2, h // 2
    off = (cy * w + cx) * 4
    r, g, b, a = img[off], img[off + 1], img[off + 2], img[off + 3]
    bridged = r > 32  # red geometry leaking into the gap == bridging triangle
    verdict = "FAIL (bridging triangle in gap -> restart NOT honored)" if bridged \
              else "PASS (gap is background -> strip cut honored)"
    print(f"[{name}] centre pixel RGBA=({r},{g},{b},{a}) -> {verdict}", flush=True)
    return not bridged


def main():
    from panda3d.core import GeomEnums
    base = ShowBase()
    results = {}
    for index_type, name in (
        (GeomEnums.NT_uint16, "uint16"),
        (GeomEnums.NT_uint32, "uint32"),
        (GeomEnums.NT_uint8, "uint8"),
    ):
        results[name] = run_case(base, index_type, name)
    print(">>> RESULTS:", results, flush=True)
    ok = [v for v in results.values() if v is not None]
    if ok and all(ok):
        print(">>> OVERALL PASS: strip-cut honored for all readable index types", flush=True)
    elif any(v is False for v in results.values()):
        print(">>> OVERALL FAIL: at least one index type drew a bridging triangle", flush=True)
    else:
        print(">>> INCONCLUSIVE: readback unavailable (judge on-screen instead)", flush=True)
    base.userExit()


if __name__ == "__main__":
    main()
