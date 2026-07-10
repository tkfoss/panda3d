# EXPERIMENTAL.  Wires the Vulkan backend into the CMake build (previously it
# could only be built via makepanda).  With -DHAVE_VULKAN=ON this configures,
# compiles, and links p3vulkandisplay as a loadable display module (selectable
# at runtime with `load-display p3vulkandisplay`).  Vulkan defaults OFF, so this
# does not affect normal CMake builds.
#
# Not yet ported from makepanda: the macOS runtime bundling of libMoltenVK /
# the Vulkan loader and the ICD manifest into the output tree.  On macOS the
# loader is found via the linked -lvulkan and MoltenVK via VK_ICD_FILENAMES at
# runtime, as with any external Vulkan install.
if(NOT HAVE_VULKAN)
  return()
endif()

set(P3VULKANDISPLAY_HEADERS
  config_vulkandisplay.h
  vulkanBufferContext.h vulkanBufferContext.I
  vulkanCommandBuffer.h vulkanCommandBuffer.I
  vulkanFramebuffer.h vulkanFramebuffer.I
  vulkanFrameData.h
  vulkanGeomMunger.h
  vulkanGraphicsBuffer.h vulkanGraphicsBuffer.I
  vulkanGraphicsPipe.h vulkanGraphicsPipe.I
  vulkanGraphicsStateGuardian.h vulkanGraphicsStateGuardian.I
  vulkanGraphicsWindow.h vulkanGraphicsWindow.I
  vulkanIndexBufferContext.h vulkanIndexBufferContext.I
  vulkanMemoryPage.h vulkanMemoryPage.I
  vulkanSamplerContext.h vulkanSamplerContext.I
  vulkanShaderContext.h vulkanShaderContext.I
  vulkanTextureContext.h vulkanTextureContext.I
  vulkanVertexBufferContext.h vulkanVertexBufferContext.I
)

set(P3VULKANDISPLAY_SOURCES
  config_vulkandisplay.cxx
  vulkanBufferContext.cxx
  vulkanCommandBuffer.cxx
  vulkanFramebuffer.cxx
  vulkanFrameData.cxx
  vulkanGeomMunger.cxx
  vulkanGraphicsBuffer.cxx
  vulkanGraphicsPipe.cxx
  vulkanGraphicsStateGuardian.cxx
  vulkanGraphicsWindow.cxx
  vulkanIndexBufferContext.cxx
  vulkanSamplerContext.cxx
  vulkanShaderContext.cxx
  vulkanTextureContext.cxx
  vulkanVertexBufferContext.cxx
)

composite_sources(p3vulkandisplay P3VULKANDISPLAY_SOURCES)

# The Vulkan window inherits from the platform's native window, exactly as the
# GL backend does; link the matching platform display component into the module.
set(VULKANDISPLAY_LINK_TARGETS)
if(WIN32)
  list(APPEND VULKANDISPLAY_LINK_TARGETS p3windisplay)
elseif(HAVE_COCOA)
  list(APPEND VULKANDISPLAY_LINK_TARGETS p3cocoadisplay)
elseif(HAVE_X11)
  list(APPEND VULKANDISPLAY_LINK_TARGETS p3x11display)
endif()

set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "Vulkan")
add_metalib(p3vulkandisplay ${MODULE_TYPE}
  ${P3VULKANDISPLAY_HEADERS} ${P3VULKANDISPLAY_SOURCES}
  COMPONENTS ${VULKANDISPLAY_LINK_TARGETS})
unset(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME)

set_target_properties(p3vulkandisplay PROPERTIES DEFINE_SYMBOL BUILDING_VULKANDISPLAY)
# The shaderpipeline (glslang -> SPIR-V + transform passes) is a component of the
# "panda" metalib, so linking panda already supplies it; link only the Vulkan
# loader here (Vulkan::Vulkan; MoltenVK-backed on macOS).
target_link_libraries(p3vulkandisplay panda PKG::VULKAN)

install(TARGETS p3vulkandisplay
  EXPORT Vulkan COMPONENT Vulkan
  DESTINATION ${MODULE_DESTINATION}
  ARCHIVE COMPONENT VulkanDevel)

export_targets(Vulkan NAMESPACE "Panda3D::Vulkan::" COMPONENT VulkanDevel)
