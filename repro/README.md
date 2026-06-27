### Vulkan backend / SPIR-V shaderpipeline repros for bugs that were found and fixed 

- `repro_hoist_all_resource_struct.py` | `SpirVHoistStructResourcesPass` -- asserting on a uniform struct whose members are all samplers (the struct is emptied + removed, leaving stale references to its deleted pointer type)
- `repro_many_render_textures.py` -- The backend deadlocking (CPU spin) when many render-texture buffers are created within one clock frame before the first frame is rendered (deferred work is parked, never submitted, so its command buffers are never reclaimed)
- `repro_transform_table_null_deref.py` -- The transform-table binding dereferencing a null data reader when fetched at state-bind time (before a draw sets the reader) — a shader declaring `p3d_TransformTable` SIGSEGV'd
- `repro_tristrip_cut_index.py` -- Multi-strip GeomTristrips (primitive restart / strip-cut index): the backend advertised `GR_strip_cut_index` but drew strips with `primitiveRestartEnable = FALSE`. Per the Vulkan spec that makes the cut index a real vertex (a bridging primitive) on conformant desktop drivers; on MoltenVK it's masked (Metal can't disable restart) and only produced a cosmetic warning. **Passes on MoltenVK with or without the bug — it's the desktop-Vulkan oracle; the desktop artifact is spec-implied and still to be confirmed on NVIDIA/AMD.**
- `repro_filtermanager_flip.py` -- Interactive oracle for the GL→Vulkan Y-flip: a render-to-texture (FilterManager) pass vs. the direct-to-window pass must end up the same way up
- `repro_offscreen_invalid_resource.py` -- **KNOWN-OPEN (not fixed):** `window-type offscreen` rendering triggers an `Invalid Resource` device-lost at frame/teardown; on-screen rendering of the same scene is clean

`repro_hoist_all_resource_struct.py`, `repro_many_render_textures.py` and
`repro_transform_table_null_deref.py` are self-checking (they print a SUCCESS /
CREATED / ALL FRAMES OK line and exit). `repro_filtermanager_flip.py` opens a window
and is judged by eye. `repro_offscreen_invalid_resource.py` reproduces a bug that is
**still open** — it is kept as the reproduction and the ruled-out record, not as a
passing test.

## Running

These need a Vulkan build of Panda and a Vulkan ICD. On macOS that is
MoltenVK; on Linux it is the native driver loader. Use the same Python the build
was made with (on macOS that is the framework Python, not Homebrew).

```bash
# macOS / MoltenVK example
PYTHONPATH=<path-to-panda>/built \
VK_ICD_FILENAMES=/opt/homebrew/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json \
PYTHONUNBUFFERED=1 \
python3.13 repro/repro_hoist_all_resource_struct.py

# Linux / native Vulkan example
PYTHONPATH=<path-to-panda>/built \
PYTHONUNBUFFERED=1 \
python3 repro/repro_many_render_textures.py
```
