import { useEffect, useMemo, useRef } from "react";
import * as THREE from "three";
import type { ThreeEvent } from "@react-three/fiber";
import { CommandHistoryManager, createWeightCommand, setupUndoRedoKeyboard } from "@/lib/undoRedo";
import { normalizeAllWeights } from "@/lib/validation";
import { useRiggingStore, type BoneWeight, type DisplayTriangle, type WeightVertex } from "@/stores/useRiggingStore";

interface RiggedModelProps {
  meshId: string;
  vertices: WeightVertex[];
  triangles: DisplayTriangle[];
  lockedBones: Set<string>;
  hiddenMaterials: Set<number>;
  deform: number;
  onPaintingChange: (painting: boolean) => void;
}

function heatColor(weight: number): [number, number, number] {
  const clamped = Math.max(0, Math.min(1, weight));
  return [clamped, 0.12 + clamped * 0.25, 1 - clamped];
}

function mirrorBoneName(boneId: string): string {
  if (boneId.includes(" L ")) return boneId.replace(" L ", " R ");
  if (boneId.includes(" R ")) return boneId.replace(" R ", " L ");
  if (/_L\b/.test(boneId)) return boneId.replace(/_L\b/, "_R");
  if (/_R\b/.test(boneId)) return boneId.replace(/_R\b/, "_L");
  if (/\bLEFT\b/i.test(boneId)) return boneId.replace(/LEFT/i, "RIGHT");
  if (/\bRIGHT\b/i.test(boneId)) return boneId.replace(/RIGHT/i, "LEFT");
  if (/\(L\)/.test(boneId)) return boneId.replace("(L)", "(R)");
  if (/\(R\)/.test(boneId)) return boneId.replace("(R)", "(L)");
  return boneId;
}

function adjustWeights(weights: BoneWeight[], boneId: string, delta: number): BoneWeight[] {
  const updated = weights.map((entry) => ({ ...entry }));
  const index = updated.findIndex((entry) => entry.boneId === boneId);
  if (index < 0) {
    if (delta <= 0) return updated;
    updated.push({ boneId, weight: delta });
  } else {
    updated[index] = { boneId, weight: Math.max(0, Math.min(1, updated[index].weight + delta)) };
  }
  const normalized = normalizeAllWeights([{ id: -1, position: [0, 0, 0], weights: updated }])[0];
  return normalized.weights.filter((entry) => entry.weight > 0.0001).slice(0, 4);
}

