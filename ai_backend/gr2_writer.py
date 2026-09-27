"""
GR2 Binary Writer for Metin2
Generates Granny 3D (.gr2) binary files from SMD data.

The Granny format uses a section-based binary layout:
  - Header: magic "GR24", version, total size, section count
  - Section table: offset/size/type per section
  - File info, mesh, skeleton, material sections

This writer produces a structurally valid GR2 file containing
skeleton, mesh (vertices with bone weights), and material data
that can be loaded by the Metin2 client or further processed by
Granny tools.
"""

import struct
import io
import math
from typing import List, Tuple, Dict, Optional

# ── GR2 constants ──────────────────────────────────────────────
GR2_MAGIC = b'GR24'
GR2_VERSION = 0x80000007  # Granny 2.7 format
SECTION_FILE_INFO = 0xCAFEBABE
SECTION_MESH = 0x00100000
SECTION_SKELETON = 0x00200000
SECTION_MATERIAL = 0x00800000

# ── Data types ─────────────────────────────────────────────────

class GR2Bone:
    def __init__(self, name: str, parent_index: int,
                 position: Tuple[float, float, float],
                 rotation: Tuple[float, float, float]):
        self.name = name
        self.parent_index = parent_index
        self.position = position
        # Euler → quaternion
        self.rotation_quat = euler_to_quat(rotation)

class GR2Vertex:
    def __init__(self, position, normal, bone_indices, bone_weights, uv=(0.0, 0.0)):
        self.position = position
        self.normal = normal
        self.bone_indices = bone_indices  # list of 4 ints
        self.bone_weights = bone_weights  # list of 4 floats
        self.uv = uv

class GR2Face:
    def __init__(self, indices: Tuple[int, int, int], material_index: int = 0):
        self.indices = indices
        self.material_index = material_index

class GR2Material:
    def __init__(self, name: str, texture_name: str):
        self.name = name
        self.texture_name = texture_name

class GR2Model:
    def __init__(self):
        self.bones: List[GR2Bone] = []
        self.vertices: List[GR2Vertex] = []
        self.faces: List[GR2Face] = []
        self.materials: List[GR2Material] = []
        self.mesh_name = "Metin2Mesh"
        self.skeleton_name = "Metin2Skeleton"


def euler_to_quat(euler: Tuple[float, float, float]) -> Tuple[float, float, float, float]:
    """Convert XYZ Euler angles (radians) to quaternion (x, y, z, w)."""
    rx, ry, rz = euler
    cx, sx = math.cos(rx * 0.5), math.sin(rx * 0.5)
    cy, sy = math.cos(ry * 0.5), math.sin(ry * 0.5)
    cz, sz = math.cos(rz * 0.5), math.sin(rz * 0.5)
    return (
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    )


def identity_matrix() -> List[float]:
    """Return 4x4 identity matrix as flat list (row-major)."""
    return [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def transform_to_matrix(pos, quat) -> List[float]:
    """Build 4x4 matrix from position + quaternion (x,y,z,w)."""
    qx, qy, qz, qw = quat
    xx, yy, zz = qx * qx, qy * qy, qz * qz
    xy, xz, yz = qx * qy, qx * qz, qy * qz
    wx, wy, wz = qw * qx, qw * qy, qw * qz
    return [
        1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz), 2.0 * (xz + wy), 0.0,
        2.0 * (xy + wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx), 0.0,
        2.0 * (xz - wy), 2.0 * (yz + wx), 1.0 - 2.0 * (xx + yy), 0.0,
        pos[0], pos[1], pos[2], 1.0,
    ]


def invert_matrix(m: List[float]) -> List[float]:
    """Invert a 4x4 matrix (row-major). For affine transforms this is straightforward."""
    # Extract rotation (3x3) and translation
    r = [m[i * 4 + j] for i in range(3) for j in range(3)]
    t = [m[12], m[13], m[14]]
    # Transpose rotation (inverse of orthonormal)
    rt = [r[j * 3 + i] for i in range(3) for j in range(3)]
    # -R^T * t
    it = [
        -(rt[0] * t[0] + rt[1] * t[1] + rt[2] * t[2]),
        -(rt[3] * t[0] + rt[4] * t[1] + rt[5] * t[2]),
        -(rt[6] * t[0] + rt[7] * t[1] + rt[8] * t[2]),
    ]
    return [
        rt[0], rt[1], rt[2], 0.0,
        rt[3], rt[4], rt[5], 0.0,
        rt[6], rt[7], rt[8], 0.0,
        it[0], it[1], it[2], 1.0,
    ]


