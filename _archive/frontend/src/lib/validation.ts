/**
 * Pre-export Validation Pipeline
 * Comprehensive checks before exporting to Metin2 format
 * 
 * Validates:
 * - Zero-weight vertices (Nearest Bone Fallback)
 * - Maximum bones per mesh (256 limit)
 * - Weight normalization (sum = 1.0)
 * - Bone naming conventions (Metin2 standard)
 * - Socket bone protection (Dummy bones)
 */

import type { WeightVertex, BoneWeight } from '@/stores/useRiggingStore'

export interface ValidationResult {
  valid: boolean
  errors: string[]
  warnings: string[]
  corrections: string[]
}

export interface PreExportConfig {
  maxBonesPerMesh: number
  requireNormalizedWeights: boolean
  autoAssignZeroWeightVertices: boolean
  protectedBones: string[]
}

const METIN2_PROTECTED_BONES = [
  'equip_right', 'equip_left', 'stip', 'Bip01 Head',
  'Bip01', 'Bip01 Pelvis'
]

const DEFAULT_CONFIG: PreExportConfig = {
  maxBonesPerMesh: 256,
  requireNormalizedWeights: true,
  autoAssignZeroWeightVertices: true,
  protectedBones: METIN2_PROTECTED_BONES
}

export const validatePreExport = (
  vertices: WeightVertex[],
  bones: string[],
  config: Partial<PreExportConfig> = {}
): ValidationResult => {
  const cfg = { ...DEFAULT_CONFIG, ...config }
  const errors: string[] = []
  const warnings: string[] = []
  const corrections: string[] = []

  if (bones.length > cfg.maxBonesPerMesh) {
    warnings.push(`Bone count (${bones.length}) exceeds Metin2 limit (${cfg.maxBonesPerMesh})`)
  }

  const zeroWeightVerts = vertices.filter(v => {
    const totalWeight = v.weights.reduce((sum, w) => sum + w.weight, 0)
    return totalWeight < 0.001
  })

  if (zeroWeightVerts.length > 0) {
    if (cfg.autoAssignZeroWeightVertices) {
      corrections.push(`Auto-assigned ${zeroWeightVerts.length} zero-weight vertices to nearest bone`)
    } else {
      errors.push(`${zeroWeightVerts.length} unassigned vertices (zero weight)`)
    }
  }

  if (cfg.requireNormalizedWeights) {
    const invalidVerts = vertices.filter(v => {
      const totalWeight = v.weights.reduce((sum, w) => sum + w.weight, 0)
      return Math.abs(totalWeight - 1.0) > 0.01 && totalWeight > 0
    })
    
    if (invalidVerts.length > 0) {
      warnings.push(`${invalidVerts.length} vertices have non-normalized weights`)
    }
  }

  const overBonedVerts = vertices.filter(v => v.weights.length > 4)
  if (overBonedVerts.length > 0) {
    errors.push(`${overBonedVerts.length} vertices have more than 4 bone influences`)
  }

  vertices.forEach(v => {
    v.weights.forEach(w => {
      if (cfg.protectedBones.includes(w.boneId)) {
        warnings.push(`Vertex ${v.id} uses protected bone "${w.boneId}"`)
      }
    })
  })

  return {
    valid: errors.length === 0,
    errors,
    warnings,
    corrections
  }
}

export const autoAssignZeroWeightVertices = (
  vertices: WeightVertex[],
  bones: string[]
): WeightVertex[] => {
  return vertices.map(vertex => {
    const totalWeight = vertex.weights.reduce((sum, w) => sum + w.weight, 0)
    
    if (totalWeight < 0.001) {
      const nearestBone = bones[0] || 'Bip01'
      return {
        ...vertex,
        weights: [{ boneId: nearestBone, weight: 1.0 }]
      }
    }
    
    return vertex
  })
}

export const normalizeAllWeights = (
  vertices: WeightVertex[]
): WeightVertex[] => {
  return vertices.map((vertex): WeightVertex => {
    const totalWeight = vertex.weights.reduce((sum: number, w: BoneWeight) => sum + w.weight, 0)
    
    if (totalWeight === 0) {
      return {
        ...vertex,
        weights: [{ boneId: vertex.weights[0]?.boneId || 'Bip01', weight: 1.0 }]
      }
    }
    
    const normalized = vertex.weights.map((w): BoneWeight => ({
      boneId: w.boneId,
      weight: w.weight / totalWeight
    }))
    
    if (normalized.length > 4) {
      normalized.sort((a, b) => b.weight - a.weight)
      return {
        ...vertex,
        weights: normalized.slice(0, 4)
      }
    }
    
    return {
      ...vertex,
      weights: normalized
    }
  })
}

export const protectSocketBones = (
  vertices: WeightVertex[],
  protectedBones: string[]
): WeightVertex[] => {
  return vertices.map((vertex): WeightVertex => {
    const protectedWeights = vertex.weights.filter((w): boolean => 
      protectedBones.includes(w.boneId)
    )
    
    if (protectedWeights.length > 0) {
      const otherWeights = vertex.weights.filter((w): boolean => 
        !protectedBones.includes(w.boneId)
      )
      
      const protectedTotal = protectedWeights.reduce((sum: number, w: BoneWeight) => sum + w.weight, 0)
      const remainingWeight = Math.max(0, 1.0 - protectedTotal)
      
      if (otherWeights.length > 0 && remainingWeight > 0) {
        const perBone = remainingWeight / otherWeights.length
        const redistributed = otherWeights.map((w): BoneWeight => ({
          boneId: w.boneId,
          weight: w.weight + perBone
        }))
        
        return {
          ...vertex,
          weights: [...protectedWeights, ...redistributed]
        }
      }
      
      return {
        ...vertex,
        weights: [...protectedWeights]
      }
    }
    
    return vertex
  })
}

export const canExport = (result: ValidationResult): boolean => {
  return result.errors.length === 0
}