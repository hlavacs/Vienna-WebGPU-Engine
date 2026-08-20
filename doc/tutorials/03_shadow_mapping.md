# Tutorial 03 Has Moved

The planned shadow-mapping tutorial was replaced by **Tutorial 03: A More Complex Shader - Glass with Transparency and Shadows**, which covers shadow *receiving* (light-space projection, bias, PCF via the engine's shared `calculate_shadow()`) as part of a complete glass shader, together with transparency and the forward render path.

**Go to the new tutorial:** [03_glass_shader.md](03_glass_shader.md)

This file is kept only so old links don't break. For the shadow *producing* side (how the engine renders shadow maps), see `src/engine/rendering/ShadowPass.cpp` and the shadow library at `resources/shaders/lib/shadow.wgsl`.
