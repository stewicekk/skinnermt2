import { useMemo } from 'react'
import * as THREE from 'three'

/**
 * Bone Weight Heatmap Shader
 * Visualizes vertex weights as colors:
 * - Red (1.0) = Full influence of selected bone
 * - Blue (0.0) = No influence
 * - Green = Intermediate weights
 * 
 * Supports single bone heatmap or total weight visualization
 */

export interface HeatmapConfig {
  enabled: boolean
  boneId: string | null
  showTotalWeight: boolean
  colorScale: 'red-blue' | 'green-red' | 'cool-warm'
}

/**
 * Generate heatmap vertex colors based on weights
 * Returns Float32Array of RGB values for BufferGeometry
 */
export const generateHeatmapColors = (
  vertices: Array<{weights: Array<{boneId: string, weight: number}>}>,
  config: HeatmapConfig,
  totalVertexCount: number
): Float32Array => {
  const colors = new Float32Array(totalVertexCount * 3)

  vertices.forEach((vertex, idx) => {
    let weightValue = 0

    if (config.showTotalWeight) {
      // Show total weight sum
      weightValue = vertex.weights.reduce((sum, w) => sum + w.weight, 0)
    } else if (config.boneId) {
      // Show weight for specific bone
      const boneWeight = vertex.weights.find(w => w.boneId === config.boneId)
      weightValue = boneWeight ? boneWeight.weight : 0
    } else {
      // Show maximum weight
      weightValue = Math.max(...vertex.weights.map(w => w.weight), 0)
    }

    // Clamp to [0, 1]
    weightValue = Math.max(0, Math.min(1, weightValue))

    // Convert weight to color based on scale
    const color = weightToColor(weightValue, config.colorScale)

    colors[idx * 3] = color[0]
    colors[idx * 3 + 1] = color[1]
    colors[idx * 3 + 2] = color[2]
  })

  return colors
}

/**
 * Convert weight value (0-1) to RGB color
 */
const weightToColor = (
  weight: number,
  scale: 'red-blue' | 'green-red' | 'cool-warm'
): [number, number, number] => {
  switch (scale) {
    case 'red-blue':
      // Red (1.0) to Blue (0.0)
      return [1.0, weight, weight]
    
    case 'green-red':
      // Green (0.0) to Red (1.0)
      return [weight, 1.0 - weight, 0.1]
    
    case 'cool-warm':
      // Blue (cold) to Red (warm)
      if (weight < 0.5) {
        const t = weight * 2
        return [t, 0.2, 1.0 - t]
      } else {
        const t = (weight - 0.5) * 2
        return [1.0, t, 1.0 - t]
      }
    
    default:
      return [weight, weight, weight]
  }
}

/**
 * Create heatmap material for Three.js mesh
 * Uses vertex colors to display weight distribution
 */
export const createHeatmapMaterial = (config: HeatmapConfig): THREE.MeshStandardMaterial => {
  return new THREE.MeshStandardMaterial({
    vertexColors: true,
    flatShading: false,
    side: THREE.DoubleSide
  })
}

/**
 * Apply heatmap to mesh geometry
 * Modifies the geometry's color attribute
 */
export const applyHeatmapToGeometry = (
  geometry: THREE.BufferGeometry,
  vertices: Array<{weights: Array<{boneId: string, weight: number}>}>,
  config: HeatmapConfig
): void => {
  const colorAttribute = generateHeatmapColors(vertices, config, vertices.length)
  
  geometry.setAttribute(
    'color',
    new THREE.BufferAttribute(colorAttribute, 3)
  )
  
  geometry.attributes.color.needsUpdate = true
}

/**
 * Create heatmap visualization as overlay
 * Returns a transparent mesh with heatmap colors
 */
export const createHeatmapOverlay = (
  sourceGeometry: THREE.BufferGeometry,
  vertices: Array<{weights: Array<{boneId: string, weight: number}>}>,
  config: HeatmapConfig
): THREE.Mesh => {
  // Clone geometry to avoid modifying original
  const heatmapGeometry = sourceGeometry.clone()
  
  // Apply heatmap colors
  applyHeatmapToGeometry(heatmapGeometry, vertices, config)
  
  // Create transparent material
  const material = new THREE.MeshBasicMaterial({
    vertexColors: true,
    transparent: true,
    opacity: 0.6,
    side: THREE.DoubleSide,
    depthTest: true
  })
  
  return new THREE.Mesh(heatmapGeometry, material)
}