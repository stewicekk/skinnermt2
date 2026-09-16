import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useRiggingStore } from "@/stores/useRiggingStore";
import type { Bone, DisplayTriangle, WeightVertex } from "@/stores/useRiggingStore";
import { Scene } from "@/canvas/Scene";
import { RiggedModel } from "@/canvas/RiggedModel";
import { BoneTree } from "@/components/BoneTree";
import { ToastContainer } from "@/components/ToastContainer";
import { ErrorBoundary } from "@/components/ErrorBoundary";
import { buildBoneHierarchy, createSampleArmor } from "@/lib/sampleArmor";
import { parseSmdModel, modelToSmd } from "@/lib/smdModel";
import type { SMDBone, SMDFrame } from "@/lib/smdExporter";
import { autoAssignZeroWeightVertices, canExport, normalizeAllWeights, validatePreExport, type ValidationResult } from "@/lib/validation";
import { generateMaterialGroups, generateMSM, validateMSM } from "@/lib/msmGenerator";
import { useBoneLocking } from "@/lib/boneLocking";
import { autoSave, hasRestorableSession, restoreSession, startAutoSave } from "@/lib/autoSave";
import { getLocalProjectDatabase } from "@/lib/database";
import { AnimationTimeline, useAnimationPlayer } from "@/lib/animationPlayer";
import { compileGr2, generateMsm, learnWeights } from "@/api/client";
import { CommandPalette, type Command } from "@/components/CommandPalette";
import { StatusBar } from "@/components/StatusBar";
import { WeightHealth } from "@/components/WeightHealth";
import "./index.css";

interface LogEntry {
  id: string;
  level: "info" | "success" | "warning" | "error";
  message: string;
  timestamp: string;
}

interface ProjectSnapshotData {
  vertices: WeightVertex[];
  triangles: DisplayTriangle[];
  bones: string[];
  smdBones: SMDBone[];
  frames: SMDFrame[];
  materials: string[];
  targetClass: string;
}

interface StoredProject {
  id: string;
  name: string;
  updatedAt: string;
}

const CLASSES = ["warrior", "ninja", "sura", "shaman", "wolfman"] as const;

