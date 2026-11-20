# T12. Dedicated color grading for the portal image

Branch `exp/T12-portal-color`. Code: portal rendering. A game run is required. Screenshot analysis (item 1) can be done before T02.

## What is known

- **Run 0.8.24.** Through the portal, the interior is brighter and grayer than from inside, and the flame is dim.
- **Run 0.8.25.**
  - F2 replaces `ImageSpaceManager` data with data captured inside, for the entire frame in which the portal pass runs (`Portal::ToggleInteriorImageSpace`, `g_interiorLook` in `src/PortalImage.cpp`).
  - With F2, the portal image matched the interior, but all of Whiterun changed color. `imageSpace.swaps` = `held`.
  - Conclusion: the exterior tone mapping colors the portal image.
- **Render order.**
  1. The portal pass renders the interior into its own `R16G16B16A16_FLOAT` texture.
  2. A quad with this texture is drawn into the exterior HDR frame `kMAIN`.
  3. The entire frame passes through the exterior ImageSpace: eye adaptation, Reinhard with white point, bloom, saturation, tint, contrast.

  Formulas — `ISHDR.hlsl` in `E:\Games\Skyrim\ModCreation\references\github\community-shaders\package\Shaders\`. GPL-3 license: do not copy code; use the formulas only as factual information.
- **Captures** in `logs/run-0.8.25/`, format 10 (`R16G16B16A16_FLOAT`), 2560×1600:
  - `main-1.dds` — HDR frame from inside near the hearth;
  - `portal-1.dds` and `main-2.dds` — outside;
  - `portal-2.dds` and `main-3.dds` — outside with F2.

  `tools/portal_dump_to_png.py` converts them to PNG. User screenshots: `E:\Games\Skyrim\The Elder Scrolls V - Skyrim\ScreenShot4.png` (inside), `ScreenShot5.png` (outside, 0.8.24). numpy 2.1 is installed.
- **ImageSpace data:** `RE::ImageSpaceManager::GetSingleton()->GetImageSpaceData()`, `ImageSpaceData` structure (0xA0): `baseData` (hdr, cinematic, tint, dof), `modData`, `baseAmount`, `modAmount`. Eye adaptation is not stored in this data and still needs to be located.

## Tasks

1. **Offline analysis of captures.** Do the portal HDR values match the interior values (wall, flame)? In other words, does the rendering itself differ, or only the tone mapping?
2. **Choose a method** so that the portal looks like the interior and the exterior still looks like the exterior:
   - **a) precompensation.** After the pass, a custom pixel shader transforms the portal texture so that exterior tone mapping produces the same result as interior tone mapping: `x' = T_exterior⁻¹(T_interior(x))`. Parameters for both tone mappings and adaptations are obtained every frame. Bloom remains the exterior bloom;
   - **b) quad after exterior ImageSpace.** The quad is drawn in LDR with frame depth, while the portal image undergoes its own tone mapping in its own texture.

   Compare the options by quality, TAA, complexity, compatibility with ENB and Community Shaders, and the “any camera” requirement (`CLAUDE.md`, 5.14, item 4). The shader can be compiled with `D3DCompile` (`d3dcompiler_47.dll` is available in Windows) or in advance.
3. **Implement the selected option.** F2 becomes a “dedicated portal color grading on/off” toggle, and the full-screen substitution is removed.

## Conflicts

Portal rendering — with T13. After T02 these are separate files.
