import { useMemo } from 'react'
import { useRiggingStore } from '@/stores/useRiggingStore'
import { calculateWeightHealth } from '@/lib/weightHealth'

export const WeightHealth: React.FC = () => {
  const currentMesh = useRiggingStore((s) => s.currentMesh)
  const meshData = useRiggingStore((s) => s.meshData)
  const boneLists = useRiggingStore((s) => s.boneLists)

  const vertices = currentMesh ? meshData[currentMesh] ?? [] : []
  const bones = currentMesh ? boneLists[currentMesh] ?? [] : []

  const health = useMemo(() => calculateWeightHealth(vertices, bones), [vertices, bones])

  return (
    <div className="mb-3 border border-gray-800 rounded p-3">
      <h3 className="text-xs uppercase text-gray-400 mb-2">Weight Health</h3>
      <div className="space-y-1 text-[11px]">
        <div className="flex justify-between">
          <span className="text-gray-400">Total vertices</span>
          <span className="text-gray-200">{health.totalVertices}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-gray-400">Weighted</span>
          <span className="text-green-400">{health.weightedVertices}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-gray-400">Zero weight</span>
          <span className={health.zeroWeightVertices > 0 ? 'text-red-400' : 'text-gray-200'}>{health.zeroWeightVertices}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-gray-400">Invalid</span>
          <span className={health.invalidWeightVertices > 0 ? 'text-amber-400' : 'text-gray-200'}>{health.invalidWeightVertices}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-gray-400">Max influences</span>
          <span className="text-gray-200">{health.maxInfluences}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-gray-400">Avg influences</span>
          <span className="text-gray-200">{health.avgInfluences.toFixed(1)}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-gray-400">Bone coverage</span>
          <span className="text-gray-200">{health.boneCoverage.toFixed(0)}%</span>
        </div>
      </div>
      {health.issues.length > 0 && (
        <div className="mt-2 pt-2 border-t border-gray-800 space-y-1">
          {health.issues.map((issue, i) => (
            <div key={i} className={`text-[11px] ${issue.severity === 'error' ? 'text-red-400' : issue.severity === 'warning' ? 'text-amber-400' : 'text-gray-400'}`}>
              {issue.description}
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
