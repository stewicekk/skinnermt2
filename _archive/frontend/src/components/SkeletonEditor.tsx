import { useState, useCallback, useMemo } from "react";
import type { Bone } from "@/stores/useRiggingStore";

export interface SkeletonEditorProps {
  bones: Bone[];
  selectedBone: string | null;
  onSelectBone: (boneId: string) => void;
  onRenameBone: (oldId: string, newName: string) => void;
  onReparentBone: (boneId: string, newParentId: string | null) => void;
  onAddBone: (name: string, parentId: string | null) => void;
  onRemoveBone: (boneId: string) => void;
  onUpdateTransform: (boneId: string, position: [number, number, number], rotation: [number, number, number]) => void;
}

// Metin2 standard bone naming presets
const METIN2_PRESETS: Record<string, string[]> = {
  warrior: [
    "Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1", "Bip01 Neck", "Bip01 Head",
    "Bip01 L Clavicle", "Bip01 R Clavicle", "Bip01 L UpperArm", "Bip01 R UpperArm",
    "Bip01 L Forearm", "Bip01 R Forearm", "Bip01 L Hand", "Bip01 R Hand",
    "Bip01 L Thigh", "Bip01 R Thigh", "Bip01 L Calf", "Bip01 R Calf",
    "Bip01 L Foot", "Bip01 R Foot", "equip_left", "equip_right", "stip"
  ],
  ninja: [
    "Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1", "Bip01 Neck", "Bip01 Head",
    "Bip01 L Clavicle", "Bip01 R Clavicle", "Bip01 L UpperArm", "Bip01 R UpperArm",
    "Bip01 L Forearm", "Bip01 R Forearm", "Bip01 L Hand", "Bip01 R Hand",
    "Bip01 L Thigh", "Bip01 R Thigh", "Bip01 L Calf", "Bip01 R Calf",
    "Bip01 L Foot", "Bip01 R Foot", "equip_left", "equip_right", "stip"
  ],
  sura: [
    "Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1", "Bip01 Spine2", "Bip01 Neck", "Bip01 Head",
    "Bip01 L Clavicle", "Bip01 R Clavicle", "Bip01 L UpperArm", "Bip01 R UpperArm",
    "Bip01 L Forearm", "Bip01 R Forearm", "Bip01 L Hand", "Bip01 R Hand",
    "Bip01 L Thigh", "Bip01 R Thigh", "Bip01 L Calf", "Bip01 R Calf",
    "Bip01 L Foot", "Bip01 R Foot", "equip_left", "equip_right", "stip"
  ],
  shaman: [
    "Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1", "Bip01 Neck", "Bip01 Head",
    "Bip01 L Clavicle", "Bip01 R Clavicle", "Bip01 L UpperArm", "Bip01 R UpperArm",
    "Bip01 L Forearm", "Bip01 R Forearm", "Bip01 L Hand", "Bip01 R Hand",
    "Bip01 L Thigh", "Bip01 R Thigh", "Bip01 L Calf", "Bip01 R Calf",
    "Bip01 L Foot", "Bip01 R Foot", "equip_left", "equip_right", "stip"
  ],
  wolfman: [
    "Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1", "Bip01 Neck", "Bip01 Head",
    "Bip01 L Clavicle", "Bip01 R Clavicle", "Bip01 L UpperArm", "Bip01 R UpperArm",
    "Bip01 L Forearm", "Bip01 R Forearm", "Bip01 L Hand", "Bip01 R Hand",
    "Bip01 L Thigh", "Bip01 R Thigh", "Bip01 L Calf", "Bip01 R Calf",
    "Bip01 L Foot", "Bip01 R Foot", "equip_left", "equip_right", "stip"
  ]
};

type Tab = "tree" | "transform" | "mapping";

