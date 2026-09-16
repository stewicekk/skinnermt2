import { useMemo } from "react";
import type { ValidationResult } from "@/lib/validation";

export interface ValidationCenterProps {
  validation: ValidationResult | null;
  boneCount: number;
  vertexCount: number;
  onRunValidation: () => void;
  onNormalize: () => void;
  onAutoAssign: () => void;
  onPruneInfluences: () => void;
  onProtectSockets: () => void;
  onAutoRepairAll: () => void;
}

function StatusBadge({ valid }: { valid: boolean }) {
  return (
    <span
      className={`inline-flex items-center gap-1 px-2 py-0.5 rounded text-[11px] font-medium ${
        valid ? "bg-emerald-900 text-emerald-300" : "bg-red-900 text-red-300"
      }`}
    >
      <span className={`w-1.5 h-1.5 rounded-full ${valid ? "bg-emerald-400" : "bg-red-400"}`} />
      {valid ? "PASS" : "FAIL"}
    </span>
  );
}

function SectionList({
  label,
  items,
  color
}: {
  label: string;
  items: string[];
  color: string;
}) {
  if (items.length === 0) return null;
  return (
    <div>
      <div className={`text-[11px] font-medium ${color} mb-1`}>{label} ({items.length})</div>
      <ul className="space-y-0.5 mb-2">
        {items.map((item, i) => (
          <li key={i} className={`text-[11px] ${color} leading-tight`}>
            {item}
          </li>
        ))}
      </ul>
    </div>
  );
}

export const ValidationCenter: React.FC<ValidationCenterProps> = ({
  validation,
  boneCount,
  vertexCount,
  onRunValidation,
  onNormalize,
  onAutoAssign,
  onPruneInfluences,
  onProtectSockets,
  onAutoRepairAll
}) => {
  const overInfluenceCount = useMemo(() => {
    if (!validation) return 0;
    const match = validation.errors.find((e) => e.includes("more than 4 bone influences"));
    return match ? parseInt(match.match(/\d+/)?.[0] ?? "0", 10) : 0;
  }, [validation]);

  return (
    <div className="mb-3 border border-gray-800 rounded p-3">
      <div className="flex items-center justify-between mb-2">
        <h3 className="text-xs uppercase text-gray-400">Validation Center</h3>
        {validation && <StatusBadge valid={validation.valid} />}
      </div>

      {/* Quick stats */}
      <div className="flex gap-3 text-[11px] text-gray-400 mb-3">
        <span>{vertexCount} verts</span>
        <span>{boneCount} bones</span>
        <span>Limit: 256 bones / 4 infl</span>
      </div>

      {/* Run validation */}
      <button
        onClick={onRunValidation}
        className="w-full px-3 py-1.5 rounded text-xs bg-gray-700 hover:bg-gray-600 mb-2"
      >
        Run Validation
      </button>

      {/* Auto-repair buttons */}
      <div className="grid grid-cols-2 gap-1.5 mb-2">
        <button
          onClick={onAutoRepairAll}
          className="col-span-2 px-2 py-1.5 rounded text-xs bg-emerald-700 hover:bg-emerald-600 font-medium"
        >
          Auto-Repair All
        </button>
        <button onClick={onNormalize} className="px-2 py-1 rounded text-xs bg-gray-800 hover:bg-gray-700 text-gray-300">
          Normalize
        </button>
        <button onClick={onAutoAssign} className="px-2 py-1 rounded text-xs bg-gray-800 hover:bg-gray-700 text-gray-300">
          Assign Zero-W
        </button>
        <button
          onClick={onPruneInfluences}
          disabled={overInfluenceCount === 0}
          className="px-2 py-1 rounded text-xs bg-gray-800 hover:bg-gray-700 text-gray-300 disabled:opacity-40 disabled:cursor-not-allowed"
        >
          Prune &gt;4 Infl
        </button>
        <button onClick={onProtectSockets} className="px-2 py-1 rounded text-xs bg-gray-800 hover:bg-gray-700 text-gray-300">
          Protect Sockets
        </button>
      </div>

      {/* Results */}
      {validation ? (
        <div className="border-t border-gray-800 pt-2">
          <SectionList label="Errors" items={validation.errors} color="text-red-400" />
          <SectionList label="Warnings" items={validation.warnings} color="text-amber-400" />
          <SectionList label="Auto-Fixed" items={validation.corrections} color="text-emerald-400" />
          {validation.valid && validation.errors.length === 0 && validation.warnings.length === 0 && validation.corrections.length === 0 && (
            <div className="text-[11px] text-emerald-400">All checks passed — ready for export.</div>
          )}
        </div>
      ) : (
        <div className="text-[11px] text-gray-500 border-t border-gray-800 pt-2">
          Run validation to check Metin2 export readiness.
        </div>
      )}
    </div>
  );
};