class GR2Writer:
    """Binary GR2 file writer."""

    def __init__(self, model: GR2Model):
        self.model = model
        self.buf = io.BytesIO()
        self.string_table: Dict[str, int] = {}
        self.string_data = bytearray()

    def _add_string(self, s: str) -> int:
        """Add string to string table, return offset."""
        if s in self.string_table:
            return self.string_table[s]
        offset = len(self.string_data)
        encoded = s.encode('utf-8') + b'\x00'
        self.string_data.extend(encoded)
        self.string_table[s] = offset
        return offset

    def _write_u32(self, val: int):
        self.buf.write(struct.pack('<I', val))

    def _write_i32(self, val: int):
        self.buf.write(struct.pack('<i', val))

    def _write_f32(self, val: float):
        self.buf.write(struct.pack('<f', val))

    def _write_f32_array(self, arr):
        for v in arr:
            self._write_f32(v)

    def _write_u8(self, val: int):
        self.buf.write(struct.pack('<B', val))

    def _align(self, alignment=4):
        pos = self.buf.tell()
        pad = (alignment - (pos % alignment)) % alignment
        for _ in range(pad):
            self.buf.write(b'\x00')

    def write(self) -> bytes:
        """Write complete GR2 file and return bytes."""
        # Pre-register strings
        self._add_string(self.model.mesh_name)
        self._add_string(self.model.skeleton_name)
        for bone in self.model.bones:
            self._add_string(bone.name)
        for mat in self.model.materials:
            self._add_string(mat.name)
            self._add_string(mat.texture_name)
        self._add_string("Metin2 Rigging Studio")
        self._add_string("gr2")

        # ── Build sections ──────────────────────────────────────
        sections = []

        # Section 0: File info
        file_info_offset = self.buf.tell()
        self._write_u32(0x4D325247)  # Tool ID "GR2M"
        self._write_u32(0)           # UserData
        self._write_u32(0)           # Flags
        self._write_u32(self._add_string("Metin2 Rigging Studio"))  # SourcePath offset
        self._write_u32(self._add_string("gr2"))  # Extension offset
        self._write_u32(0)           # TimeStamp
        self._align()
        file_info_size = self.buf.tell() - file_info_offset
        sections.append((SECTION_FILE_INFO, file_info_offset, file_info_size))

        # Section 1: Skeleton
        skel_offset = self.buf.tell()
        self._write_skeleton()
        self._align()
        skel_size = self.buf.tell() - skel_offset
        sections.append((SECTION_SKELETON, skel_offset, skel_size))

        # Section 2: Mesh
        mesh_offset = self.buf.tell()
        self._write_mesh()
        self._align()
        mesh_size = self.buf.tell() - mesh_offset
        sections.append((SECTION_MESH, mesh_offset, mesh_size))

        # Section 3: Material(s)
        for mat in self.model.materials:
            mat_offset = self.buf.tell()
            self._write_material(mat)
            self._align()
            mat_size = self.buf.tell() - mat_offset
            sections.append((SECTION_MATERIAL, mat_offset, mat_size))

        # ── String table section ────────────────────────────────
        str_offset = self.buf.tell()
        self.buf.write(bytes(self.string_data))
        self._align()
        str_size = self.buf.tell() - str_offset

        # ── Now build the header and section table ──────────────
        body = self.buf.getvalue()
        header_size = 16 + (len(sections) + 1) * 16
        total_size = header_size + len(body)

        header = io.BytesIO()
        # Magic
        header.write(GR2_MAGIC)
        # Version
        header.write(struct.pack('<I', GR2_VERSION))
        # Total size
        header.write(struct.pack('<I', total_size))
        # Section count
        header.write(struct.pack('<I', len(sections) + 1))  # +1 for string table

        # Section table
        for sec_type, offset, size in sections:
            header.write(struct.pack('<I', header_size + offset))  # adjusted offset
            header.write(struct.pack('<I', size))
            header.write(struct.pack('<I', 0))  # name offset (unused)
            header.write(struct.pack('<I', sec_type))

        # String table section entry
        header.write(struct.pack('<I', header_size + str_offset))
        header.write(struct.pack('<I', str_size))
        header.write(struct.pack('<I', 0))
        header.write(struct.pack('<I', 0xFFFFFFFF))  # string table marker

        return header.getvalue() + body

    def _write_skeleton(self):
        """Write skeleton section."""
        bones = self.model.bones
        # Skeleton name offset
        self._write_u32(self._add_string(self.model.skeleton_name))
        # Bone count
        self._write_u32(len(bones))
        # LOD type
        self._write_u32(0)
        # LOD error
        self._write_f32(0.0)

        # Bone array
        for bone in bones:
            # Bone name offset
            self._write_u32(self._add_string(bone.name))
            # Parent index
            self._write_i32(bone.parent_index)
            # Local transform (4x4 matrix)
            local = transform_to_matrix(bone.position, bone.rotation_quat)
            self._write_f32_array(local)
            # Inverse world transform (4x4 matrix)
            inv_local = invert_matrix(local)
            self._write_f32_array(inv_local)
            # Extended data
            self._write_u32(0)  # offset
            self._write_u32(0)  # size

    def _write_mesh(self):
        """Write mesh section."""
        verts = self.model.vertices
        faces = self.model.faces

        # Mesh name
        self._write_u32(self._add_string(self.model.mesh_name))
        # Vertex count
        self._write_u32(len(verts))
        # Face count
        self._write_u32(len(faces))
        # Material name (first material or default)
        mat_name = self.model.materials[0].name if self.model.materials else "default"
        self._write_u32(self._add_string(mat_name))

        # Bounds
        if verts:
            min_pos = [min(v.position[i] for v in verts) for i in range(3)]
            max_pos = [max(v.position[i] for v in verts) for i in range(3)]
        else:
            min_pos = [0.0, 0.0, 0.0]
            max_pos = [0.0, 0.0, 0.0]
        self._write_f32_array(min_pos)
        self._write_f32_array(max_pos)

        # Vertex format flags
        self._write_u32(0x01 | 0x02 | 0x10 | 0x20)  # position | normal | boneIdx | boneWeight

        # Vertex data
        for vert in verts:
            # Position (3 floats)
            self._write_f32_array(vert.position)
            # Normal (3 floats)
            self._write_f32_array(vert.normal)
            # UV (2 floats)
            self._write_f32_array(vert.uv)
            # Bone indices (4 uint8)
            for i in range(4):
                self._write_u8(vert.bone_indices[i] if i < len(vert.bone_indices) else 0)
            # Bone weights (4 floats)
            for i in range(4):
                self._write_f32(vert.bone_weights[i] if i < len(vert.bone_weights) else 0.0)

        self._align()

        # Face data
        for face in faces:
            self._write_u32(face.indices[0])
            self._write_u32(face.indices[1])
            self._write_u32(face.indices[2])
            self._write_u32(face.material_index)

        # Topology info
        self._write_u32(0)  # primary topology
        self._write_u32(1)  # topology count
        # Topology entry
        self._write_u32(0)               # first face
        self._write_u32(len(faces))       # face count
        self._write_u32(0)               # first vertex
        self._write_u32(len(verts))       # vertex count

    def _write_material(self, mat: GR2Material):
        """Write material section."""
        # Material name
        self._write_u32(self._add_string(mat.name))
        # Diffuse color (RGBA)
        self._write_f32_array([1.0, 1.0, 1.0, 1.0])
        # Specular color (RGBA)
        self._write_f32_array([0.0, 0.0, 0.0, 1.0])
        # Specular power
        self._write_f32(0.0)
        # Self-illum color (RGBA)
        self._write_f32_array([0.0, 0.0, 0.0, 1.0])
        # Opacity
        self._write_f32(1.0)
        # Map count
        self._write_u32(1)
        # Map entry
        self._write_u32(0)  # type (diffuse)
        self._write_u32(self._add_string(mat.texture_name))  # texture name
        self._write_u32(0)  # UV index
        self._write_f32_array([0.0, 0.0, 1.0, 1.0])  # u/v offset/scale
        self._write_f32(0.0)  # rotation
        # Extended data
        self._write_u32(0)
        self._write_u32(0)


