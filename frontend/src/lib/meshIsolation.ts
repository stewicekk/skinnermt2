import { useState, useCallback, useRef } from 'react'
import * as THREE from 'three'

export interface SubMesh {
  id: string
  name: string
  materialIndex: number
  triangleCount: number
  vertexRange: [number, number]
  visible: boolean
}

export interface MeshIsolationState {
  hiddenMeshes: Set<string>
  isolatedMesh: string | null
  originalVisibility: Map<string, boolean>
}

export const useMeshIsolation = (
  scene: THREE.Scene | null,
  subMeshes: SubMesh[]
) => {
  const [state, setState] = useState<MeshIsolationState>({
    hiddenMeshes: new Set(),
    isolatedMesh: null,
    originalVisibility: new Map()
  })

  const originalVisRef = useRef<Map<string, boolean>>(new Map())

  const hideMesh = useCallback((meshId: string) => {
    setState(prev => {
      const nextHidden = new Set(prev.hiddenMeshes)
      nextHidden.add(meshId)
      
      if (scene) {
        const mesh = scene.getObjectByName(meshId)
        if (mesh && mesh.type === 'Mesh') {
          originalVisRef.current.set(meshId, mesh.visible)
          mesh.visible = false
        }
      }
      
      return { ...prev, hiddenMeshes: nextHidden }
    })
  }, [scene])

  const showMesh = useCallback((meshId: string) => {
    setState(prev => {
      const nextHidden = new Set(prev.hiddenMeshes)
      nextHidden.delete(meshId)
      
      if (scene) {
        const mesh = scene.getObjectByName(meshId)
        if (mesh && mesh.type === 'Mesh' && originalVisRef.current.has(meshId)) {
          mesh.visible = originalVisRef.current.get(meshId) || true
          originalVisRef.current.delete(meshId)
        }
      }
      
      return { ...prev, hiddenMeshes: nextHidden }
    })
  }, [scene])

  const toggleMesh = useCallback((meshId: string) => {
    if (state.hiddenMeshes.has(meshId)) {
      showMesh(meshId)
    } else {
      hideMesh(meshId)
    }
  }, [state.hiddenMeshes, hideMesh, showMesh])

  const isolateMesh = useCallback((meshId: string) => {
    if (scene) {
      scene.traverse((object) => {
        if (object.type === 'Mesh' && object.name !== meshId) {
          originalVisRef.current.set(object.name, object.visible)
          object.visible = false
        }
      })
    }
    
    setState(prev => ({
      ...prev,
      isolatedMesh: meshId,
      hiddenMeshes: new Set(subMeshes.filter(m => m.id !== meshId).map(m => m.id))
    }))
  }, [scene, subMeshes])

  const showAllMeshes = useCallback(() => {
    if (scene) {
      scene.traverse((object) => {
        if (object.type === 'Mesh' && originalVisRef.current.has(object.name)) {
          object.visible = originalVisRef.current.get(object.name) || true
        }
      })
      originalVisRef.current.clear()
    }
    
    setState(prev => ({
      ...prev,
      hiddenMeshes: new Set(),
      isolatedMesh: null
    }))
  }, [scene])

  const handleKeyDown = useCallback((e: KeyboardEvent) => {
    if (e.key === 'H' && (e.altKey || e.metaKey)) {
      e.preventDefault()
      showAllMeshes()
    }
  }, [showAllMeshes])

  const cleanup = useCallback(() => {
    showAllMeshes()
  }, [showAllMeshes])

  return {
    state,
    hideMesh,
    showMesh,
    toggleMesh,
    isolateMesh,
    showAllMeshes,
    handleKeyDown,
    cleanup,
    isHidden: (meshId: string) => state.hiddenMeshes.has(meshId),
    isIsolated: (meshId: string) => state.isolatedMesh === meshId
  }
}

export const extractSubMeshes = (
  geometry: THREE.BufferGeometry,
  materialIndices?: number[]
): SubMesh[] => {
  const index = geometry.index
  if (!index) return []

  const indices = index.array
  const materials = materialIndices || []
  
  const materialGroups: Record<number, number[]> = {}
  
  for (let i = 0; i < indices.length; i += 3) {
    const matIdx = materials[i / 3] || 0
    if (!materialGroups[matIdx]) {
      materialGroups[matIdx] = []
    }
    materialGroups[matIdx].push(i / 3)
  }

  const subMeshes: SubMesh[] = []
  let vertexOffset = 0

  Object.entries(materialGroups).forEach(([matIdx, triIndices]) => {
    const startVert = vertexOffset
    const endVert = vertexOffset + triIndices.length * 3 - 1
    
    subMeshes.push({
      id: `submesh_${matIdx}`,
      name: `Material ${matIdx}`,
      materialIndex: parseInt(matIdx),
      triangleCount: triIndices.length,
      vertexRange: [startVert, endVert],
      visible: true
    })
    
    vertexOffset += triIndices.length * 3
  })

  return subMeshes
}

export const applyMeshMask = (
  geometry: THREE.BufferGeometry,
  subMeshes: SubMesh[],
  hiddenMeshes: Set<string>
): void => {
  const index = geometry.index
  if (!index) return

  let drawCount = 0

  subMeshes.forEach(mesh => {
    if (!hiddenMeshes.has(mesh.id)) {
      drawCount += mesh.triangleCount * 3
    }
  })

  geometry.setDrawRange(0, drawCount)
}