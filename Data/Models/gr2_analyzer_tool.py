#!/usr/bin/env python3
"""Deep analyzer for real Metin2 GR2 files - extracts structure and data."""

import os
import sys
import struct
import re
import json

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))


GR2_MAGIC_V1 = b"gr2\x00"
GR2_MAGIC_V2 = b")\xdel\xc0"  # Real Metin2 GR2 magic


class Metin2GR2RealAnalyzer:
    """Analyzes real Metin2 GR2 binary files with v2 magic."""

    def __init__(self, path: str):
        self.path = path
        self.data = b""
        self.file_size = 0
        self.strings = []
        self.sections = []
        self._load()

    def _load(self):
        if not os.path.exists(self.path):
            raise FileNotFoundError(f"File not found: {self.path}")
        with open(self.path, "rb") as f:
            self.data = f.read()
        self.file_size = len(self.data)

    def analyze(self) -> dict:
        info = {
            "path": self.path,
            "file_size": self.file_size,
            "magic": self.data[:4].hex(),
            "is_metin2_gr2": self.data[:4] == GR2_MAGIC_V2,
            "is_standard_gr2": self.data[:3] == b"gr2",
        }

        if self.data[:4] == GR2_MAGIC_V2:
            info["format"] = "Metin2 GR2 (Granny 2.x variant)"
            info["type"] = self._identify_type()
            info["strings"] = self._extract_strings()
            info["has_bip01"] = b"Bip01" in self.data
            info["bone_count_estimate"] = self._estimate_bones()
            info["sections"] = self._find_sections()
            info["has_weights"] = self._has_weights()
            info["has_animation"] = self._has_animation()
        elif self.data[:3] == b"gr2":
            info["format"] = "Standard GR2"
        else:
            info["format"] = "Unknown"

        return info

    def _identify_type(self) -> str:
        path_lower = self.path.lower()
        if "warrior" in path_lower:
            return "Character - Warrior"
        elif "sura" in path_lower:
            return "Character - Sura"
        elif "shaman" in path_lower:
            return "Character - Shaman"
        elif "assassin" in path_lower or "ninja" in path_lower:
            return "Character - Assassin"
        elif "weapon" in path_lower or any(c in path_lower for c in ["sword", "bow", "dagger", "fan", "bell"]):
            return "Weapon"
        return "Unknown"

    def _extract_strings(self) -> list:
        strings = []
        for m in re.finditer(rb"[\x20-\x7e]{4,}", self.data):
            try:
                s = m.group().decode("ascii")
                if any(kw in s.lower() for kw in ["bip", "bone", "mesh", "skin", "weight", "gr2",
                                                     "warrior", "sura", "shaman", "assassin"]):
                    strings.append({"offset": m.start(), "text": s})
            except:
                pass
        return strings[:30]  # limit

    def _estimate_bones(self) -> int:
        """Count Bip01 references as bone estimate."""
        bip_count = self.data.count(b"Bip01")
        return bip_count

    def _find_sections(self) -> list:
        """Look for section-like structures."""
        sections = []
        # Check for potential size/offset patterns
        for i in range(64, min(512, self.file_size - 8), 4):
            size = struct.unpack("<I", self.data[i:i+4])[0]
            offset = struct.unpack("<I", self.data[i+4:i+8])[0] if i+8 < self.file_size else 0
            if 0 < size < self.file_size and 0 < offset < self.file_size:
                if self.file_size - offset < size * 2:  # plausible section
                    sections.append({"offset": i, "size_at": size, "offset_to_data": offset})
        return sections[:10]

    def _has_weights(self) -> bool:
        weight_keywords = [b"weight", b"skin", b"bone_weight"]
        for kw in weight_keywords:
            if kw in self.data.lower():
                return True
        return False

    def _has_animation(self) -> bool:
        anim_keywords = [b"anim", b"frame", b"keyframe", b"play"]
        for kw in anim_keywords:
            if kw in self.data.lower():
                return True
        return False

    def extract_raw_header(self) -> dict:
        """Extract detailed header structure."""
        header = {}
        if self.file_size < 64:
            return {"error": "file too small"}
        d = self.data
        header["magic"] = d[:4].hex()
        header["field_04"] = struct.unpack("<I", d[4:8])[0] if len(d) > 8 else 0
        header["field_08"] = struct.unpack("<I", d[8:12])[0] if len(d) > 12 else 0
        header["field_12"] = struct.unpack("<I", d[12:16])[0] if len(d) > 16 else 0
        header["field_16"] = struct.unpack("<I", d[16:20])[0] if len(d) > 20 else 0
        header["field_20"] = struct.unpack("<I", d[20:24])[0] if len(d) > 24 else 0
        header["field_24"] = struct.unpack("<I", d[24:28])[0] if len(d) > 28 else 0
        header["field_28"] = struct.unpack("<I", d[28:32])[0] if len(d) > 32 else 0
        return header

    def try_decompress(self) -> bytes:
        """Try various decompression methods."""
        if self.data[:4] != GR2_MAGIC_V2:
            return b""
        import zlib
        # Try different offsets
        for offset in [4, 8, 16, 32]:
            try:
                result = zlib.decompress(self.data[offset:])
                print(f"  Zlib decompress at offset {offset}: {len(result)} bytes -> {len(result)} bytes")
                return result
            except:
                pass
        return b""

    def extract_mesh_data(self) -> dict:
        """Attempt to extract mesh data from the binary."""
        result = {"vertices": 0, "faces": 0, "normals": 0, "uvs": 0}
        d = self.data
        # Look for patterns that suggest vertex data (three consecutive floats)
        float_patterns = 0
        for i in range(64, min(len(d) - 12, 10000), 4):
            try:
                f0 = struct.unpack("<f", d[i:i+4])[0]
                f1 = struct.unpack("<f", d[i+4:i+8])[0]
                f2 = struct.unpack("<f", d[i+8:i+12])[0]
                # Check if these look like 3D coordinates
                if -100 < f0 < 100 and -100 < f1 < 100 and -100 < f2 < 100:
                    float_patterns += 1
            except:
                pass
        result["float_triplets_64_10000"] = float_patterns
        return result

    def extract_skeleton_info(self) -> dict:
        """Extract skeleton information from Bip01 references."""
        info = {"bone_count": 0, "bones": []}
        d = self.data
        bip_pattern = re.compile(rb'Bip01[\x20-\x7e]{0,40}[\x00]')
        matches = bip_pattern.findall(d)
        bones_found = set()
        for m in matches:
            try:
                name = m.split(b"\x00")[0].decode("ascii", errors="replace").strip()
                if name:
                    bones_found.add(name)
            except:
                pass
        info["bones"] = sorted(bones_found)
        info["bone_count"] = len(bones_found)
        return info


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Metin2 GR2 Analyzer Tool")
    parser.add_argument("path", type=str, help="Path to .gr2 file or directory")
    parser.add_argument("--deep", action="store_true", help="Deep analysis")
    parser.add_argument("--json", action="store_true", help="JSON output")
    parser.add_argument("--decompress", action="store_true", help="Try decompress")
    args = parser.parse_args()

    path = args.path
    if os.path.isdir(path):
        files = [os.path.join(path, f) for f in os.listdir(path) if f.endswith(".gr2")]
    else:
        files = [path]

    results = []
    for fpath in files:
        print(f"\n{'='*60}")
        print(f"  Analyzing: {os.path.basename(fpath)}")
        print(f"{'='*60}")
        analyzer = Metin2GR2RealAnalyzer(fpath)
        info = analyzer.analyze()
        print(f"  Format: {info['format']}")
        print(f"  Size: {info['file_size']} bytes")
        print(f"  Type: {info.get('type', 'Unknown')}")
        print(f"  Bones detected: {info.get('bone_count_estimate', 0)}")
        print(f"  Has Bip01: {info.get('has_bip01', False)}")

        if info.get("strings"):
            print(f"\n  Key strings found:")
            for s in info["strings"][:15]:
                print(f"    [{s['offset']:6d}] {s['text']}")

        if args.deep:
            header = analyzer.extract_raw_header()
            print(f"\n  Raw Header:")
            for k, v in header.items():
                print(f"    {k}: {v}")

            skel = analyzer.extract_skeleton_info()
            print(f"\n  Skeleton: {skel['bone_count']} bones")
            for b in skel["bones"][:25]:
                print(f"    - {b}")

            mesh = analyzer.extract_mesh_data()
            print(f"\n  Mesh data hints:")
            for k, v in mesh.items():
                print(f"    {k}: {v}")

        if args.decompress:
            print(f"\n  Decompression:")
            decompressed = analyzer.try_decompress()

        results.append(info)

    if args.json:
        print(json.dumps(results, indent=2, default=str))


if __name__ == "__main__":
    main()
