import type { WeightVertex } from '@/stores/useRiggingStore'

export interface WeightHealthIssue {
  severity: 'info' | 'warning' | 'error'
  description: string
}

export interface WeightHealthReport {
  totalVertices: number
  weightedVertices: number
  zeroWeightVertices: number
  invalidWeightVertices: number
  maxInfluences: number
  avgInfluences: number
  boneCoverage: number
  issues: WeightHealthIssue[]
}

export function calculateWeightHealth(vertices: WeightVertex[], bones: string[]): WeightHealthReport {
  const totalVertices = vertices.length
  let weightedVertices = 0
  let zeroWeightVertices = 0
  let invalidWeightVertices = 0
  let maxInfluences = 0
  let totalInfluences = 0
  const bonesUsed = new Set<string>()
  const issues: WeightHealthIssue[] = []

  for (const vertex of vertices) {
    const totalWeight = vertex.weights.reduce((sum, w) => sum + w.weight, 0)
    const influenceCount = vertex.weights.filter(w => w.weight > 0.0001).length

    if (totalWeight < 0.001) {
      zeroWeightVertices++
    } else {
      weightedVertices++
    }

    if (totalWeight > 0 && Math.abs(totalWeight - 1.0) > 0.01) {
      invalidWeightVertices++
    }

    if (vertex.weights.length > 4) {
      invalidWeightVertices++
    }

    maxInfluences = Math.max(maxInfluences, influenceCount)
    totalInfluences += influenceCount

    vertex.weights.forEach(w => {
      if (w.weight > 0.0001) bonesUsed.add(w.boneId)
    })
  }

  const avgInfluences = totalVertices > 0 ? totalInfluences / totalVertices : 0
  const boneCoverage = bones.length > 0 ? (bonesUsed.size / bones.length) * 100 : 0

  if (zeroWeightVertices > 0) {
    issues.push({ severity: 'error', description: `${zeroWeightVertices} vertices have zero weight` })
  }
  if (invalidWeightVertices > 0) {
    issues.push({ severity: 'warning', description: `${invalidWeightVertices} vertices have invalid weights` })
  }
  if (bones.length > 0 && bonesUsed.size < bones.length) {
    issues.push({ severity: 'info', description: `${bones.length - bonesUsed.size} bones have no vertex weights` })
  }

  return {
    totalVertices,
    weightedVertices,
    zeroWeightVertices,
    invalidWeightVertices,
    maxInfluences,
    avgInfluences,
    boneCoverage,
    issues
  }
}