export const SkeletonEditor: React.FC<SkeletonEditorProps> = ({
  bones,
  selectedBone,
  onSelectBone,
  onRenameBone,
  onReparentBone,
  onAddBone,
  onRemoveBone,
  onUpdateTransform
}) => {
  const [tab, setTab] = useState<Tab>("tree");
  const [expandedNodes, setExpandedNodes] = useState<Set<string>>(new Set());
  const [editingName, setEditingName] = useState<string | null>(null);
  const [newBoneName, setNewBoneName] = useState("");
  const [newBoneParent, setNewBoneParent] = useState<string | null>(null);
  const [showAddForm, setShowAddForm] = useState(false);
  const [presetClass, setPresetClass] = useState<string>("warrior");

  const boneMap = useMemo(() => new Map(bones.map(b => [b.id, b])), [bones]);
  const selectedBoneData = useMemo(() => bones.find(b => b.id === selectedBone) ?? null, [bones, selectedBone]);

  const toggleNode = useCallback((boneId: string) => {
    setExpandedNodes(prev => {
      const next = new Set(prev);
      if (next.has(boneId)) next.delete(boneId);
      else next.add(boneId);
      return next;
    });
  }, []);

  const buildTree = useCallback((boneList: Bone[]): Bone[] => {
    const map = new Map(boneList.map(b => [b.id, { ...b, children: [] as string[] }]));
    const roots: Bone[] = [];
    boneList.forEach(bone => {
      if (bone.parent && map.has(bone.parent)) {
        map.get(bone.parent)!.children!.push(bone.id);
      } else {
        roots.push(map.get(bone.id)!);
      }
    });
    return roots;
  }, []);

  const renderBoneNode = (bone: Bone, depth: number): JSX.Element => {
    const isExpanded = expandedNodes.has(bone.id);
    const isSelected = selectedBone === bone.id;
    const isEditing = editingName === bone.id;
    const hasChildren = bone.children && bone.children.length > 0;
    const pad = { paddingLeft: `${depth * 16 + 4}px` };

    return (
      <div key={bone.id}>
        <div
          className={`flex items-center gap-1 px-1 py-1 rounded cursor-pointer text-xs transition-colors ${
            isSelected ? "bg-blue-600 text-white" : "text-gray-300 hover:bg-gray-700"
          }`}
          style={pad}
          onClick={() => onSelectBone(bone.id)}
          onDoubleClick={() => setEditingName(bone.id)}
        >
          {hasChildren ? (
            <button
              onClick={(e) => { e.stopPropagation(); toggleNode(bone.id); }}
              className="w-3 h-3 flex items-center justify-center text-gray-400 hover:text-white text-[10px]"
            >
              {isExpanded ? "▼" : "▶"}
            </button>
          ) : (
            <span className="w-3" />
          )}

          {isEditing ? (
            <input
              autoFocus
              defaultValue={bone.name}
              className="flex-1 bg-gray-900 border border-gray-600 rounded px-1 text-xs text-white"
              onClick={(e) => e.stopPropagation()}
              onBlur={(e) => {
                const newName = e.target.value.trim();
                if (newName && newName !== bone.name) onRenameBone(bone.id, newName);
                setEditingName(null);
              }}
              onKeyDown={(e) => {
                if (e.key === "Enter") (e.target as HTMLInputElement).blur();
                if (e.key === "Escape") setEditingName(null);
              }}
            />
          ) : (
            <span className="flex-1 truncate">{bone.name}</span>
          )}

          <button
            onClick={(e) => { e.stopPropagation(); onRemoveBone(bone.id); }}
            className="w-4 h-4 flex items-center justify-center text-gray-500 hover:text-red-400 text-[10px]"
            title="Remove bone"
          >
            ✕
          </button>
        </div>

        {hasChildren && isExpanded && (
          bone.children!.map(childId => {
            const child = boneMap.get(childId);
            return child ? renderBoneNode(child, depth + 1) : null;
          })
        )}
      </div>
    );
  };

  const treeRoots = buildTree(bones);

  // Transform tab
  const renderTransformTab = () => {
    if (!selectedBoneData) {
      return <div className="text-[11px] text-gray-500 py-4 text-center">Select a bone to edit its transform</div>;
    }
    const t = selectedBoneData.transform;
    return (
      <div className="space-y-3">
        <div className="text-xs text-gray-400">
          Editing: <span className="text-white font-medium">{selectedBoneData.name}</span>
        </div>
        <div>
          <label className="block text-[11px] text-gray-400 mb-1">Position (X / Y / Z)</label>
          <div className="grid grid-cols-3 gap-1">
            {([0, 1, 2] as const).map(i => (
              <input
                key={i}
                type="number"
                step="0.01"
                value={t.position[i].toFixed(3)}
                onChange={(e) => {
                  const pos = [...t.position] as [number, number, number];
                  pos[i] = parseFloat(e.target.value) || 0;
                  onUpdateTransform(selectedBoneData.id, pos, t.rotation);
                }}
                className="bg-gray-900 border border-gray-700 rounded px-1 py-1 text-xs text-white"
              />
            ))}
          </div>
        </div>
        <div>
          <label className="block text-[11px] text-gray-400 mb-1">Rotation (X / Y / Z)</label>
          <div className="grid grid-cols-3 gap-1">
            {([0, 1, 2] as const).map(i => (
              <input
                key={i}
                type="number"
                step="0.01"
                value={t.rotation[i].toFixed(3)}
                onChange={(e) => {
                  const rot = [...t.rotation] as [number, number, number];
                  rot[i] = parseFloat(e.target.value) || 0;
                  onUpdateTransform(selectedBoneData.id, t.position, rot);
                }}
                className="bg-gray-900 border border-gray-700 rounded px-1 py-1 text-xs text-white"
              />
            ))}
          </div>
        </div>
        <div>
          <label className="block text-[11px] text-gray-400 mb-1">Parent</label>
          <select
            value={selectedBoneData.parent ?? ""}
            onChange={(e) => onReparentBone(selectedBoneData.id, e.target.value || null)}
            className="w-full bg-gray-900 border border-gray-700 rounded px-2 py-1 text-xs text-white"
          >
            <option value="">(root — no parent)</option>
            {bones.filter(b => b.id !== selectedBoneData.id).map(b => (
              <option key={b.id} value={b.id}>{b.name}</option>
            ))}
          </select>
        </div>
      </div>
    );
  };

  // Mapping tab — compare current bones to Metin2 preset
  const renderMappingTab = () => {
    const preset = METIN2_PRESETS[presetClass] ?? METIN2_PRESETS.warrior;
    const currentNames = new Set(bones.map(b => b.name));
    const missing = preset.filter(name => !currentNames.has(name));
    const extra = bones.filter(b => !preset.includes(b.name)).map(b => b.name);

    return (
      <div className="space-y-3">
        <div>
          <label className="block text-[11px] text-gray-400 mb-1">Metin2 Class Preset</label>
          <select
            value={presetClass}
            onChange={(e) => setPresetClass(e.target.value)}
            className="w-full bg-gray-900 border border-gray-700 rounded px-2 py-1 text-xs text-white"
          >
            {Object.keys(METIN2_PRESETS).map(cls => (
              <option key={cls} value={cls}>{cls}</option>
            ))}
          </select>
        </div>

        <div>
          <div className="text-[11px] text-gray-400 mb-1">Missing bones ({missing.length})</div>
          {missing.length === 0 ? (
            <div className="text-[11px] text-emerald-400">All preset bones present ✓</div>
          ) : (
            <div className="space-y-0.5 max-h-32 overflow-y-auto">
              {missing.map(name => (
                <div key={name} className="flex items-center justify-between text-[11px]">
                  <span className="text-amber-400">{name}</span>
                  <button
                    onClick={() => {
                      const parent = bones.find(b => name.startsWith("Bip01") && b.name === "Bip01");
                      onAddBone(name, parent?.id ?? null);
                    }}
                    className="px-1.5 py-0.5 rounded bg-gray-700 hover:bg-gray-600 text-[10px] text-gray-300"
                  >
                    Add
                  </button>
                </div>
              ))}
            </div>
          )}
        </div>

        <div>
          <div className="text-[11px] text-gray-400 mb-1">Extra bones ({extra.length})</div>
          {extra.length === 0 ? (
            <div className="text-[11px] text-gray-500">No extra bones</div>
          ) : (
            <div className="space-y-0.5 max-h-32 overflow-y-auto">
              {extra.map(name => (
                <div key={name} className="text-[11px] text-gray-400">{name}</div>
              ))}
            </div>
          )}
        </div>
      </div>
    );
  };

  return (
    <div className="border border-gray-800 rounded p-3">
      <div className="flex items-center justify-between mb-2">
        <h3 className="text-xs uppercase text-gray-400">Skeleton Editor</h3>
        <span className="text-[11px] text-gray-500">{bones.length} bones</span>
      </div>

      {/* Tab bar */}
      <div className="flex gap-1 mb-2">
        {(["tree", "transform", "mapping"] as const).map(t => (
          <button
            key={t}
            onClick={() => setTab(t)}
            className={`flex-1 px-2 py-1 rounded text-[11px] capitalize ${
              tab === t ? "bg-blue-600 text-white" : "bg-gray-800 text-gray-400 hover:bg-gray-700"
            }`}
          >
            {t}
          </button>
        ))}
      </div>

      {tab === "tree" && (
        <div>
          <div className="space-y-0.5 max-h-64 overflow-y-auto mb-2">
            {treeRoots.map(bone => renderBoneNode(bone, 0))}
          </div>

          {showAddForm ? (
            <div className="border border-gray-700 rounded p-2 space-y-1.5">
              <input
                placeholder="Bone name"
                value={newBoneName}
                onChange={(e) => setNewBoneName(e.target.value)}
                className="w-full bg-gray-900 border border-gray-700 rounded px-2 py-1 text-xs text-white"
              />
              <select
                value={newBoneParent ?? ""}
                onChange={(e) => setNewBoneParent(e.target.value || null)}
                className="w-full bg-gray-900 border border-gray-700 rounded px-2 py-1 text-xs text-white"
              >
                <option value="">(root)</option>
                {bones.map(b => (
                  <option key={b.id} value={b.id}>{b.name}</option>
                ))}
              </select>
              <div className="flex gap-1">
                <button
                  onClick={() => {
                    if (newBoneName.trim()) {
                      onAddBone(newBoneName.trim(), newBoneParent);
                      setNewBoneName("");
                      setNewBoneParent(null);
                      setShowAddForm(false);
                    }
                  }}
                  className="flex-1 px-2 py-1 rounded text-xs bg-emerald-600 hover:bg-emerald-500"
                >
                  Add
                </button>
                <button
                  onClick={() => setShowAddForm(false)}
                  className="flex-1 px-2 py-1 rounded text-xs bg-gray-700 hover:bg-gray-600"
                >
                  Cancel
                </button>
              </div>
            </div>
          ) : (
            <button
              onClick={() => setShowAddForm(true)}
              className="w-full px-2 py-1 rounded text-xs bg-gray-800 hover:bg-gray-700 text-gray-300"
            >
              + Add Bone
            </button>
          )}

          <p className="text-[10px] text-gray-500 mt-2">Double-click a bone to rename. Click ✕ to remove.</p>
        </div>
      )}

      {tab === "transform" && renderTransformTab()}
      {tab === "mapping" && renderMappingTab()}
    </div>
  );
};
