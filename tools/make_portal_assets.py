"""Writes the portal quad's assets for the Vortex archive (standard library only):

  res/data/Meshes/CellLoadResearch/portalquad.nif  - one unit quad, Skyrim SE NIF (20.2.0.7, BS 100)
  res/data/Textures/CellLoadResearch/portal.dds    - 4x4 magenta placeholder

The quad lies in its local XZ plane (x, z in -0.5..0.5, normal +Y) with UV (0,0) at the top left
(-x, +z). It has a BSEffectShaderProperty (unlit) whose source texture is portal.dds; at run time
the plugin points that texture's shader resource view at the portal camera's render target, so
the quad shows the interior seen through the door. A magenta quad means the swap did not happen.

Usage: python tools/make_portal_assets.py
"""
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NIF_PATH = os.path.join(ROOT, "res", "data", "Meshes", "CellLoadResearch", "portalquad.nif")
DDS_PATH = os.path.join(ROOT, "res", "data", "Textures", "CellLoadResearch", "portal.dds")
TEXTURE = "textures\\CellLoadResearch\\portal.dds"

# Bethesda vertex attributes (BSVertexDesc bits 44+)
VF_VERTEX, VF_UV, VF_NORMAL, VF_COLORS = 0x1, 0x2, 0x8, 0x20

# SkyrimShaderPropertyFlags
SLSF1_ZBUFFER_TEST = 1 << 31
SLSF2_ZBUFFER_WRITE = 1 << 0
SLSF2_NO_FADE = 1 << 3
SLSF2_DOUBLE_SIDED = 1 << 4


def u8(v): return struct.pack("<B", v)
def u16(v): return struct.pack("<H", v)
def u32(v): return struct.pack("<I", v)
def i32(v): return struct.pack("<i", v)
def u64(v): return struct.pack("<Q", v)
def f32(*vs): return b"".join(struct.pack("<f", v) for v in vs)
def sized(s): return u32(len(s)) + s.encode("ascii")
def export(s): return u8(len(s) + 1) + s.encode("ascii") + b"\0"


def half(v):
    return struct.pack("<e", v)


def norm_byte(v):
    """-1..1 to the 0..255 byte Bethesda uses for normals."""
    return max(0, min(255, round((v + 1.0) * 127.5)))


def ni_object_net(name_index):
    return i32(name_index) + u32(0) + i32(-1)  # name, no extra data, no controller


def ni_av_object(name_index, flags=14):
    identity = f32(1, 0, 0, 0, 1, 0, 0, 0, 1)
    return ni_object_net(name_index) + u32(flags) + f32(0, 0, 0) + identity + f32(1.0) + i32(-1)


def ni_node(name_index, children):
    return ni_av_object(name_index) + u32(len(children)) + b"".join(i32(c) for c in children) + u32(0)


def bs_tri_shape(name_index, shader_ref):
    # position (12) + unused W (4) | UV half2 (4) | normal + bitangent Y bytes (4) | color (4) = 28
    stride, uv_offset, normal_offset, color_offset = 28, 16, 20, 24
    attributes = VF_VERTEX | VF_UV | VF_NORMAL | VF_COLORS
    desc = (stride // 4) | ((uv_offset // 4) << 8) | ((normal_offset // 4) << 16) | ((color_offset // 4) << 24) | (attributes << 44)
    corners = [  # x, z, u, v
        (-0.5, 0.5, 0.0, 0.0),
        (0.5, 0.5, 1.0, 0.0),
        (-0.5, -0.5, 0.0, 1.0),
        (0.5, -0.5, 1.0, 1.0),
    ]
    vertices = b""
    for x, z, u, v in corners:
        vertices += f32(x, 0.0, z) + u32(0) + half(u) + half(v)
        vertices += bytes([norm_byte(0.0), norm_byte(1.0), norm_byte(0.0), norm_byte(0.0)])
        vertices += bytes([255, 255, 255, 255])
    triangles = u16(0) + u16(2) + u16(1) + u16(1) + u16(2) + u16(3)
    assert len(vertices) == 4 * stride
    return (ni_av_object(name_index)
            + f32(0.0, 0.0, 0.0, 0.7072)  # bounding sphere
            + i32(-1) + i32(shader_ref) + i32(-1)  # skin, shader property, alpha property
            + u64(desc)
            + u16(2) + u16(4)  # triangles, vertices
            + u32(len(vertices) + len(triangles))
            + vertices + triangles
            + u32(0))  # particle data size


def bs_effect_shader_property(name_index):
    return (ni_object_net(name_index)
            + u32(SLSF1_ZBUFFER_TEST)
            + u32(SLSF2_ZBUFFER_WRITE | SLSF2_NO_FADE | SLSF2_DOUBLE_SIDED)
            + f32(0.0, 0.0) + f32(1.0, 1.0)  # UV offset, UV scale
            + sized(TEXTURE)
            + u8(0)  # texture clamp mode: clamp S, clamp T
            + u8(0) + u8(0) + u8(0)  # lighting influence, env map min LOD, unknown
            + f32(1.0, 1.0, 1.0, 0.0)  # falloff start/stop angle, start/stop opacity
            + f32(1.0, 1.0, 1.0, 1.0)  # base color
            + f32(1.0)  # base color scale
            + f32(100.0)  # soft falloff depth
            + sized(""))  # greyscale texture


def write_nif(path):
    strings = ["PortalQuad", "PortalQuad:0", "PortalQuadShader"]
    block_types = ["NiNode", "BSTriShape", "BSEffectShaderProperty"]
    blocks = [ni_node(0, [1]), bs_tri_shape(1, 2), bs_effect_shader_property(2)]
    out = b"Gamebryo File Format, Version 20.2.0.7\n"
    out += u32(0x14020007) + u8(1) + u32(12) + u32(len(blocks))
    out += u32(100) + export("CellLoadResearch") + export("") + export("")
    out += u16(len(block_types)) + b"".join(sized(t) for t in block_types)
    out += b"".join(u16(i) for i in range(len(blocks)))
    out += b"".join(u32(len(b)) for b in blocks)
    out += u32(len(strings)) + u32(max(len(s) for s in strings)) + b"".join(sized(s) for s in strings)
    out += u32(0)  # groups
    out += b"".join(blocks)
    out += u32(1) + i32(0)  # roots
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


def write_dds(path, size=4):
    header = struct.pack("<4s7I44x", b"DDS ", 124, 0x100F, size, size, size * 4, 0, 0)
    pixel_format = struct.pack("<8I", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    caps = struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    pixels = bytes([255, 0, 255, 255]) * (size * size)  # B, G, R, A: magenta
    data = header + pixel_format + caps + pixels
    assert len(header) + len(pixel_format) + len(caps) == 128
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    return len(data)


if __name__ == "__main__":
    print(f"{NIF_PATH}: {write_nif(NIF_PATH)} bytes")
    print(f"{DDS_PATH}: {write_dds(DDS_PATH)} bytes")
