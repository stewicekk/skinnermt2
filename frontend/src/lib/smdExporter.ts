/**
 * SMD Exporter Module for Metin2
 * Converts Three.js BufferGeometry to ASCII SMD (Half-Life skeletal format)
 * 
 * Handles: vertex positions, normals, bone indices, weights, triangle faces
 * Preserves material IDs for proper texture grouping in Metin2
 */

import type { WeightVertex, BoneWeight } from '@/stores/useRiggingStore'

export interface SMDTriangle {
  materialIndex: number
  vertexIndices: [number, number, number]
}

export interface SMDBone {
  id: number
  name: string
  parentId: number
  position: [number, number, number]
  rotation: [number, number, number]
}

export interface SMDFrame {
  time: number
  boneTransforms: Array<{
    boneId: number
    position: [number, number, number]
    rotation: [number, number, number]
  }>
}

export const exportToSMD = (
  vertices: WeightVertex[],
  triangles: SMDTriangle[],
  bones: SMDBone[],
  frames: SMDFrame[],
  materialMap?: Record<number, string>
): string => {
  const lines: string[] = []
  const boneNumbers = new Map(bones.map((bone) => [bone.name, bone.id]))

  lines.push('version 1')
  lines.push('')

  lines.push('nodes')
  bones.forEach((bone) => {
    lines.push(`  ${bone.id} "${bone.name}" ${bone.parentId}`)
  })
  lines.push('end')
  lines.push('')

  lines.push('skeleton')
  lines.push('time 0')
  bones.forEach((bone) => {
    lines.push(`  ${bone.id} ${bone.position[0].toFixed(6)} ${bone.position[1].toFixed(6)} ${bone.position[2].toFixed(6)} ${bone.rotation[0].toFixed(6)} ${bone.rotation[1].toFixed(6)} ${bone.rotation[2].toFixed(6)}`)
  })
  lines.push('end')
  lines.push('')

  const materialName = (materialIndex: number): string => materialMap?.[materialIndex] ?? `material_${materialIndex}.dds`
  lines.push('triangles')
  triangles.forEach((tri, triIdx) => {
    lines.push(materialName(tri.materialIndex))
    tri.vertexIndices.forEach((vertIdx) => {
      const vertex = vertices[vertIdx]
      if (!vertex) return
      const normal = calculateVertexNormal(vertex, vertices, triangles, triIdx)
      const links = vertex.weights.filter((entry) => entry.weight > 0.000001).slice(0, 4)
      const parent = links.length > 0 ? (boneNumbers.get(links[0].boneId) ?? 0) : 0
      const linkText = links.map((entry) => `${boneNumbers.get(entry.boneId) ?? 0} ${entry.weight.toFixed(6)}`).join(' ')
      lines.push(`${parent} ${vertex.position[0].toFixed(6)} ${vertex.position[1].toFixed(6)} ${vertex.position[2].toFixed(6)} ${normal[0].toFixed(6)} ${normal[1].toFixed(6)} ${normal[2].toFixed(6)} 0.000000 0.000000 ${links.length}${linkText ? ` ${linkText}` : ''}`)
    })
  })
  lines.push('end')

  if (frames.length > 0) {
    lines.push('')
    lines.push('frames')
    frames.forEach((frame, idx) => {
      lines.push(`time ${idx}`)
      frame.boneTransforms.forEach((transform) => {
        lines.push(`  ${transform.boneId} ${transform.position[0].toFixed(6)} ${transform.position[1].toFixed(6)} ${transform.position[2].toFixed(6)} ${transform.rotation[0].toFixed(6)} ${transform.rotation[1].toFixed(6)} ${transform.rotation[2].toFixed(6)}`)
      })
    })
    lines.push('end')
  }

  return lines.join('\n')
}

/**
 * Calculate vertex normal for SMD export
 * Handles hard/soft edges by averaging adjacent face normals
 */
const calculateVertexNormal = (
  vertex: WeightVertex,
  vertices: WeightVertex[],
  triangles: SMDTriangle[],
  currentTriIdx: number
): [number, number, number] => {
  // Find adjacent triangles sharing this vertex
  const adjacentTris = triangles.filter((_, idx) => {
    if (idx === currentTriIdx) return false
    return _.vertexIndices.includes(vertex.id)
  })

  // Accumulate normals from adjacent faces
  let normalX = 0, normalY = 0, normalZ = 0
  let count = 0

  adjacentTris.forEach((tri) => {
    const v0 = vertices[tri.vertexIndices[0]]
    const v1 = vertices[tri.vertexIndices[1]]
    const v2 = vertices[tri.vertexIndices[2]]

    if (v0 && v1 && v2) {
      const faceNormal = computeFaceNormal(
        v0.position, v1.position, v2.position
      )
      normalX += faceNormal[0]
      normalY += faceNormal[1]
      normalZ += faceNormal[2]
      count++
    }
  })

  if (count > 0) {
    const length = Math.sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ)
    if (length > 0) {
      return [normalX / length, normalY / length, normalZ / length]
    }
  }

  // Fallback to default normal
  return [0, 0, 1]
}

