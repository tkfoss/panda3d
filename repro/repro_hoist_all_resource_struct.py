"""Standalone repro: SpirVHoistStructResourcesPass crashed on a uniform struct
whose members are ALL opaque resources (samplers).

The trigger is narrow: a uniform struct whose members are *all* resources, e.g.
a material descriptor `struct { sampler2D albedo, normal, roughness, specular; }`.
SpirVHoistStructResourcesPass lifts the samplers to global scope; the struct is
then empty and (because the pass is constructed with _remove_empty_structs=true)
removed, which also deletes the OpTypePointer to it. The struct's leftover
OpVariable / OpAccessChain instructions still reference that deleted, never-defined
pointer type, so the pre-fix pass asserted in unwrap_pointer_type():

    AssertionError: is_defined(id) at panda/src/shaderpipeline/spirVTransformPass.I:36

on the first rendered frame.

A struct that MIXES resources and plain data (the `mix-struct control` at the
bottom) is never emptied, so its pointer type is never deleted and it takes the
unchanged code path -- it passes both before and after the fix. That control is
why a naive single-sampler+vec4 repro did not reproduce the bug.

Expected:
  * pre-fix : asserts is_defined(id) at spirVTransformPass.I:36
  * post-fix: prints "SUCCESS: rendered two frames with all-sampler struct shader";
              the transformed SPIR-V validates and the four samplers become four
              global OpVariable UniformConstant correctly decorated + referenced.

Run (see repro/README.md for the environment):
    PYTHONPATH=<panda>/built \
    VK_ICD_FILENAMES=<...>/MoltenVK_icd.json \
    PYTHONUNBUFFERED=1 python3.13 repro/repro_hoist_all_resource_struct.py
"""
from panda3d.core import load_prc_file_data
load_prc_file_data('', 'load-display p3vulkandisplay')
load_prc_file_data('', 'win-size 64 64')
load_prc_file_data('', 'window-type offscreen')

from direct.showbase.ShowBase import ShowBase
from panda3d.core import Shader, Texture, CardMaker, NodePath

VERT = """#version 430
in vec4 p3d_Vertex;
in vec2 p3d_MultiTexCoord0;
out vec2 texcoord;
uniform mat4 p3d_ModelViewProjectionMatrix;
void main() {
    gl_Position = p3d_ModelViewProjectionMatrix * p3d_Vertex;
    texcoord = p3d_MultiTexCoord0;
}
"""

# A uniform struct whose members are ALL samplers. After the samplers are hoisted
# to global scope the struct is empty and gets removed -> the pointer type to the
# struct is deleted -> the struct's OpVariable references a deleted pointer type.
# This is the exact construct that crashed in RenderPipeline's gbuffer/material
# shaders.
FRAG = """#version 430
in vec2 texcoord;
out vec4 color;

struct Material {
    sampler2D albedo;
    sampler2D normal;
    sampler2D roughness;
    sampler2D specular;
};

uniform Material material;

void main() {
    color = texture(material.albedo, texcoord)
          + texture(material.normal, texcoord)
          + texture(material.roughness, texcoord)
          + texture(material.specular, texcoord);
}
"""

base = ShowBase()

shader = Shader.make(Shader.SL_GLSL, VERT, FRAG)
assert shader is not None, "shader failed to compile"

cm = CardMaker('card')
cm.set_frame(-1, 1, -1, 1)
card = NodePath(cm.generate())
card.set_shader(shader)

for name in ('albedo', 'normal', 'roughness', 'specular'):
    tex = Texture(name)
    tex.setup_2d_texture(4, 4, Texture.T_unsigned_byte, Texture.F_rgba)
    card.set_shader_input('material.' + name, tex)
card.reparent_to(base.render2d)

print('>>> rendering frame 1', flush=True)
base.graphics_engine.render_frame()
print('>>> rendering frame 2', flush=True)
base.graphics_engine.render_frame()
print('>>> SUCCESS: rendered two frames with all-sampler struct shader', flush=True)
base.userExit()

# ---------------------------------------------------------------------------
# Mix-struct control (the common path): a struct with one resource + one plain
# member is NOT emptied, so its pointer type is never deleted and the original
# (unchanged) code path is taken. Passes both before and after the fix, proving
# the fix is scoped to the all-resource / empty-struct case. To run the control,
# swap FRAG for the below and bind 'material.albedo' (a texture) + 'material.tint'
# (a colour):
#
#   FRAG = '''#version 430
#   in vec2 texcoord;
#   out vec4 color;
#   struct Material { vec4 tint; sampler2D albedo; };
#   uniform Material material;
#   void main() { color = texture(material.albedo, texcoord) * material.tint; }
#   '''
#   card.set_shader_input('material.albedo', tex)
#   card.set_shader_input('material.tint', (1, 1, 1, 1))
