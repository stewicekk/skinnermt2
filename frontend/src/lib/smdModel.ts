import type { DisplayTriangle, WeightVertex } from "@/stores/useRiggingStore";
import { exportToSMD, type SMDBone, type SMDFrame } from "@/lib/smdExporter";
import { normalizeAllWeights } from "@/lib/validation";

export interface ImportedSmdModel {
  meshId: string;
  vertices: WeightVertex[];
  triangles: DisplayTriangle[];
  bones: SMDBone[];
  boneNames: string[];
  frames: SMDFrame[];
  materials: string[];
}

function sectionLines(text: string, start: string): string[] {
  const lines = text.split(/\r?\n/).map((line) => line.trim());
  const output: string[] = [];
  let active = false;
  for (const line of lines) {
    if (!active) {
      if (line === start || line.startsWith(`${start} `)) active = true;
      continue;
    }
    if (line === "end") break;
    if (line) output.push(line);
  }
  return output;
}

function parseNodes(text: string): SMDBone[] {
  return sectionLines(text, "nodes").map((line, fallbackId) => {
    const match = line.match(/^(\d+)\s+"([^"]+)"\s+(-?\d+)/);
    if (!match) throw new Error(`Invalid SMD node line: ${line}`);
    return {
      id: Number(match[1]),
      name: match[2],
      parentId: Number(match[3]),
      position: [0, 0, 0],
      rotation: [0, 0, 0]
    };
  }).map((bone, index) => ({
    ...bone,
    id: Number.isFinite(bone.id) ? bone.id : index,
    position: bone.position as [number, number, number],
    rotation: bone.rotation as [number, number, number]
  }));
}

function parseSkeleton(text: string, bones: SMDBone[]): { bones: SMDBone[]; frames: SMDFrame[] } {
  const lines = sectionLines(text, "skeleton");
  const frames: SMDFrame[] = [];
  let current: SMDFrame | null = null;
  const bind = new Map<number, { position: [number, number, number]; rotation: [number, number, number] }>();

  for (const line of lines) {
    if (line.startsWith("time ")) {
      if (current) frames.push(current);
      current = { time: Number(line.split(/\s+/)[1] ?? frames.length), boneTransforms: [] };
      continue;
    }
    const parts = line.split(/\s+/).map(Number);
    if (!current || parts.length < 4 || parts.slice(0, 4).some((value) => !Number.isFinite(value))) continue;
    const boneId = parts[0];
    const position: [number, number, number] = [parts[1] ?? 0, parts[2] ?? 0, parts[3] ?? 0];
    const rotation: [number, number, number] = [parts[4] ?? 0, parts[5] ?? 0, parts[6] ?? 0];
    current.boneTransforms.push({ boneId, position, rotation });
    if (frames.length === 0 && !bind.has(boneId)) bind.set(boneId, { position, rotation });
  }
  if (current) frames.push(current);

  return {
    bones: bones.map((bone) => ({ ...bone, ...(bind.get(bone.id) ?? { position: bone.position, rotation: bone.rotation }) })),
    frames
  };
}

export function parseSmdModel(text: string, meshId: string): ImportedSmdModel {
  const bones = parseNodes(text);
  const boneNames = new Map(bones.map((bone) => [bone.id, bone.name]));
  const { bones: posedBones, frames } = parseSkeleton(text, bones);
  const triangleLines = sectionLines(text, "triangles");
  const vertices: WeightVertex[] = [];
  const triangles: DisplayTriangle[] = [];
  const materials: string[] = [];
  const vertexIndex = new Map<string, number>();
  let material = "material_0.dds";
  let remaining = 0;
  let current: number[] = [];

  const keyFor = (position: number[], weights: Array<{ bone: string; weight: number }>): string =>
    [...position.map((value) => value.toFixed(5)), ...weights.flatMap((entry) => [entry.bone, entry.weight.toFixed(5)])].join("|");

  for (const line of triangleLines) {
    if (remaining === 0) {
      material = line;
      if (!materials.includes(material)) materials.push(material);
      current = [];
      remaining = 3;
      continue;
    }
    const parts = line.split(/\s+/);
    const numbers = parts.map(Number);
    if (numbers.length < 10 || numbers.slice(0, 10).some((value) => !Number.isFinite(value))) {
      throw new Error(`Invalid SMD vertex line: ${line}`);
    }
    const linkCount = Math.max(0, Math.min(4, Math.floor(numbers[9])));
    const weights: Array<{ bone: string; weight: number }> = [];
    for (let i = 0; i < linkCount; i += 1) {
      const boneId = numbers[10 + i * 2];
      const weight = numbers[11 + i * 2];
      if (Number.isFinite(boneId) && Number.isFinite(weight) && weight > 0) {
        weights.push({ bone: boneNames.get(boneId) ?? `bone_${boneId}`, weight });
      }
    }
    const position: [number, number, number] = [numbers[1], numbers[2], numbers[3]];
    const key = keyFor(position, weights);
    let index = vertexIndex.get(key);
    if (index === undefined) {
      index = vertices.length;
      vertexIndex.set(key, index);
      const total = weights.reduce((sum, entry) => sum + entry.weight, 0) || 1;
      vertices.push({
        id: index,
        position,
        weights: weights.map((entry) => ({ boneId: entry.bone, weight: entry.weight / total }))
      });
    }
    current.push(index);
    remaining -= 1;
    if (remaining === 0) {
      if (current.length !== 3) throw new Error("Invalid SMD triangle.");
      const materialIndex = Math.max(0, materials.indexOf(material));
      triangles.push({ materialIndex, vertexIndices: [current[0], current[1], current[2]] });
    }
  }

  const normalized = normalizeAllWeights(vertices);
  return {
    meshId,
    vertices: normalized.map((vertex, id) => ({ ...vertex, id })),
    triangles,
    bones: posedBones,
    boneNames: posedBones.map((bone) => bone.name),
    frames,
    materials
  };
}

export function modelToSmd(
  vertices: WeightVertex[],
  triangles: DisplayTriangle[],
  bones: SMDBone[],
  frames: SMDFrame[] = [],
  materials: string[] = []
): string {
  const materialMap: Record<number, string> = Object.fromEntries(materials.map((material, index) => [index, material]));
  return exportToSMD(vertices, triangles, bones, frames, materialMap);
}
