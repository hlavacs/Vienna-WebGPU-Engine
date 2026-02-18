# Tutorial 03: Shadow Mapping

> **💡 Tip:** It's recommended using the [03_shadow_mapping.html](03_shadow_mapping.html) version of this tutorial as copying code works best there regarding padding and formatting.

> **Status:** This tutorial is currently under development.

> **⚠️ Build issues?** See [Troubleshooting](#troubleshooting) at the end of this tutorial for help reading build errors from the terminal.

This tutorial will cover implementing shadow mapping to add realistic shadows to your 3D scenes.

**Topics to be covered:**
- Shadow map rendering from light perspective
- Depth texture creation and sampling
- Shadow bias and peter-panning artifacts
- PCF (Percentage Closer Filtering) for soft shadows
- Integrating shadows into the lighting pipeline
- 
---

## What's Next?

While this tutorial is being developed, continue to **Tutorial 04** to learn about post-processing:

**Next Tutorial:** [04_postprocessing.md](04_postprocessing.md) / [04_postprocessing.pdf](04_postprocessing.pdf) / [04_postprocessing.html](04_postprocessing.html)

In Tutorial 04, you'll learn how to write a custom render pass by implementing post-processing effects like vignette, tone mapping, and color grading.

---

## Troubleshooting

### Build Failures - Reading Terminal Output

**⚠️ Important:** When using `scripts/build.bat`, the task system may report success even if the build actually failed. You **MUST check the terminal output** to see the real result.

**What to look for in terminal:**
1. Scroll to the **very end** of the terminal output
2. Look for `[SUCCESS] Build completed successfully!` - if this appears, build succeeded
3. If you see `[ERROR] Build failed.` - the build failed regardless of task status

**Common build issues:**
- **Shader errors** - Check `.wgsl` files for missing semicolons and type mismatches
- **CMake cache issues** - Delete `build/` folder and rebuild clean
- **Include paths** - Verify header includes are correct and files exist

### Debug Strategy

**If errors are unclear:**
1. Open `MeshPass.cpp` in your editor
2. Add a breakpoint in the `render()` method
3. Press `F5` to start debugging with VS Code
4. Check the **Terminal Output** panel - errors will be printed there
