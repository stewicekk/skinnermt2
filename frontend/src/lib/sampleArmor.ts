import type { Bone, BoneWeight, DisplayTriangle, WeightVertex } from "@/stores/useRiggingStore";

export interface SampleArmor {
  meshId: string;
  vertices: WeightVertex[];
  triangles: DisplayTriangle[];
  bones: string[];
  hierarchy: Bone[];
}

interface BoneAnchor {
  bone: string;
  position: [number, number, number];
}

const ANCHORS: BoneAnchor[] = [
  { bone: "Bip01 Pelvis", position: [0, 0.05, 0] },
  { bone: "Bip01 Spine", position: [0, 0.75, 0] },
  { bone: "Bip01 Spine1", position: [0, 1.35, 0] },
  { bone: "Bip01 Neck", position: [0, 1.95, 0] },
  { bone: "Bip01 Head", position: [0, 2.35, 0] },
  { bone: "Bip01 L Clavicle", position: [-0.34, 1.62, 0] },
  { bone: "Bip01 R Clavicle", position: [0.34, 1.62, 0] },
  { bone: "Bip01 L UpperArm", position: [-0.72, 1.58, 0] },
  { bone: "Bip01 R UpperArm", position: [0.72, 1.58, 0] },
  { bone: "Bip01 L Forearm", position: [-1.08, 1.43, 0] },
  { bone: "Bip01 R Forearm", position: [1.08, 1.43, 0] },
  { bone: "Bip01 L Hand", position: [-1.38, 1.30, 0] },
  { bone: "Bip01 R Hand", position: [1.38, 1.30, 0] },
  { bone: "Bip01 L Thigh", position: [-0.24, -0.25, 0] },
  { bone: "Bip01 R Thigh", position: [0.24, -0.25, 0] },
  { bone: "Bip01 L Calf", position: [-0.25, -1.10, 0] },
  { bone: "Bip01 R Calf", position: [0.25, -1.10, 0] },
  { bone: "Bip01 L Foot", position: [-0.25, -1.82, 0.12] },
  { bone: "Bip01 R Foot", position: [0.25, -1.82, 0.12] },
  { bone: "equip_left", position: [-1.42, 1.28, 0.05] },
  { bone: "equip_right", position: [1.42, 1.28, 0.05] },
  { bone: "stip", position: [0, -0.05, 0.18] }
];

const BONE_NAMES = [
  "Bip01",
  "Bip01 Pelvis",
  "Bip01 Spine",
  "Bip01 Spine1",
  "Bip01 Neck",
  "Bip01 Head",
  "Bip01 L Clavicle",
  "Bip01 R Clavicle",
  "Bip01 L UpperArm",
  "Bip01 R UpperArm",
  "Bip01 L Forearm",
  "Bip01 R Forearm",
  "Bip01 L Hand",
  "Bip01 R Hand",
  "Bip01 L Thigh",
  "Bip01 R Thigh",
  "Bip01 L Calf",
  "Bip01 R Calf",
  "Bip01 L Foot",
  "Bip01 R Foot",
  "equip_left",
  "equip_right",
  "stip"
];