/**
 * Compute face normal from three vertices
 */
const computeFaceNormal = (
  v0: [number, number, number],
  v1: [number, number, number],
  v2: [number, number, number]
): [number, number, number] => {
  const ux = v1[0] - v0[0]
  const uy = v1[1] - v0[1]
  const uz = v1[2] - v0[2]

  const vx = v2[0] - v0[0]
  const vy = v2[1] - v0[1]
  const vz = v2[2] - v0[2]

  // Cross product
  const nx = uy * vz - uz * vy
  const ny = uz * vx - ux * vz
  const nz = ux * vy - uy * vx

  return [nx, ny, nz]
}

/**
 * Pre-export validation checks
 * Returns { valid: boolean, errors: string[] }
 */
export const validatePreExport = (
  vertices: WeightVertex[],
  bones: SMDBone[],
  maxBonesPerMesh: number = 256
): { valid: boolean; errors: string[]; warnings: string[] } => {
  const errors: string[] = []
  const warnings: string[] = []

  // Check bone count limit
  if (bones.length > maxBonesPerMesh) {
    warnings.push(`Bone count (${bones.length}) exceeds Metin2 limit of ${maxBonesPerMesh}`)
  }

  // Check for zero-weight vertices
  const zeroWeightVerts = vertices.filter(v => {
    const totalWeight = v.weights.reduce((sum, w) => sum + w.weight, 0)
    return totalWeight < 0.001
  })

  if (zeroWeightVerts.length > 0) {
    errors.push(`Found ${zeroWeightVerts.length} unassigned vertices (zero weight)`)
  }

  // Check for vertices with too many bones
  const overBonedVerts = vertices.filter(v => v.weights.length > 4)
  if (overBonedVerts.length > 0) {
    errors.push(`Found ${overBonedVerts.length} vertices with more than 4 bone influences`)
  }

  // Check weight sums
  const invalidWeightVerts = vertices.filter(v => {
    const totalWeight = v.weights.reduce((sum, w) => sum + w.weight, 0)
    return Math.abs(totalWeight - 1.0) > 0.01 && totalWeight > 0
  })
  if (invalidWeightVerts.length > 0) {
    warnings.push(`${invalidWeightVerts.length} vertices have non-normalized weights (sum != 1.0)`)
  }

  return {
    valid: errors.length === 0,
    errors,
    warnings
  }
}

/**
 * Apply bind pose protection
 * Ensures Bip01 root bone has correct transform
 */
export const applyBindPoseProtection = (
  vertices: WeightVertex[],
  bones: SMDBone[],
  referenceBones: SMDBone[]
): WeightVertex[] => {
  // Find Bip01 root bone
  const rootBone = bones.find(b => b.name === 'Bip01' || b.name === 'bip01')
  const refRootBone = referenceBones.find(b => b.name === 'Bip01' || b.name === 'bip01')

  if (!rootBone || !refRootBone) {
    return vertices
  }

  // Check for position offset
  const offsetX = rootBone.position[0] - refRootBone.position[0]
  const offsetY = rootBone.position[1] - refRootBone.position[1]
  const offsetZ = rootBone.position[2] - refRootBone.position[2]

  if (Math.abs(offsetX) > 0.001 || Math.abs(offsetY) > 0.001 || Math.abs(offsetZ) > 0.001) {
    // Apply correction to all vertices influenced by Bip01
    vertices.forEach(v => {
      const hasRootWeight = v.weights.some(w => w.boneId === String(rootBone.id))
      if (hasRootWeight) {
        v.position[0] -= offsetX
        v.position[1] -= offsetY
        v.position[2] -= offsetZ
      }
    })
  }

  return vertices
}

/**
 * Scale normalization for export
 * Converts between different unit scales (x1.0 vs x100.0)
 */
export const normalizeScale = (
  vertices: WeightVertex[],
  targetScale: number = 1.0,
  sourceScale: number = 1.0
): WeightVertex[] => {
  const scaleFactor = targetScale / sourceScale

  return vertices.map(v => ({
    ...v,
    position: [
      v.position[0] * scaleFactor,
      v.position[1] * scaleFactor,
      v.position[2] * scaleFactor
    ]
  }))
}