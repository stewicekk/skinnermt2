import { useState, useEffect, useCallback } from "react";
import type { SMDBone } from "@/lib/smdExporter";

export interface RiggingPreset {
  id: string;
  name: string;
  boneNames: string[];
  smdBones: SMDBone[];
  weightMapping: Record<string, string>;
  createdAt: string;
}

const STORAGE_KEY = "rigging-presets";

function loadPresets(): RiggingPreset[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    return raw ? (JSON.parse(raw) as RiggingPreset[]) : [];
  } catch {
    return [];
  }
}

function persistPresets(presets: RiggingPreset[]) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(presets));
}

export interface PresetLibraryProps {
  boneNames: string[];
  smdBones: SMDBone[];
  onLoadPreset: (preset: RiggingPreset) => void;
  notify: (type: "success" | "error" | "warning" | "info", title: string, message: string) => void;
}

export const PresetLibrary: React.FC<PresetLibraryProps> = ({ boneNames, smdBones, onLoadPreset, notify }) => {
  const [presets, setPresets] = useState<RiggingPreset[]>([]);
  const [name, setName] = useState("");

  useEffect(() => {
    setPresets(loadPresets());
  }, []);

  const handleSave = useCallback(() => {
    const trimmed = name.trim();
    if (!trimmed) return;
    if (smdBones.length === 0) {
      notify("warning", "No skeleton", "Load a model before saving a preset.");
      return;
    }
    const preset: RiggingPreset = {
      id: `preset_${Date.now()}`,
      name: trimmed,
      boneNames: [...boneNames],
      smdBones: smdBones.map((b) => ({
        ...b,
        position: [...b.position] as [number, number, number],
        rotation: [...b.rotation] as [number, number, number],
      })),
      weightMapping: {},
      createdAt: new Date().toISOString(),
    };
    const updated = [preset, ...presets];
    persistPresets(updated);
    setPresets(updated);
    setName("");
    notify("success", "Preset saved", `"${trimmed}" with ${preset.smdBones.length} bones stored.`);
  }, [name, boneNames, smdBones, presets, notify]);

  const handleDelete = useCallback((id: string) => {
    const updated = presets.filter((p) => p.id !== id);
    persistPresets(updated);
    setPresets(updated);
  }, [presets]);

  return (
    <div className="border border-gray-800 rounded p-3">
      <h3 className="text-xs uppercase text-gray-400 mb-2">Preset Library</h3>
      <div className="flex gap-1 mb-2">
        <input
          placeholder="Preset name…"
          value={name}
          onChange={(e) => setName(e.target.value)}
          onKeyDown={(e) => e.key === "Enter" && handleSave()}
          className="flex-1 bg-gray-900 border border-gray-700 rounded px-2 py-1 text-xs text-white"
        />
        <button onClick={handleSave} className="px-2 py-1 rounded text-xs bg-emerald-600 hover:bg-emerald-500">Save</button>
      </div>
      {presets.length === 0 ? (
        <p className="text-[11px] text-gray-500">No presets saved yet. Save your current skeleton + weight config to reuse it.</p>
      ) : (
        <div className="space-y-1 max-h-48 overflow-y-auto">
          {presets.map((preset) => (
            <div key={preset.id} className="flex items-center gap-1">
              <button
                onClick={() => onLoadPreset(preset)}
                className="flex-1 text-left px-2 py-1 rounded text-xs bg-gray-900 hover:bg-gray-800 text-gray-300"
              >
                {preset.name}
                <span className="text-gray-500 ml-1">· {preset.smdBones.length} bones</span>
              </button>
              <button
                onClick={() => handleDelete(preset.id)}
                className="px-1.5 py-1 rounded text-xs bg-gray-800 hover:bg-red-900 text-gray-400 hover:text-red-300"
              >
                ✕
              </button>
            </div>
          ))}
        </div>
      )}
    </div>
  );
};