function distance(a: [number, number, number], b: [number, number, number]): number {
  return Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

function weightsFor(position: [number, number, number]): BoneWeight[] {
  return ANCHORS.map((anchor) => ({
    boneId: anchor.bone,
    weight: 1 / (distance(position, anchor.position) + 0.08)
  }))
    .sort((a, b) => b.weight - a.weight)
    .slice(0, 4)
    .map((entry, index, entries) => {
      const total = entries.reduce((sum, item) => sum + item.weight, 0);
      return { boneId: entry.boneId, weight: total > 0 ? entry.weight / total : 0 };
    })
    .filter((entry) => entry.weight > 0.0001);
}

function parentFor(name: string): string | null {
  const normalized = name.toLowerCase().replace(/\s+/g, " ").trim();
  if (normalized === "bip01") return null;
  if (normalized === "bip01 pelvis" || normalized === "stip") return "Bip01";
  if (normalized === "bip01 spine") return "Bip01 Pelvis";
  if (normalized === "bip01 spine1") return "Bip01 Spine";
  if (normalized === "bip01 neck") return "Bip01 Spine1";
  if (normalized === "bip01 head") return "Bip01 Neck";
  if (normalized.endsWith("clavicle")) return "Bip01 Spine1";
  if (normalized.endsWith("upperarm")) return normalized.includes(" l ") ? "Bip01 L Clavicle" : "Bip01 R Clavicle";
  if (normalized.endsWith("forearm")) return normalized.includes(" l ") ? "Bip01 L UpperArm" : "Bip01 R UpperArm";
  if (normalized.endsWith("hand")) return normalized.includes(" l ") ? "Bip01 L Forearm" : "Bip01 R Forearm";
  if (normalized === "equip_left") return "Bip01 L Hand";
  if (normalized === "equip_right") return "Bip01 R Hand";
  if (normalized.endsWith("thigh")) return "Bip01 Pelvis";
  if (normalized.endsWith("calf")) return normalized.includes(" l ") ? "Bip01 L Thigh" : "Bip01 R Thigh";
  if (normalized.endsWith("foot")) return normalized.includes(" l ") ? "Bip01 L Calf" : "Bip01 R Calf";
  return "Bip01";
}

export function buildBoneHierarchy(names: string[]): Bone[] {
  const bones: Bone[] = names.map((name) => ({
    id: name,
    name,
    parent: parentFor(name),
    children: [],
    transform: { position: [0, 0, 0], rotation: [0, 0, 0], scale: [1, 1, 1] }
  }));
  const byId = new Map(bones.map((bone) => [bone.id, bone]));
  bones.forEach((bone) => {
    if (bone.parent && byId.has(bone.parent)) byId.get(bone.parent)?.children.push(bone.id);
  });
  return bones;
}

export function createSampleArmor(): SampleArmor {
  const positions: [number, number, number][] = [];
  const triangles: DisplayTriangle[] = [];
  const addVertex = (position: [number, number, number]): number => {
    positions.push(position);
    return positions.length - 1;
  };
  const addQuad = (a: number, b: number, c: number, d: number, materialIndex = 0) => {
    triangles.push({ materialIndex, vertexIndices: [a, b, c] });
    triangles.push({ materialIndex, vertexIndices: [a, c, d] });
  };

  const torsoLevels: Array<{ y: number; rx: number; rz: number }> = [
    { y: -0.28, rx: 0.24, rz: 0.18 },
    { y: 0.12, rx: 0.34, rz: 0.23 },
    { y: 0.55, rx: 0.38, rz: 0.25 },
    { y: 1.00, rx: 0.40, rz: 0.26 },
    { y: 1.38, rx: 0.38, rz: 0.25 },
    { y: 1.68, rx: 0.32, rz: 0.22 },
    { y: 1.92, rx: 0.20, rz: 0.18 }
  ];
  const radial = 18;
  const torsoRings: number[][] = torsoLevels.map(({ y, rx, rz }) => {
    const ring: number[] = [];
    for (let i = 0; i < radial; i += 1) {
      const angle = (i / radial) * Math.PI * 2;
      ring.push(addVertex([Math.cos(angle) * rx, y, Math.sin(angle) * rz]));
    }
    return ring;
  });
  for (let level = 0; level < torsoRings.length - 1; level += 1) {
    for (let i = 0; i < radial; i += 1) {
      const next = (i + 1) % radial;
      addQuad(torsoRings[level][i], torsoRings[level][next], torsoRings[level + 1][next], torsoRings[level + 1][i], 0);
    }
  }

  const limb = (
    start: [number, number, number],
    end: [number, number, number],
    radius: number,
    lengthSegments: number,
    radialSegments: number,
    materialIndex: number
  ) => {
    const direction: [number, number, number] = [end[0] - start[0], end[1] - start[1], end[2] - start[2]];
    const axis = Math.abs(direction[0]) >= Math.abs(direction[1]) ? "x" : "y";
    const rings: number[][] = [];
    for (let row = 0; row <= lengthSegments; row += 1) {
      const t = row / lengthSegments;
      const center: [number, number, number] = [
        start[0] + direction[0] * t,
        start[1] + direction[1] * t,
        start[2] + direction[2] * t
      ];
      const ring: number[] = [];
      for (let i = 0; i < radialSegments; i += 1) {
        const angle = (i / radialSegments) * Math.PI * 2;
        const offset = axis === "x"
          ? [0, Math.cos(angle) * radius, Math.sin(angle) * radius]
          : [Math.cos(angle) * radius, 0, Math.sin(angle) * radius];
        ring.push(addVertex([center[0] + offset[0], center[1] + offset[1], center[2] + offset[2]]));
      }
      rings.push(ring);
    }
    for (let row = 0; row < rings.length - 1; row += 1) {
      for (let i = 0; i < radialSegments; i += 1) {
        const next = (i + 1) % radialSegments;
        addQuad(rings[row][i], rings[row][next], rings[row + 1][next], rings[row + 1][i], materialIndex);
      }
    }
  };

  limb([-0.34, 1.58, 0], [-1.43, 1.28, 0], 0.13, 8, 12, 1);
  limb([0.34, 1.58, 0], [1.43, 1.28, 0], 0.13, 8, 12, 1);
  limb([-0.24, -0.02, 0], [-0.25, -1.88, 0.04], 0.16, 9, 12, 2);
  limb([0.24, -0.02, 0], [0.25, -1.88, 0.04], 0.16, 9, 12, 2);

  const headCenter: [number, number, number] = [0, 2.38, 0];
  const latitudes = 7;
  const longitudes = 16;
  const headRings: number[][] = [];
  for (let lat = 1; lat < latitudes; lat += 1) {
    const phi = (lat / latitudes) * Math.PI;
    const ring: number[] = [];
    for (let lon = 0; lon < longitudes; lon += 1) {
      const theta = (lon / longitudes) * Math.PI * 2;
      ring.push(addVertex([
        headCenter[0] + Math.sin(phi) * Math.cos(theta) * 0.19,
        headCenter[1] + Math.cos(phi) * 0.23,
        headCenter[2] + Math.sin(phi) * Math.sin(theta) * 0.20
      ]));
    }
    headRings.push(ring);
  }
  for (let row = 0; row < headRings.length - 1; row += 1) {
    for (let i = 0; i < longitudes; i += 1) {
      const next = (i + 1) % longitudes;
      addQuad(headRings[row][i], headRings[row][next], headRings[row + 1][next], headRings[row + 1][i], 3);
    }
  }

  const vertices: WeightVertex[] = positions.map((position, id) => ({ id, position, weights: weightsFor(position) }));

  return {
    meshId: "sample-warrior-armor",
    vertices,
    triangles,
    bones: BONE_NAMES,
    hierarchy: buildBoneHierarchy(BONE_NAMES)
  };
}
