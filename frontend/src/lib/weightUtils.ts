/**
 * Weight Normalizer Utility for Metin2 Rigging
 * Ensures: max 4 bones per vertex, sum of weights = 1.0
 * 
 * @param vertices - Array of WeightVertex to normalize
 * @returns Object with { success: boolean, modifiedVertices: WeightVertex[] }
 */
import type { WeightVertex, BoneWeight } from '@/stores/useRiggingStore'
export const normalizeVertexWeights = (
  vertices: WeightVertex[]
): { success: boolean; modifiedVertices: WeightVertex[] } => {
  const modified = vertices.map((vertex) => {
    const { weights, ...rest } = vertex
    
    let wts = [...weights]

    // Step 1: Clamp to max 4 bones - keep top 4 by weight
    if (wts.length > 4) {
      wts.sort((a, b) => b.weight - a.weight)
      wts = wts.slice(0, 4)
    }

    // Step 2: Calculate total weight
    let totalWeight = wts.reduce((sum, w) => sum + w.weight, 0)

    // Step 3: Handle zero-weight vertices
    if (totalWeight === 0) {
      // Assign to bone with highest existing weight (or first if all zero)
      const fallbackBone = wts.length > 0 ? wts[0].boneId : 'Bip01'
      return {
        ...rest,
        weights: [{ boneId: fallbackBone, weight: 1.0 }, ...wts.slice(0, 3) ],
      }
    }

    // Step 4: Normalize to sum to 1.0
    if (Math.abs(totalWeight - 1.0) > 0.001) {
      wts = wts.map((w) => ({
        boneId: w.boneId,
        weight: w.weight / totalWeight,
      }))
    }

    // Step 5: Ensure we have exactly the right structure
    // Pad with zeros if less than 4 bones
    while (wts.length < 4) {
      wts.push({ boneId: '', weight: 0 })
    }

    return {
      ...rest,
      weights: wts.slice(0, 4),
    }
  })

  const hasChanges = vertices.some(
    (v, i) => JSON.stringify(modified[i].weights) !== JSON.stringify(v.weights)
  )

  return {
    success: hasChanges,
    modifiedVertices: modified,
  }
}

/**
 * Add weight using falloff brush
 * Applies smooth falloff based on distance from brush center
 * 
 * @param vertexPos - Position of the vertex in bone space
 * @param brushCenter - Center of the brush in bone space
 * @param strength - Brush strength (0.0 to 1.0)
 * @param mode - 'add', 'subtract', 'smooth', 'normalize'
 * @returns New weight value for this vertex
 */
export const applyBrushFalloff = (
  vertexPos: [number, number, number],
  brushCenter: [number, number, number],
  strength: number,
  mode: 'add' | 'subtract' | 'smooth' | 'normalize'
): number => {
  // Euclidean distance
  const dx = vertexPos[0] - brushCenter[0]
  const dy = vertexPos[1] - brushCenter[1]
  const dz = vertexPos[2] - brushCenter[2]
  const distance = Math.sqrt(dx * dx + dy * dy + dz * dz)

  // Gaussian falloff
  const falloff = Math.exp(-(distance * distance) / (2 * 1.0 * 1.0))
  
  let delta = falloff * strength

  switch (mode) {
    case 'add':
      return delta
    case 'subtract':
      return -delta
    case 'smooth':
      // Smooth blends towards the brush target
      return delta * 0.5 // Reduced for smoothing effect
    case 'normalize':
      return 0 // Normalization happens separately
    default:
      return 0
  }
}

/**
 * Check if all vertices are fully skinned (sum of weights = 1.0)
 * 
 * @param vertices - Array of WeightVertex to check
 * @returns { boolean } - True if all vertices are valid
 */
export const checkAllVerticesValid = (
  vertices: WeightVertex[]
): { valid: boolean; invalidCount: number } => {
  let invalidCount = 0

  for (const vertex of vertices) {
    const total = vertex.weights.reduce((sum, w) => sum + w.weight, 0)
    if (Math.abs(total - 1.0) > 0.001 || vertex.weights.length > 4) {
      invalidCount++
    }
  }

  return {
    valid: invalidCount === 0,
    invalidCount,
  }
}