export function RiggedModel({ meshId, vertices, triangles, lockedBones, hiddenMaterials, deform, onPaintingChange }: RiggedModelProps) {
  const brushRadius = useRiggingStore((state) => state.brushRadius);
  const brushStrength = useRiggingStore((state) => state.brushStrength);
  const brushMode = useRiggingStore((state) => state.brushMode);
  const symmetryEnabled = useRiggingStore((state) => state.symmetryEnabled);
  const symmetryAxis = useRiggingStore((state) => state.symmetryAxis);
  const selectedBone = useRiggingStore((state) => state.selectedBone);
  const viewMode = useRiggingStore((state) => state.viewMode);
  const setMeshData = useRiggingStore((state) => state.setMeshData);

  const historyRef = useRef<CommandHistoryManager | null>(null);
  if (!historyRef.current) historyRef.current = new CommandHistoryManager();
  const strokePreviousRef = useRef(new Map<number, BoneWeight[]>());
  const paintingRef = useRef(false);

  const geometry = useMemo(() => {
    const geo = new THREE.BufferGeometry();
    const visibleTriangles = triangles.filter((triangle) => !hiddenMaterials.has(triangle.materialIndex));
    const positions = new Float32Array(vertices.length * 3);
    const colors = new Float32Array(vertices.length * 3);
    vertices.forEach((vertex, index) => {
      const deformInfluence = vertex.weights
        .filter((entry) => entry.boneId === "Bip01 Spine1" || entry.boneId === "Bip01 Neck" || entry.boneId === "Bip01 Head")
        .reduce((sum, entry) => sum + entry.weight, 0);
      positions[index * 3] = vertex.position[0];
      positions[index * 3 + 1] = vertex.position[1] + deform * deformInfluence;
      positions[index * 3 + 2] = vertex.position[2];
      const weight = selectedBone
        ? vertex.weights.find((entry) => entry.boneId === selectedBone)?.weight ?? 0
        : Math.max(0, ...vertex.weights.map((entry) => entry.weight));
      const [r, g, b] = heatColor(weight);
      colors[index * 3] = r;
      colors[index * 3 + 1] = g;
      colors[index * 3 + 2] = b;
    });
    const indices = new Uint32Array(visibleTriangles.flatMap((triangle) => triangle.vertexIndices));
    geo.setAttribute("position", new THREE.BufferAttribute(positions, 3));
    geo.setAttribute("color", new THREE.BufferAttribute(colors, 3));
    geo.setIndex(new THREE.BufferAttribute(indices, 1));
    geo.computeVertexNormals();
    return geo;
  }, [vertices, triangles, hiddenMaterials, deform, selectedBone]);

  useEffect(() => () => geometry.dispose(), [geometry]);

  const undo = () => {
    const command = historyRef.current?.undo();
    if (!command) return;
    const state = useRiggingStore.getState();
    const current = state.meshData[meshId] ?? [];
    const next = [...current];
    if (next[command.vertexIndex]) {
      next[command.vertexIndex] = { ...next[command.vertexIndex], weights: command.previousWeights };
      state.setMeshData(meshId, next);
    }
  };

  const redo = () => {
    const command = historyRef.current?.redo();
    if (!command) return;
    const state = useRiggingStore.getState();
    const current = state.meshData[meshId] ?? [];
    const next = [...current];
    if (next[command.vertexIndex]) {
      next[command.vertexIndex] = { ...next[command.vertexIndex], weights: command.newWeights };
      state.setMeshData(meshId, next);
    }
  };

  useEffect(() => setupUndoRedoKeyboard(undo, redo), [meshId]);

  const endStroke = () => {
    if (!paintingRef.current) return;
    paintingRef.current = false;
    onPaintingChange(false);
    const state = useRiggingStore.getState();
    const current = state.meshData[meshId] ?? [];
    strokePreviousRef.current.forEach((previousWeights, index) => {
      const nextVertex = current[index];
      if (!nextVertex) return;
      historyRef.current?.execute(createWeightCommand(
        meshId,
        index,
        previousWeights,
        nextVertex.weights.map((entry) => ({ ...entry })),
        brushMode === "add" ? "ADD_WEIGHT" : brushMode === "subtract" ? "SUBTRACT_WEIGHT" : brushMode === "smooth" ? "SMOOTH" : "NORMALIZE",
        `Brush ${brushMode} on vertex ${index}`
      ));
    });
    strokePreviousRef.current.clear();
  };

  const paintAtPoint = (point: THREE.Vector3, startStroke: boolean) => {
    const state = useRiggingStore.getState();
    const current = state.meshData[meshId] ?? [];
    const bone = state.selectedBone;
    if (!bone || lockedBones.has(bone) || current.length === 0) return;
    if (startStroke) {
      paintingRef.current = true;
      onPaintingChange(true);
      strokePreviousRef.current.clear();
    }
    if (!paintingRef.current) return;

    const radius = Math.max(0.05, state.brushRadius);
    const radiusSquared = radius * radius;
    const strength = Math.max(0, Math.min(1, state.brushStrength));
    const next = current.map((vertex) => ({ ...vertex, weights: vertex.weights.map((entry) => ({ ...entry })) }));
    let touched = false;

    const mirroredTarget = state.symmetryEnabled ? mirrorBoneName(bone) : bone;
    const changed = new Map<number, BoneWeight[]>();

    current.forEach((vertex, index) => {
      const dx = vertex.position[0] - point.x;
      const dy = vertex.position[1] - point.y;
      const dz = vertex.position[2] - point.z;
      const distanceSquared = dx * dx + dy * dy + dz * dz;
      if (distanceSquared > radiusSquared) return;
      const falloff = Math.pow(Math.max(0, 1 - Math.sqrt(distanceSquared) / radius), 2);
      if (state.brushMode === "normalize") {
        const normalized = normalizeAllWeights([next[index]])[0];
        if (JSON.stringify(normalized.weights) !== JSON.stringify(next[index].weights)) {
          if (!strokePreviousRef.current.has(index)) strokePreviousRef.current.set(index, current[index].weights.map((entry) => ({ ...entry })));
          next[index] = normalized;
          changed.set(index, normalized.weights);
          touched = true;
        }
        return;
      }
      const delta = state.brushMode === "subtract" ? -falloff * strength : falloff * strength * (state.brushMode === "smooth" ? 0.45 : 1);
      const updated = adjustWeights(next[index].weights, bone, delta);
      if (JSON.stringify(updated) !== JSON.stringify(next[index].weights)) {
        if (!strokePreviousRef.current.has(index)) strokePreviousRef.current.set(index, current[index].weights.map((entry) => ({ ...entry })));
        next[index] = { ...next[index], weights: updated };
        changed.set(index, updated);
        touched = true;
      }
    });

    if (state.symmetryEnabled) {
      changed.forEach((weights, index) => {
        const source = current[index];
        const target: [number, number, number] = [...source.position] as [number, number, number];
        if (state.symmetryAxis === "x") target[0] = -target[0];
        if (state.symmetryAxis === "y") target[1] = -target[1];
        if (state.symmetryAxis === "z") target[2] = -target[2];
        const mirrorIndex = current.findIndex((candidate, candidateIndex) => {
          if (candidateIndex === index) return false;
          return Math.abs(candidate.position[0] - target[0]) < 0.004 &&
            Math.abs(candidate.position[1] - target[1]) < 0.004 &&
            Math.abs(candidate.position[2] - target[2]) < 0.004;
        });
        if (mirrorIndex >= 0) {
          if (!strokePreviousRef.current.has(mirrorIndex)) strokePreviousRef.current.set(mirrorIndex, current[mirrorIndex].weights.map((entry) => ({ ...entry })));
          next[mirrorIndex] = {
            ...next[mirrorIndex],
            weights: weights.map((entry) => ({ boneId: mirrorBoneName(entry.boneId), weight: entry.weight }))
          };
          touched = true;
        }
      });
      void mirroredTarget;
    }

    if (touched) state.setMeshData(meshId, next);
  };

  const handlePointerDown = (event: ThreeEvent<PointerEvent>) => {
    event.stopPropagation();
    paintAtPoint(event.point, true);
  };

  const handlePointerMove = (event: ThreeEvent<PointerEvent>) => {
    if (event.nativeEvent.buttons !== 1) {
      if (paintingRef.current) endStroke();
      return;
    }
    event.stopPropagation();
    paintAtPoint(event.point, false);
  };

  return (
    <group>
      <mesh
        geometry={geometry}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={endStroke}
        onPointerLeave={endStroke}
      >
        <meshStandardMaterial
          vertexColors={viewMode === "heatmap"}
          color={viewMode === "heatmap" ? "#ffffff" : "#8fa1b5"}
          wireframe={viewMode === "wireframe"}
          flatShading={false}
          side={THREE.DoubleSide}
        />
      </mesh>
      <points geometry={geometry}>
        <pointsMaterial size={0.025} vertexColors={viewMode === "heatmap"} color={viewMode === "heatmap" ? "#ffffff" : "#334155"} sizeAttenuation transparent opacity={0.85} depthWrite={false} />
      </points>
    </group>
  );
}