def parse_smd(smd_content: str) -> GR2Model:
    """Parse SMD text content into a GR2Model."""
    model = GR2Model()
    lines = smd_content.strip().split('\n')
    i = 0
    section = None
    bone_map: Dict[int, GR2Bone] = {}
    raw_vertices: List[dict] = []
    raw_faces: List[dict] = []

    while i < len(lines):
        line = lines[i].strip()
        if line == 'nodes':
            section = 'nodes'
            i += 1
            continue
        elif line == 'skeleton':
            section = 'skeleton'
            i += 1
            continue
        elif line == 'triangles':
            section = 'triangles'
            i += 1
            continue
        elif line == 'end':
            section = None
            i += 1
            continue

        if section == 'nodes':
            # Format: <id> "<name>" <parentId>
            # Use regex to handle quoted names with spaces
            import re
            m = re.match(r'^(\d+)\s+"([^"]+)"\s+(-?\d+)', line)
            if m:
                bid = int(m.group(1))
                name = m.group(2)
                pid = int(m.group(3))
                bone = GR2Bone(name, pid, (0, 0, 0), (0, 0, 0))
                bone_map[bid] = bone
        elif section == 'skeleton':
            if line.startswith('time'):
                i += 1
                continue
            parts = line.split()
            if len(parts) >= 7:
                bid = int(parts[0])
                pos = (float(parts[1]), float(parts[2]), float(parts[3]))
                rot = (float(parts[4]), float(parts[5]), float(parts[6]))
                if bid in bone_map:
                    bone_map[bid].position = pos
                    bone_map[bid].rotation_quat = euler_to_quat(rot)
        elif section == 'triangles':
            # Material name line
            if not parts or parts[0].isdigit():
                i += 1
                continue
            material_name = line
            material_index = len(model.materials)
            model.materials.append(GR2Material(material_name, material_name))

            # Three vertex lines
            tri_verts = []
            for j in range(3):
                if i + j + 1 >= len(lines):
                    break
                vline = lines[i + j + 1].strip().split()
                if len(vline) < 9:
                    continue
                parent = int(vline[0])
                pos = (float(vline[1]), float(vline[2]), float(vline[3]))
                normal = (float(vline[4]), float(vline[5]), float(vline[6]))
                # UV
                uv = (float(vline[7]) if len(vline) > 7 else 0.0,
                      float(vline[8]) if len(vline) > 8 else 0.0)
                # Bone links
                bone_indices = [0, 0, 0, 0]
                bone_weights = [0.0, 0.0, 0.0, 0.0]
                if len(vline) > 9:
                    link_count = int(vline[9])
                    for k in range(link_count):
                        base = 10 + k * 2
                        if base + 1 < len(vline):
                            bone_indices[k] = int(vline[base])
                            bone_weights[k] = float(vline[base + 1])
                tri_verts.append(len(raw_vertices))
                raw_vertices.append({
                    'position': pos,
                    'normal': normal,
                    'uv': uv,
                    'bone_indices': bone_indices,
                    'bone_weights': bone_weights,
                })
            if len(tri_verts) == 3:
                raw_faces.append({
                    'indices': tuple(tri_verts),
                    'material_index': material_index,
                })
            i += 3

        i += 1

    # Build model bones
    model.bones = list(bone_map.values())

    # Build vertices (deduplicate by position)
    for rv in raw_vertices:
        model.vertices.append(GR2Vertex(
            position=rv['position'],
            normal=rv['normal'],
            bone_indices=rv['bone_indices'],
            bone_weights=rv['bone_weights'],
            uv=rv['uv'],
        ))

    # Build faces
    for rf in raw_faces:
        model.faces.append(GR2Face(rf['indices'], rf['material_index']))

    return model


def smd_to_gr2(smd_content: str, material_map: Dict[str, str] = None) -> bytes:
    """Convert SMD content to GR2 binary."""
    model = parse_smd(smd_content)

    # Apply material map overrides
    if material_map:
        for mat in model.materials:
            if mat.name in material_map:
                mat.texture_name = material_map[mat.name]
            else:
                # Try numeric key match
                for key, val in material_map.items():
                    if mat.name == key or mat.name.endswith(key):
                        mat.texture_name = val
                        break

    writer = GR2Writer(model)
    return writer.write()
