/**
 * MSM Generator Module for Metin2
 * Generates Metin2 Script Mesh (.msm) text format for client loading
 * 
 * Format: Group ShapeDataXX, ShapeIndex, Model, SourceSkin
 */

import type { WeightVertex } from '@/stores/useRiggingStore'
import { exportToSMD } from './smdExporter'

export interface MSMGroup {
  name: string
  shapeIndex: number
  materialId: number
  triangleCount: number
  vertexOffset: number
}

export interface MSMConfig {
  modelName: string
  classType: 'warrior' | 'ninja' | 'sura' | 'shaman' | 'wolfman'
  meshPath: string
  texturePaths: string[]
  materialGroups: MSMGroup[]
}

/**
 * Generate Metin2 Script Mesh (.msm) content
 * Properly formatted with indentation for Metin2 client parsing
 */
export const generateMSM = (config: MSMConfig): string => {
  const lines: string[] = []

  // Header
  lines.push('# Metin2 Script Mesh')
  lines.push(`# Class: ${config.classType}`)
  lines.push(`# Model: ${config.modelName}`)
  lines.push('')

  // Material mappings
  config.texturePaths.forEach((tex, idx) => {
    lines.push(`# Texture ${idx}: ${tex}`)
  })
  lines.push('')

  // Shape Data groups
  config.materialGroups.forEach((group) => {
    lines.push(`Group ShapeData${String(group.shapeIndex).padStart(2, '0')}`)
    lines.push('{')
    lines.push('  ShapeIndex')
    lines.push('  {')
    lines.push(`    ${group.triangleCount}`)
    lines.push(`    ${group.vertexOffset}`)
    lines.push('  }')
    lines.push('')
    lines.push('  Model')
    lines.push('  {')
    lines.push(`    ${config.meshPath}`)
    lines.push('  }')
    lines.push('')
    lines.push('  SourceSkin')
    lines.push('  {')
    lines.push(`    ${config.classType}_base`)
    lines.push('  }')
    lines.push('}')
    lines.push('')
  })

  return lines.join('\n')
}

/**
 * Generate material groups from vertices and triangles
 * Automatically sorts triangles by material ID
 */
export const generateMaterialGroups = (
  vertices: WeightVertex[],
  triangles: Array<{materialIndex: number, vertexIndices: [number, number, number]}>,
  verticesPerGroup: number = 1000
): MSMGroup[] => {
  const groups: MSMGroup[] = []
  let currentGroup = 0
  let vertexOffset = 0

  // Group triangles by material
  const materialTriangles: Record<number, typeof triangles> = {}
  triangles.forEach((tri) => {
    if (!materialTriangles[tri.materialIndex]) {
      materialTriangles[tri.materialIndex] = []
    }
    materialTriangles[tri.materialIndex].push(tri)
  })

  // Create groups for each material
  Object.entries(materialTriangles).forEach(([matIdx, tris]) => {
    // Split into chunks if too many triangles
    const chunks = Math.ceil(tris.length / verticesPerGroup)
    
    for (let c = 0; c < chunks; c++) {
      const chunk = tris.slice(c * verticesPerGroup, (c + 1) * verticesPerGroup)
      groups.push({
        name: `ShapeData${String(currentGroup).padStart(2, '0')}`,
        shapeIndex: currentGroup,
        materialId: parseInt(matIdx),
        triangleCount: chunk.length,
        vertexOffset: vertexOffset
      })
      vertexOffset += chunk.length * 3  // 3 vertices per triangle
      currentGroup++
    }
  })

  return groups
}

/**
 * Generate complete export package (SMD + MSM + materials list)
 */
export const generateExportPackage = (
  vertices: WeightVertex[],
  triangles: Array<{materialIndex: number, vertexIndices: [number, number, number]}>,
  bones: Array<{id: number, name: string, parentId: number, position: [number, number, number], rotation: [number, number, number]}>,
  config: MSMConfig
): { smd: string; msm: string; materialList: string[] } => {
  // Generate SMD
  const smdContent = exportToSMD(vertices, triangles, bones, [])

  // Generate MSM
  const materialGroups = generateMaterialGroups(vertices, triangles)
  const msmContent = generateMSM({
    ...config,
    materialGroups
  })

  // Extract material list
  const materialList = config.texturePaths

  return {
    smd: smdContent,
    msm: msmContent,
    materialList
  }
}

/**
 * Validate MSM content for Metin2 client compatibility
 */
export const validateMSM = (msmContent: string): { valid: boolean; errors: string[] } => {
  const errors: string[] = []
  const lines = msmContent.split('\n')

  // Check for required sections
  const hasShapeData = lines.some(l => l.includes('Group ShapeData'))
  const hasShapeIndex = lines.some(l => l.includes('ShapeIndex'))
  const hasModel = lines.some(l => l.includes('Model'))
  const hasSourceSkin = lines.some(l => l.includes('SourceSkin'))

  if (!hasShapeData) errors.push('Missing Group ShapeData section')
  if (!hasShapeIndex) errors.push('Missing ShapeIndex section')
  if (!hasModel) errors.push('Missing Model section')
  if (!hasSourceSkin) errors.push('Missing SourceSkin section')

  return {
    valid: errors.length === 0,
    errors
  }
}