function downloadFile(filename: string, content: string, mime = "text/plain;charset=utf-8") {
  const blob = new Blob([content], { type: mime });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function smdBonesFromNames(names: string[]): SMDBone[] {
  const hierarchy = buildBoneHierarchy(names);
  const indexById = new Map(hierarchy.map((bone, index) => [bone.id, index]));
  return hierarchy.map((bone) => ({
    id: indexById.get(bone.id) ?? 0,
    name: bone.name,
    parentId: bone.parent ? indexById.get(bone.parent) ?? -1 : -1,
    position: [0, 0, 0],
    rotation: [0, 0, 0]
  }));
}

function msmClass(targetClass: string): "warrior" | "ninja" | "sura" | "shaman" | "wolfman" {
  const normalized = targetClass.toLowerCase();
  return (CLASSES as readonly string[]).includes(normalized)
    ? (normalized as "warrior" | "ninja" | "sura" | "shaman" | "wolfman")
    : "warrior";
}

export const App: React.FC = () => {
  const currentMesh = useRiggingStore((state) => state.currentMesh);
  const meshData = useRiggingStore((state) => state.meshData);
  const meshTriangles = useRiggingStore((state) => state.meshTriangles);
  const boneLists = useRiggingStore((state) => state.boneLists);
  const brushRadius = useRiggingStore((state) => state.brushRadius);
  const brushStrength = useRiggingStore((state) => state.brushStrength);
  const brushMode = useRiggingStore((state) => state.brushMode);
  const viewMode = useRiggingStore((state) => state.viewMode);
  const cameraView = useRiggingStore((state) => state.cameraView);
  const showGrid = useRiggingStore((state) => state.showGrid);
  const showAxes = useRiggingStore((state) => state.showAxes);
  const symmetryEnabled = useRiggingStore((state) => state.symmetryEnabled);
  const symmetryAxis = useRiggingStore((state) => state.symmetryAxis);
  const selectedBone = useRiggingStore((state) => state.selectedBone);
  const targetClass = useRiggingStore((state) => state.targetClass);
  const isProcessing = useRiggingStore((state) => state.isProcessing);
  const error = useRiggingStore((state) => state.error);

  const [painting, setPainting] = useState(false);
  const [hiddenMaterials, setHiddenMaterials] = useState<Set<number>>(new Set());
  const [hiddenBones, setHiddenBones] = useState<string[]>([]);
  const [selectedMaterial, setSelectedMaterial] = useState(0);
  const [smdBones, setSmdBones] = useState<SMDBone[]>([]);
  const [animationFrames, setAnimationFrames] = useState<SMDFrame[]>([]);
  const [materials, setMaterials] = useState<string[]>(["armor_body.dds", "armor_trim.dds", "armor_cloth.dds", "armor_head.dds"]);
  const [modelPath, setModelPath] = useState("d:/ymir work/pc/warrior/sample_armor.gr2");
  const [textureText, setTextureText] = useState("armor_body.dds\narmor_trim.dds\narmor_cloth.dds\narmor_head.dds");
  const [validation, setValidation] = useState<ValidationResult | null>(null);
  const [logs, setLogs] = useState<LogEntry[]>([{ id: "boot", level: "info", message: "Studio initialized", timestamp: new Date().toLocaleTimeString() }]);
  const [projects, setProjects] = useState<StoredProject[]>([]);
  const [activeProjectId, setActiveProjectId] = useState<string | null>(null);
  const [animPlaying, setAnimPlaying] = useState(false);
  const [commandPaletteOpen, setCommandPaletteOpen] = useState(false);

  const modelInput = useRef<HTMLInputElement>(null);
  const referenceInput = useRef<HTMLInputElement>(null);
  const animationInput = useRef<HTMLInputElement>(null);
  const initializedRef = useRef(false);
  const snapshotRef = useRef<() => ProjectSnapshotData | null>(() => null);

  const { lockedBones, toggleBoneLock, lockSocketBones } = useBoneLocking();
  const vertices = currentMesh ? meshData[currentMesh] ?? [] : [];
  const triangles = currentMesh ? meshTriangles[currentMesh] ?? [] : [];
  const boneNames = currentMesh ? boneLists[currentMesh] ?? [] : [];
  const boneTree: Bone[] = useMemo(() => buildBoneHierarchy(boneNames), [boneNames]);
  const validity = useMemo(() => {
    let invalid = 0;
    vertices.forEach((vertex) => {
      const total = vertex.weights.reduce((sum, entry) => sum + entry.weight, 0);
      if (Math.abs(total - 1) > 0.001 || vertex.weights.length > 4) invalid += 1;
    });
    return { valid: invalid === 0, invalidCount: invalid };
  }, [vertices]);

  const pushLog = useCallback((message: string, level: LogEntry["level"] = "info") => {
    setLogs((previous) => [
      { id: `${Date.now()}-${Math.random().toString(36).slice(2)}`, level, message, timestamp: new Date().toLocaleTimeString() },
      ...previous
    ].slice(0, 80));
  }, []);

  const notify = useCallback((type: "success" | "error" | "warning" | "info", title: string, message: string) => {
    useRiggingStore.getState().addToast({ type, title, message, duration: 4200 });
  }, []);

  const applyLoadedModel = useCallback((model: {
    meshId: string;
    vertices: WeightVertex[];
    triangles: DisplayTriangle[];
    boneNames: string[];
    bones: SMDBone[];
    frames: SMDFrame[];
    materials: string[];
  }) => {
    const store = useRiggingStore.getState();
    store.setCurrentMesh(model.meshId);
    store.setMeshData(model.meshId, model.vertices);
    store.setMeshTriangles(model.meshId, model.triangles);
    store.setBoneList(model.meshId, model.boneNames);
    setSmdBones(model.bones);
    setAnimationFrames(model.frames);
    setMaterials(model.materials.length > 0 ? model.materials : ["material_0.dds"]);
    setTextureText((model.materials.length > 0 ? model.materials : ["material_0.dds"]).join("\n"));
    setHiddenMaterials(new Set());
    setHiddenBones([]);
    setSelectedMaterial(0);
    setValidation(null);
    store.setSelectedBone(model.boneNames.find((name) => name.toLowerCase().includes("spine1")) ?? model.boneNames[0] ?? null);
    lockSocketBones(model.boneNames);
  }, [lockSocketBones]);

  const loadSample = useCallback(() => {
    const sample = createSampleArmor();
    applyLoadedModel({
      meshId: sample.meshId,
      vertices: sample.vertices,
      triangles: sample.triangles,
      boneNames: sample.bones,
      bones: smdBonesFromNames(sample.bones),
      frames: [],
      materials: ["armor_body.dds", "armor_trim.dds", "armor_cloth.dds", "armor_head.dds"]
    });
    setModelPath(`d:/ymir work/pc/${useRiggingStore.getState().targetClass}/sample_armor.gr2`);
    pushLog(`Loaded sample armor: ${sample.vertices.length} vertices, ${sample.triangles.length} triangles`, "success");
  }, [applyLoadedModel, pushLog]);

  const refreshProjects = useCallback(async () => {
    try {
      const database = getLocalProjectDatabase();
      const items = await database.findByOwner("local-user");
      setProjects(items.map((item) => ({ id: item.id, name: item.name, updatedAt: item.updatedAt })));
    } catch (queryError) {
      pushLog(queryError instanceof Error ? queryError.message : "Project query failed", "error");
    }
  }, [pushLog]);

  useEffect(() => {
    if (initializedRef.current) return;
    initializedRef.current = true;
    void (async () => {
      try {
        const saved = await restoreSession().catch(() => null);
        if (saved && saved.vertices.length > 0) {
          applyLoadedModel({
            meshId: saved.meshId,
            vertices: saved.vertices,
            triangles: saved.triangles,
            boneNames: saved.bones,
            bones: saved.smdBones,
            frames: saved.frames,
            materials: saved.materials
          });
          useRiggingStore.getState().setTargetClass(saved.targetClass);
          pushLog(`Restored autosaved session from ${new Date(saved.timestamp).toLocaleString()}`, "success");
        } else {
          loadSample();
        }
      } catch (restoreError) {
        loadSample();
        pushLog(restoreError instanceof Error ? restoreError.message : "Session restore failed", "error");
      }
      await refreshProjects();
    })();

    const stop = startAutoSave(async () => {
      const snapshot = snapshotRef.current();
      const store = useRiggingStore.getState();
      if (!snapshot || !store.currentMesh) return;
      await autoSave(
        store.currentMesh,
        snapshot.vertices,
        snapshot.triangles,
        snapshot.bones,
        snapshot.smdBones,
        snapshot.frames,
        snapshot.materials,
        store.targetClass,
        { radius: store.brushRadius, strength: store.brushStrength, mode: store.brushMode }
      );
    });
    return stop;
  }, [applyLoadedModel, loadSample, pushLog, refreshProjects]);

  useEffect(() => {
    snapshotRef.current = currentMesh
      ? () => ({ vertices, triangles, bones: boneNames, smdBones, frames: animationFrames, materials, targetClass })
      : () => null;
  }, [currentMesh, vertices, triangles, boneNames, smdBones, animationFrames, materials, targetClass]);

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      if (target && (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT")) return;
      if ((event.key === "h" || event.key === "H") && !event.ctrlKey && !event.metaKey && !event.altKey) {
        event.preventDefault();
        setHiddenMaterials((previous) => {
          const next = new Set(previous);
          if (next.has(selectedMaterial)) next.delete(selectedMaterial);
          else next.add(selectedMaterial);
          return next;
        });
        pushLog(`Toggled sub-mesh visibility for material ${selectedMaterial}`, "info");
      }
      if ((event.key === "h" || event.key === "H") && (event.altKey || event.metaKey)) {
        event.preventDefault();
        setHiddenMaterials(new Set());
        pushLog("Restored all sub-meshes", "info");
      }
    };
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, [selectedMaterial, pushLog]);

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && event.key === "p") {
        event.preventDefault();
        setCommandPaletteOpen(true);
      }
    };
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, []);

  const readTextFile = (file: File): Promise<string> => new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result ?? ""));
    reader.onerror = () => reject(new Error("File read failed"));
    reader.readAsText(file);
  });

  const handleModelFile = async (file: File) => {
    const store = useRiggingStore.getState();
    store.setIsProcessing(true);
    store.setError(null);
    try {
      const text = await readTextFile(file);
      const meshId = file.name.replace(/\.[^.]+$/, "") || `model-${Date.now()}`;
      const parsed = parseSmdModel(text, meshId);
      applyLoadedModel(parsed);
      setModelPath(`d:/ymir work/pc/${store.targetClass}/${meshId}.gr2`);
      pushLog(`Imported SMD model ${file.name}: ${parsed.vertices.length} vertices`, "success");
      notify("success", "Model imported", `${parsed.vertices.length} vertices loaded from ${file.name}.`);
    } catch (importError) {
      const message = importError instanceof Error ? importError.message : "Failed to parse SMD model";
      store.setError(message);
      pushLog(message, "error");
      notify("error", "Import failed", message);
    } finally {
      store.setIsProcessing(false);
    }
  };

  const ensureNormalized = (): WeightVertex[] => {
    const store = useRiggingStore.getState();
    if (!currentMesh) return [];
    const assigned = autoAssignZeroWeightVertices(normalizeAllWeights(store.meshData[currentMesh] ?? []), boneNames);
    store.setMeshData(currentMesh, assigned);
    return assigned;
  };

  const runValidation = (nextVertices: WeightVertex[] = vertices): ValidationResult => {
    const result = validatePreExport(nextVertices, boneNames);
    setValidation(result);
    if (!result.valid) {
      pushLog(`Validation failed: ${result.errors.join("; ")}`, "error");
      notify("error", "Pre-export validation failed", result.errors.join("; "));
    } else if (result.warnings.length > 0 || result.corrections.length > 0) {
      pushLog(`Validation passed with notes: ${[...result.warnings, ...result.corrections].join("; ")}`, "warning");
    } else {
      pushLog("Pre-export validation passed", "success");
    }
    return result;
  };

  const handleExportSmd = () => {
    if (!currentMesh) return;
    const normalized = ensureNormalized();
    const result = runValidation(normalized);
    if (!canExport(result)) return;
    const content = modelToSmd(normalized, triangles, smdBones, animationFrames, materials);
    downloadFile(`${currentMesh}.smd`, content);
    pushLog(`Exported ${currentMesh}.smd (${content.length} bytes)`, "success");
    notify("success", "SMD exported", `${currentMesh}.smd is ready.`);
  };

  const handleExportMsm = () => {
    if (!currentMesh) return;
    const normalized = ensureNormalized();
    const result = runValidation(normalized);
    if (!canExport(result)) return;
    const groups = generateMaterialGroups(normalized, triangles);
    const texturePaths = textureText.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
    const content = generateMSM({
      modelName: currentMesh,
      classType: msmClass(targetClass),
      meshPath: modelPath,
      texturePaths: texturePaths.length > 0 ? texturePaths : materials,
      materialGroups: groups
    });
    const msmValidation = validateMSM(content);
    if (!msmValidation.valid) {
      pushLog(`MSM validation failed: ${msmValidation.errors.join("; ")}`, "error");
      notify("error", "MSM validation failed", msmValidation.errors.join("; "));
      return;
    }
    downloadFile(`${currentMesh}.msm`, content);
    pushLog(`Exported ${currentMesh}.msm with ${groups.length} shape groups`, "success");
    notify("success", "MSM exported", `${groups.length} material groups written.`);
  };

  const handleCompileGr2 = async () => {
    if (!currentMesh) return;
    const store = useRiggingStore.getState();
    const normalized = ensureNormalized();
    const result = runValidation(normalized);
    if (!canExport(result)) return;
    store.setIsProcessing(true);
    try {
      const smd = modelToSmd(normalized, triangles, smdBones, animationFrames, materials);
      const response = await compileGr2({
        smd_content: smd,
        material_map: Object.fromEntries(materials.map((material, index) => [String(index), material]))
      });
      const binary = Uint8Array.from(atob(response.content_base64), (character) => character.charCodeAt(0));
      const blob = new Blob([binary.buffer as ArrayBuffer], { type: "application/octet-stream" });
      const url = URL.createObjectURL(blob);
      const anchor = document.createElement("a");
      anchor.href = url;
      anchor.download = response.filename;
      document.body.appendChild(anchor);
      anchor.click();
      anchor.remove();
      window.setTimeout(() => URL.revokeObjectURL(url), 1000);
      pushLog(`Downloaded ${response.filename} (${response.file_size} bytes)`, "success");
      notify("success", "GR2 compile request completed", response.filename);
    } catch (compileError) {
      const message = compileError instanceof Error ? compileError.message : "GR2 compilation failed";
      store.setError(message);
      pushLog(message, "error");
      notify("error", "GR2 compilation failed", message);
    } finally {
      store.setIsProcessing(false);
    }
  };

  const runAiTransfer = async (referenceVertices: WeightVertex[]) => {
    const store = useRiggingStore.getState();
    if (!currentMesh) return;
    const target = store.meshData[currentMesh] ?? [];
    if (target.length === 0 || referenceVertices.length === 0) {
      throw new Error("Both reference and target meshes must contain vertices.");
    }
    store.setIsProcessing(true);
    try {
      const response = await learnWeights({
        source_vertices: referenceVertices.map((vertex) => [...vertex.position]),
        target_vertices: target.map((vertex) => [...vertex.position]),
        source_weights: referenceVertices.map((vertex) => vertex.weights.map((entry) => [entry.boneId, entry.weight] as [string, number])),
        bone_map: { bones: boneNames }
      });
      if (response.transferred_weights.length !== target.length) {
        throw new Error("AI backend returned an incompatible number of vertices.");
      }
      const transferred = target.map((vertex, index) => ({
        ...vertex,
        weights: boneNames
          .map((bone, boneIndex) => ({ boneId: bone, weight: response.transferred_weights[index]?.[boneIndex] ?? 0 }))
          .filter((entry) => entry.weight > 0.0001)
      }));
      store.setMeshData(currentMesh, normalizeAllWeights(transferred));
      runValidation(normalizeAllWeights(transferred));
      pushLog(`AI weight transfer completed using ${response.method}`, "success");
      notify("success", "AI transfer completed", `${response.source_vertices_analyzed} reference vertices analyzed.`);
    } finally {
      store.setIsProcessing(false);
    }
  };

  const handleReferenceFile = async (file: File) => {
    try {
      const parsed = parseSmdModel(await readTextFile(file), `reference-${Date.now()}`);
      await runAiTransfer(parsed.vertices);
    } catch (transferError) {
      const message = transferError instanceof Error ? transferError.message : "AI transfer failed";
      useRiggingStore.getState().setError(message);
      pushLog(message, "error");
      notify("error", "AI transfer failed", message);
    }
  };

  const handleAnimationFile = async (file: File) => {
    try {
      const parsed = parseSmdModel(await readTextFile(file), `animation-${Date.now()}`);
      if (parsed.frames.length === 0) throw new Error("No animation frames found in this SMD file.");
      setAnimationFrames(parsed.frames);
      setAnimPlaying(true);
      pushLog(`Loaded animation ${file.name}: ${parsed.frames.length} frames`, "success");
      notify("success", "Animation loaded", `${parsed.frames.length} frames ready for preview.`);
    } catch (animationError) {
      const message = animationError instanceof Error ? animationError.message : "Animation import failed";
      pushLog(message, "error");
      notify("error", "Animation import failed", message);
    }
  };

  const saveProjectSnapshot = async () => {
    if (!currentMesh) return;
    const store = useRiggingStore.getState();
    const database = getLocalProjectDatabase();
    const snapshot: ProjectSnapshotData = {
      vertices,
      triangles,
      bones: boneNames,
      smdBones,
      frames: animationFrames,
      materials,
      targetClass
    };
    if (activeProjectId) {
      const project = await database.findById(activeProjectId);
      if (!project || !("ownerId" in project)) throw new Error("Active project not found.");
      await database.update(activeProjectId, { name: currentMesh, targetClass, updatedAt: new Date().toISOString() });
      await database.create({
        projectId: activeProjectId,
        commitMessage: `Manual snapshot ${new Date().toLocaleTimeString()}`,
        data: snapshot as unknown as Record<string, unknown>,
        authorId: "local-user"
      });
    } else {
      const project = await database.create({ name: currentMesh, targetClass, ownerId: "local-user", metadata: {} });
      const created = Array.isArray(project) ? project[0] : project;
      setActiveProjectId(created.id);
      await database.create({
        projectId: created.id,
        commitMessage: "Initial project snapshot",
        data: snapshot as unknown as Record<string, unknown>,
        authorId: "local-user"
      });
    }
    await refreshProjects();
    pushLog("Project snapshot saved to local project database", "success");
    notify("success", "Project saved", "Snapshot stored with full mesh, bones and animation data.");
  };

  const restoreProject = async (projectId: string) => {
    const database = getLocalProjectDatabase();
    const latest = await database.getLatest(projectId);
    if (!latest) throw new Error("No project snapshot found.");
    const snapshot = latest.data as unknown as ProjectSnapshotData;
    applyLoadedModel({
      meshId: projectId,
      vertices: snapshot.vertices,
      triangles: snapshot.triangles,
      boneNames: snapshot.bones,
      bones: snapshot.smdBones,
      frames: snapshot.frames,
      materials: snapshot.materials
    });
    useRiggingStore.getState().setTargetClass(snapshot.targetClass);
    setActiveProjectId(projectId);
    pushLog(`Restored project snapshot v${latest.version}`, "success");
  };

  const onAnimationFrame = useCallback(() => undefined, []);
  const player = useAnimationPlayer({ bones: smdBones, frames: animationFrames, isPlaying: animPlaying, onFrameChange: onAnimationFrame });
  const deform = animationFrames.length > 1
    ? Math.sin((player.state.currentFrame / Math.max(1, animationFrames.length - 1)) * Math.PI * 2) * 0.12
    : 0;

  const commands: Command[] = [
    { id: "import-smd", name: "Import SMD", category: "File", action: () => modelInput.current?.click() },
    { id: "sample", name: "Load Sample Armor", category: "File", action: loadSample },
    { id: "export-smd", name: "Export SMD", category: "Export", action: handleExportSmd },
    { id: "export-msm", name: "Export MSM", category: "Export", action: handleExportMsm },
    { id: "normalize", name: "Normalize All Weights", category: "Weights", action: () => { ensureNormalized(); runValidation(); } },
    { id: "validate", name: "Validate", category: "Weights", action: () => runValidation() },
    { id: "view-solid", name: "Solid View", category: "View", action: () => useRiggingStore.getState().setViewMode("solid") },
    { id: "view-wireframe", name: "Wireframe View", category: "View", action: () => useRiggingStore.getState().setViewMode("wireframe") },
    { id: "view-heatmap", name: "Weight Heatmap", category: "View", action: () => useRiggingStore.getState().setViewMode("heatmap") },
    { id: "view-xray", name: "X-Ray View", category: "View", action: () => useRiggingStore.getState().setViewMode("xray") },
    { id: "view-skeleton", name: "Skeleton View", category: "View", action: () => useRiggingStore.getState().setViewMode("skeleton") },
    { id: "cam-perspective", name: "Camera Perspective", category: "Camera", action: () => useRiggingStore.getState().setCameraView("perspective") },
    { id: "cam-front", name: "Camera Front", category: "Camera", action: () => useRiggingStore.getState().setCameraView("front") },
    { id: "cam-back", name: "Camera Back", category: "Camera", action: () => useRiggingStore.getState().setCameraView("back") },
    { id: "cam-left", name: "Camera Left", category: "Camera", action: () => useRiggingStore.getState().setCameraView("left") },
    { id: "cam-right", name: "Camera Right", category: "Camera", action: () => useRiggingStore.getState().setCameraView("right") },
    { id: "cam-top", name: "Camera Top", category: "Camera", action: () => useRiggingStore.getState().setCameraView("top") },
    { id: "cam-bottom", name: "Camera Bottom", category: "Camera", action: () => useRiggingStore.getState().setCameraView("bottom") },
    { id: "toggle-grid", name: "Toggle Grid", category: "View", action: () => useRiggingStore.getState().setShowGrid(!useRiggingStore.getState().showGrid) },
    { id: "toggle-axes", name: "Toggle Axes", category: "View", action: () => useRiggingStore.getState().setShowAxes(!useRiggingStore.getState().showAxes) },
    { id: "brush-add", name: "Brush: Add", category: "Brush", action: () => useRiggingStore.getState().setBrushMode("add") },
    { id: "brush-subtract", name: "Brush: Subtract", category: "Brush", action: () => useRiggingStore.getState().setBrushMode("subtract") },
    { id: "brush-set", name: "Brush: Set", category: "Brush", action: () => useRiggingStore.getState().setBrushMode("set") },
    { id: "brush-smooth", name: "Brush: Smooth", category: "Brush", action: () => useRiggingStore.getState().setBrushMode("smooth") },
    { id: "brush-blur", name: "Brush: Blur", category: "Brush", action: () => useRiggingStore.getState().setBrushMode("blur") },
    { id: "brush-sharpen", name: "Brush: Sharpen", category: "Brush", action: () => useRiggingStore.getState().setBrushMode("sharpen") },
    { id: "brush-normalize", name: "Brush: Normalize", category: "Brush", action: () => useRiggingStore.getState().setBrushMode("normalize") },
    { id: "brush-prune", name: "Brush: Prune", category: "Brush", action: () => useRiggingStore.getState().setBrushMode("prune") },
    { id: "brush-flood", name: "Brush: Flood", category: "Brush", action: () => useRiggingStore.getState().setBrushMode("flood") },
  ];

  return (
    <div className="min-h-screen bg-gray-950 text-white overflow-hidden">
      <header className="border-b border-gray-800 bg-gray-900 px-4 py-3 flex flex-wrap items-center gap-3 justify-between">
        <div>
          <h1 className="text-xl font-bold">Metin2 Rigging Studio</h1>
          <p className="text-xs text-gray-400">Mesh {currentMesh ?? "none"} · {vertices.length} vertices · {triangles.length} triangles</p>
        </div>
        <div className="flex flex-wrap items-center gap-2">
          <span className="text-xs text-gray-400">Class</span>
          {CLASSES.map((cls) => (
            <button
              key={cls}
              onClick={() => useRiggingStore.getState().setTargetClass(cls)}
              className={`px-3 py-1 rounded text-xs capitalize ${targetClass === cls ? "bg-blue-500 text-white" : "text-gray-300 hover:bg-gray-700"}`}
            >
              {cls}
            </button>
          ))}
          <span className="text-xs text-gray-400">View</span>
          {(["solid", "wireframe", "heatmap", "xray", "skeleton"] as const).map((mode) => (
            <button
              key={mode}
              onClick={() => useRiggingStore.getState().setViewMode(mode)}
              className={`px-3 py-1 rounded text-xs capitalize ${viewMode === mode ? "bg-emerald-500 text-white" : "text-gray-300 hover:bg-gray-700"}`}
            >
              {mode === "xray" ? "X-Ray" : mode}
            </button>
          ))}
          <span className="text-xs text-gray-400 ml-2">Cam</span>
          {(["perspective", "front", "back", "left", "right", "top", "bottom"] as const).map((view) => (
            <button
              key={view}
              onClick={() => useRiggingStore.getState().setCameraView(view)}
              className={`px-2 py-1 rounded text-xs capitalize ${cameraView === view ? "bg-purple-500 text-white" : "text-gray-300 hover:bg-gray-700"}`}
            >
              {view.slice(0, 3)}
            </button>
          ))}
        </div>
      </header>

      <div className="flex h-[calc(100vh-132px)]">
        <aside className="w-72 border-r border-gray-800 bg-gray-950 flex flex-col overflow-auto p-4">
          <div className="flex gap-2 mb-3">
            <button onClick={() => modelInput.current?.click()} className="flex-1 px-2 py-2 rounded text-xs bg-blue-600 hover:bg-blue-500">Import SMD</button>
            <button onClick={loadSample} className="flex-1 px-2 py-2 rounded text-xs bg-gray-700 hover:bg-gray-600">Sample armor</button>
          </div>
          <input ref={modelInput} type="file" accept=".smd" className="hidden" onChange={(event) => { const file = event.target.files?.[0]; if (file) void handleModelFile(file); event.target.value = ""; }} />
          <BoneTree
            bones={boneTree}
            selectedBone={selectedBone}
            onSelectBone={(boneId) => useRiggingStore.getState().setSelectedBone(boneId)}
            lockedBones={Array.from(lockedBones.keys())}
            onToggleLock={toggleBoneLock}
            hiddenBones={hiddenBones}
            onToggleHidden={(boneId) => setHiddenBones((previous) => previous.includes(boneId) ? previous.filter((item) => item !== boneId) : [...previous, boneId])}
          />
          <div className="mt-4 pt-4 border-t border-gray-800 text-xs text-gray-500 space-y-1">
            <div>Selected bone: {selectedBone ?? "None"}</div>
            <div>Invalid vertices: {validity.invalidCount}</div>
            <div>Symmetry: {symmetryEnabled ? `on (${symmetryAxis})` : "off"}</div>
          </div>
          <div className="mt-4 border-t border-gray-800 pt-3">
            <h3 className="text-xs uppercase text-gray-400 mb-2">Sub-meshes</h3>
            {materials.map((material, index) => (
              <button
                key={`${material}-${index}`}
                onClick={() => { setSelectedMaterial(index); setHiddenMaterials((previous) => { const next = new Set(previous); if (next.has(index)) next.delete(index); else next.add(index); return next; }); }}
                className={`w-full text-left px-2 py-1 rounded text-xs mb-1 ${hiddenMaterials.has(index) ? "bg-red-900 text-red-200" : selectedMaterial === index ? "bg-gray-700 text-white" : "text-gray-300 hover:bg-gray-800"}`}
              >
                {hiddenMaterials.has(index) ? "Hidden" : "Visible"} · {material}
              </button>
            ))}
            <p className="text-[11px] text-gray-500">H hides the selected sub-mesh, Alt+H restores all.</p>
          </div>
        </aside>

        <main className="flex-1 relative flex flex-col">
          <div className="flex-1 relative">
            <ErrorBoundary>
              {currentMesh ? (
                <Scene controlsEnabled={!painting}>
                  <RiggedModel
                    meshId={currentMesh}
                    vertices={vertices}
                    triangles={triangles}
                    lockedBones={new Set(lockedBones.keys())}
                    hiddenMaterials={hiddenMaterials}
                    deform={deform}
                    onPaintingChange={setPainting}
                  />
                </Scene>
              ) : (
                <div className="h-full flex items-center justify-center text-sm text-gray-400">No mesh loaded</div>
              )}
            </ErrorBoundary>
            <div className="absolute top-4 left-4 flex gap-2">
              {(["add", "subtract", "set", "smooth", "blur", "sharpen", "normalize", "prune", "flood"] as const).map((mode) => (
                <button
                  key={mode}
                  onClick={() => useRiggingStore.getState().setBrushMode(mode)}
                  className={`px-3 py-1 rounded text-xs capitalize ${brushMode === mode ? "bg-blue-500 text-white" : "bg-gray-900/80 text-gray-300 hover:bg-gray-700"}`}
                >
                  {mode}
                </button>
              ))}
            </div>
            <div className="absolute bottom-4 left-4 bg-gray-900/80 backdrop-blur rounded p-3 space-y-2 w-48">
              <label className="block text-[11px] text-gray-300">Brush radius: {brushRadius.toFixed(2)}</label>
              <input type="range" min="0.1" max="2.5" step="0.05" value={brushRadius} onChange={(event) => useRiggingStore.getState().setBrushRadius(Number(event.target.value))} className="w-full" />
              <label className="block text-[11px] text-gray-300">Brush strength: {brushStrength.toFixed(2)}</label>
              <input type="range" min="0.05" max="1" step="0.05" value={brushStrength} onChange={(event) => useRiggingStore.getState().setBrushStrength(Number(event.target.value))} className="w-full" />
              <button onClick={() => { ensureNormalized(); runValidation(); }} className="w-full px-2 py-1 rounded text-xs bg-orange-600 hover:bg-orange-500">Normalize all weights</button>
            </div>
            <div className="absolute top-4 right-4 flex gap-2">
              <button onClick={() => useRiggingStore.getState().setShowGrid(!useRiggingStore.getState().showGrid)} className={`px-2 py-1 rounded text-xs ${showGrid ? "bg-gray-700 text-white" : "bg-gray-900/80 text-gray-500"}`}>Grid</button>
              <button onClick={() => useRiggingStore.getState().setShowAxes(!useRiggingStore.getState().showAxes)} className={`px-2 py-1 rounded text-xs ${showAxes ? "bg-gray-700 text-white" : "bg-gray-900/80 text-gray-500"}`}>Axes</button>
            </div>
            <div className="absolute bottom-4 right-4 text-[11px] text-gray-400 bg-gray-900/70 rounded px-2 py-1">Drag paints · Right-drag orbits · Wheel zooms · Ctrl+Z / Ctrl+Y · Ctrl+P Commands</div>
          </div>
          <div className="border-t border-gray-800 bg-gray-950 p-3">
            <div className="flex flex-wrap items-center gap-2 mb-2">
              <button onClick={() => animationInput.current?.click()} className="px-3 py-1 rounded text-xs bg-gray-700 hover:bg-gray-600">Load animation SMD</button>
              <button onClick={() => { setAnimationFrames([]); setAnimPlaying(false); }} className="px-3 py-1 rounded text-xs bg-gray-700 hover:bg-gray-600">Clear animation</button>
              <span className="text-[11px] text-gray-500">{animationFrames.length} frames · live weight-driven deformation preview</span>
            </div>
            <input ref={animationInput} type="file" accept=".smd" className="hidden" onChange={(event) => { const file = event.target.files?.[0]; if (file) void handleAnimationFile(file); event.target.value = ""; }} />
            <AnimationTimeline
              state={{ ...player.state, isPlaying: animPlaying }}
              onTogglePlay={() => setAnimPlaying((value) => !value)}
              onStop={() => { setAnimPlaying(false); player.stop(); }}
              onSeek={player.seekToFrame}
              onSpeedChange={player.setSpeed}
              onToggleLoop={player.toggleLoop}
            />
          </div>
        </main>

        <aside className="w-80 border-l border-gray-800 bg-gray-950 flex flex-col overflow-auto p-4">
          <h2 className="text-sm font-medium mb-3 border-b border-gray-800 pb-2">Workflow</h2>
          <WeightHealth />
          <div className="mb-3 border border-gray-800 rounded p-3">
            <h3 className="text-xs uppercase text-gray-400 mb-2">Symmetry</h3>
            <div className="flex gap-2 mb-2">
              <button onClick={() => useRiggingStore.getState().setSymmetryEnabled(true)} className={`flex-1 px-2 py-1 rounded text-xs ${symmetryEnabled ? "bg-green-600 text-white" : "bg-gray-700 text-gray-300"}`}>On</button>
              <button onClick={() => useRiggingStore.getState().setSymmetryEnabled(false)} className={`flex-1 px-2 py-1 rounded text-xs ${!symmetryEnabled ? "bg-green-600 text-white" : "bg-gray-700 text-gray-300"}`}>Off</button>
            </div>
            <div className="flex gap-2">
              {(["x", "y", "z"] as const).map((axis) => (
                <button key={axis} onClick={() => useRiggingStore.getState().setSymmetryAxis(axis)} className={`flex-1 px-2 py-1 rounded text-xs uppercase ${symmetryAxis === axis ? "bg-green-600 text-white" : "bg-gray-700 text-gray-300"}`}>{axis}</button>
              ))}
            </div>
          </div>

          <div className="mb-3 border border-gray-800 rounded p-3">
            <h3 className="text-xs uppercase text-gray-400 mb-2">AI reference transfer</h3>
            <button onClick={() => referenceInput.current?.click()} className="w-full px-3 py-2 rounded text-xs bg-blue-600 hover:bg-blue-500">Upload reference SMD</button>
            <input ref={referenceInput} type="file" accept=".smd" className="hidden" onChange={(event) => { const file = event.target.files?.[0]; if (file) void handleReferenceFile(file); event.target.value = ""; }} />
            <p className="text-[11px] text-gray-500 mt-2">Uses backend KD-tree transfer and Metin2 weight limits.</p>
          </div>

          <div className="mb-3 border border-gray-800 rounded p-3 space-y-2">
            <h3 className="text-xs uppercase text-gray-400">Export</h3>
            <label className="block text-[11px] text-gray-400">Client model path</label>
            <input value={modelPath} onChange={(event) => setModelPath(event.target.value)} className="w-full bg-gray-900 border border-gray-700 rounded px-2 py-1 text-xs" />
            <label className="block text-[11px] text-gray-400">Textures, one per line</label>
            <textarea value={textureText} onChange={(event) => setTextureText(event.target.value)} rows={4} className="w-full bg-gray-900 border border-gray-700 rounded px-2 py-1 text-xs" />
            <button onClick={handleExportSmd} className="w-full px-3 py-2 rounded text-xs bg-emerald-600 hover:bg-emerald-500">Export SMD</button>
            <button onClick={handleExportMsm} className="w-full px-3 py-2 rounded text-xs bg-emerald-600 hover:bg-emerald-500">Export MSM</button>
            <button onClick={() => void handleCompileGr2()} className="w-full px-3 py-2 rounded text-xs bg-amber-600 hover:bg-amber-500">Request GR2 compile</button>
            {validation && (
              <div className="text-[11px] space-y-1">
                <div className={validation.valid ? "text-emerald-300" : "text-red-300"}>{validation.valid ? "Validation passed" : "Validation blocked export"}</div>
                {validation.errors.map((item) => <div key={item} className="text-red-300">Error: {item}</div>)}
                {validation.warnings.map((item) => <div key={item} className="text-amber-300">Warning: {item}</div>)}
                {validation.corrections.map((item) => <div key={item} className="text-emerald-300">Fixed: {item}</div>)}
              </div>
            )}
          </div>

          <div className="mb-3 border border-gray-800 rounded p-3">
            <h3 className="text-xs uppercase text-gray-400 mb-2">Local projects</h3>
            <button onClick={() => void saveProjectSnapshot()} className="w-full px-3 py-2 rounded text-xs bg-gray-700 hover:bg-gray-600 mb-2">Save snapshot</button>
            {projects.length === 0 && <p className="text-[11px] text-gray-500">No snapshots yet.</p>}
            {projects.map((project) => (
              <button key={project.id} onClick={() => void restoreProject(project.id)} className={`w-full text-left px-2 py-1 rounded text-xs mb-1 ${activeProjectId === project.id ? "bg-blue-800" : "bg-gray-900 hover:bg-gray-800"}`}>
                {project.name} · {new Date(project.updatedAt).toLocaleString()}
              </button>
            ))}
          </div>

          <div className="mt-auto border-t border-gray-800 pt-3">
            <h3 className="text-xs uppercase text-gray-400 mb-2">Log console</h3>
            <div className="h-36 overflow-y-auto space-y-1 text-[11px]">
              {logs.map((entry) => (
                <div key={entry.id} className={entry.level === "error" ? "text-red-300" : entry.level === "success" ? "text-emerald-300" : entry.level === "warning" ? "text-amber-300" : "text-gray-400"}>
                  [{entry.timestamp}] {entry.message}
                </div>
              ))}
            </div>
          </div>
        </aside>
      </div>

      {error && <div className="fixed top-4 left-1/2 -translate-x-1/2 bg-red-600 text-white px-4 py-2 rounded text-sm shadow">{error}</div>}
      {isProcessing && (
        <div className="fixed inset-0 bg-black/50 backdrop-blur flex items-center justify-center z-50">
          <div className="bg-gray-900 px-8 py-6 rounded border border-gray-700 text-sm">Processing request…</div>
        </div>
      )}
      <StatusBar />
      <ToastContainer />
      <CommandPalette open={commandPaletteOpen} onClose={() => setCommandPaletteOpen(false)} commands={commands} />
    </div>
  );
};
