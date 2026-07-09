### Building the Vulkan backend on macOS (MoltenVK)

```sh
brew install molten-vk vulkan-loader vulkan-headers vulkan-tools glslang spirv-cross spirv-tools
```

When building use the framework python from python.org not Homebrew's: makepanda only searches 
`$SYSROOT/System/.../Python.framework`, `/Library/Frameworks/Python.framework/Versions/<X.Y>`, 
and the Command Line Tools path, and a C extension built against one Python ABI segfaults if
imported into a different one.

```bash
cd <panda3d>
export VULKAN_SDK=/opt/homebrew           # the Homebrew prefix works directly now
export PYTHONUNBUFFERED=1                 # see note below
PY=/Library/Frameworks/Python.framework/Versions/3.13/bin/python3.13
$PY makepanda/makepanda.py --nothing \
  --use-python --use-direct --use-egg --use-gl --use-vulkan --use-cocoa \
  --use-freetype --use-harfbuzz --use-zlib --use-png --use-jpeg \
  --glslang-incdir=/opt/homebrew/opt/glslang/include       --glslang-libdir=/opt/homebrew/opt/glslang/lib \
  --spirv-cross-incdir=/opt/homebrew/opt/spirv-cross/include --spirv-cross-libdir=/opt/homebrew/opt/spirv-cross/lib \
  --spirv-tools-incdir=/opt/homebrew/opt/spirv-tools/include --spirv-tools-libdir=/opt/homebrew/opt/spirv-tools/lib \
  --threads=14
```

- `--use-cocoa` is required even for a minimal build: the Vulkan window inherits `CocoaGraphicsWindow`
- `PYTHONUNBUFFERED=1`: makepanda's error-exit paths use `os._exit()`, which discards buffered stdout/stderr 
- avoid `--everything` here: it pulls older `darwin-libs-a` thirdparty headers (e.g. OpenEXR) that aren't C++17-clean

Finally run:

```bash
PYTHONPATH=<panda3d>/built \
VK_ICD_FILENAMES=/opt/homebrew/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json \
PYTHONUNBUFFERED=1 \
python3.13 main.py        # your app; remember to set `load-display p3vulkandisplay`
```

### Adding the RmlUi (HTML/CSS UI) integration

This branch adds the `panda3d.rmlui` module (`panda/src/rmlui/`). It is an optional
package; enable it with `--use-rmlui` and point makepanda at an RmlUi install.

Build & install RmlUi first (any recent RmlUi 6.x; built as a shared lib):

```bash
git clone https://github.com/mikke89/RmlUi <rmlui-src>
cmake -S <rmlui-src> -B <rmlui-src>/build \
  -DCMAKE_INSTALL_PREFIX=<rmlui>/install -DBUILD_SHARED_LIBS=ON
cmake --build <rmlui-src>/build --target install -j
```

That yields `<rmlui>/install/include/RmlUi` and `<rmlui>/install/lib/librmlui.dylib`
(+ `librmlui_debugger.dylib`). Then add these flags to the `makepanda.py` invocation above:

```bash
  --use-rmlui \
  --rmlui-incdir=<rmlui>/install/include \
  --rmlui-libdir=<rmlui>/install/lib \
```

makepanda auto-detects RmlUi via `SmartPkgEnable("RMLUI", ...)` looking for
`RmlUi/Core/Core.h`; the incdir/libdir flags make detection explicit. The RmlUi
visual debugger overlay (`HAVE_RMLUI_DEBUGGER`) is only wired up in the CMake build;
makepanda links the core library only.

At runtime, `librmlui.dylib` must be resolvable — add the RmlUi lib dir to the
dynamic loader path alongside `built/lib`:

```bash
DYLD_FALLBACK_LIBRARY_PATH=<panda3d>/built/lib:<rmlui>/install/lib \
PYTHONPATH=<panda3d>/built \
VK_ICD_FILENAMES=/opt/homebrew/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json \
python3.13 samples/rmlui-samples/<sample>.py    # set `load-display p3vulkandisplay`
```

Note on shaders: the RmlUi render interface (`rmlRenderInterface.cxx`) emits
`#version 150` GLSL via `Shader::make(SL_GLSL, ...)`. The shaderpipeline's glslang
frontend special-cases `#version 150` and cross-compiles it to SPIR-V, so the
embedded shaders build unchanged on the Vulkan backend. If an RmlUi sample renders
blank/white on Vulkan, the first suspects are the mid-frame render-target switches in
layer compositing (offscreen `GraphicsBuffer` pool), CSS-filter multi-pass shaders,
and stencil clip masks — see `rmlRegion.cxx` / `rmlRenderInterface.cxx`.

Shader-pipeline tests:

```bash
PYTHONPATH=<panda3d>/built python3.13 -m pytest \
  tests/shaderpipeline tests/gobj/test_shader.py -q
```

RmlUi tests (headless):

```bash
DYLD_FALLBACK_LIBRARY_PATH=<panda3d>/built/lib:<rmlui>/install/lib \
PYTHONPATH=<panda3d>/built python3.13 -m pytest tests/rmlui -q
```

Validation layer: off by default (the `vk-validate` config var). The backend now
links the Vulkan loader, so `VK_LAYER_KHRONOS_validation` can attach when you
enable it; some SDK versions crash inside the layer on complex shader modules, so
it is opt-in. Enable with `vk-validate #t` while debugging.

Why link the loader, not MoltenVK directly: on macOS the backend links `-lvulkan`
(the loader) rather than `-lMoltenVK`. Instance layers such as validation are a loader 
feature and cannot insert into the call chain when the ICD is linked directly. MoltenVK 
is discovered at runtime as the ICD via `VK_ICD_FILENAMES`. Both `libvulkan.dylib` and 
`libMoltenVK.dylib` are bundled into `built/lib` with `@loader_path`-relative install names.

### Building render pipeline's native C++ module against this build

- `interrogate` / `interrogate_module` are not installed to `built/bin/`. They
  build to `built/tmp/interrogate/...`; symlink them into `built/bin` so the module
  builder's `find_in_sdk("bin", "interrogate")` finds them. (A full
  `makepanda --installer` would place them properly.)
- No separate `libp3interrogatedb.dylib`. This build compiles the
  Python-wrapper runtime (`py_panda`/`Dtool*`) into each extension module, so the
  module builder must link with `INTERROGATE_LIB` empty, not `p3interrogatedb`.
- Match the Python ABI. CMake's legacy `find_package(PythonLibs)` may pick up a
  different Homebrew Python than the framework interpreter you run; a mismatched
  extension segfaults in `_PyObject_Malloc` on import. Link the same framework
  Python; verify with `otool -L <module>.so | grep -i python`.
- C++17 and qualified `std`. These headers need C++17 and no longer do
  `using namespace std;`, so dependent code must compile at C++17 and qualify
  `std::` names
