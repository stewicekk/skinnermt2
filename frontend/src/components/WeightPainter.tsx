import { useRef, useState, useCallback, useEffect } from 'react'
import { useThree } from '@react-three/fiber'
import { Raycaster, Vector2, Vector3 } from 'three'
import { normalizeVertexWeights, applyBrushFalloff } from '@/lib/weightUtils'
import { useRiggingStore } from '@/stores/useRiggingStore'
import type { WeightVertex, BoneWeight } from '@/stores/useRiggingStore'

export const useWeightPainter = (meshId: string) => {
  const { meshData, setMeshData, brushRadius, brushStrength, brushMode, symmetryEnabled, symmetryAxis, selectedBone } = useRiggingStore.getState()
  const raycaster = useRef(new Raycaster())
  const mouse = useRef(new Vector2())
  const [isPainting, setIsPainting] = useState(false)
  
  const vertices = meshData[meshId] || []

  const onPointerDown = useCallback((event: MouseEvent) => {
    const canvas = event.target as HTMLCanvasElement
    const rect = canvas.getBoundingClientRect()
    if (!rect) return
    
    mouse.current.x = ((event.clientX - rect.left) / rect.width) * 2 - 1
    mouse.current.y = -((event.clientY - rect.top) / rect.height) * 2 + 1
    
    setIsPainting(true)
    performPainting()
  }, [meshId])
  
  const onPointerMove = useCallback((event: MouseEvent) => {
    if (!isPainting) return
    
    const canvas = event.target as HTMLCanvasElement
    const rect = canvas.getBoundingClientRect()
    if (!rect) return
    
    mouse.current.x = ((event.clientX - rect.left) / rect.width) * 2 - 1
    mouse.current.y = -((event.clientY - rect.top) / rect.height) * 2 + 1
    
    performPainting()
  }, [isPainting, meshId])
  
  const onPointerUp = useCallback(() => {
    setIsPainting(false)
  }, [])
  
  const performPainting = useCallback(() => {
    const { scene, camera } = useThree((state: any) => ({ scene: state.scene, camera: state.camera }))
    
    raycaster.current.setFromCamera(mouse.current, camera)
    
    const intersected = raycaster.current.intersectObjects(scene.children, true)
    
    if (intersected.length > 0) {
      const intersectedMesh = intersected[0].object as any
      const geometry = intersectedMesh.geometry
      if (!geometry.attributes?.position) return
      
      const positionAttr = geometry.attributes.position
      const maxVerts = positionAttr.count
      
      // Use the intersection point as reference
      const intersectPoint = intersected[0].point
      
      let closestDist = Infinity
      let closestVertexIndex = -1
      let closestWorldPos: [number, number, number] = [0, 0, 0]
      
      for (let i = 0; i < maxVerts; i++) {
        const vx = positionAttr.getX(i)
        const vy = positionAttr.getY(i)
        const vz = positionAttr.getZ(i)
        
        const vertexWorldPos = new Vector3(vx, vy, vz).applyMatrix4(intersectedMesh.matrixWorld)
        const dist = intersectPoint.distanceTo(vertexWorldPos)
        
        if (dist < closestDist) {
          closestDist = dist
          closestVertexIndex = i
          closestWorldPos = [vertexWorldPos.x, vertexWorldPos.y, vertexWorldPos.z]
        }
      }
      
      if (closestVertexIndex >= 0 && closestDist < brushRadius * 2) {
        const vertexData = vertices[closestVertexIndex]
        if (!vertexData) return
        
        const currentWeights: BoneWeight[] = vertexData.weights.map((w) => ({
          boneId: w.boneId,
          weight: w.weight
        }))
        
        const targetBone = selectedBone || 'bip01 pelvis'
        
        const delta = applyBrushFalloff(
          closestWorldPos,
          closestWorldPos,
          brushStrength,
          brushMode
        )
        
        let updatedWeights: BoneWeight[]
        
        switch (brushMode) {
          case 'add':
            updatedWeights = addWeight(currentWeights, targetBone, delta)
            break
          case 'subtract':
            updatedWeights = subtractWeight(currentWeights, targetBone, delta)
            break
          case 'smooth':
            updatedWeights = smoothWeights(currentWeights, targetBone, delta)
            break
          case 'normalize':
            updatedWeights = [...currentWeights]
            break
          default:
            updatedWeights = [...currentWeights]
        }
        
        const tempVertex: WeightVertex = {
          id: closestVertexIndex,
          position: closestWorldPos,
          weights: updatedWeights
        }
        const normalized = normalizeVertexWeights([tempVertex])
        
        if (symmetryEnabled && targetBone) {
          applySymmetryToWeights(closestWorldPos, targetBone, normalized.modifiedVertices)
        }
        
        if (normalized.modifiedVertices[0]) {
          const newWeights = normalized.modifiedVertices[0].weights
          const updatedVertices = [...vertices]
          updatedVertices[closestVertexIndex] = {
            ...updatedVertices[closestVertexIndex],
            weights: newWeights
          }
          setMeshData(meshId, updatedVertices)
        }
      }
    }
  }, [meshId, brushRadius, brushStrength, brushMode, symmetryEnabled, symmetryAxis, selectedBone, vertices])
  
  const addWeight = (current: BoneWeight[], targetBone: string, delta: number): BoneWeight[] => {
    let updated = [...current]
    
    const boneIdx = updated.findIndex(w => w.boneId === targetBone)
    
    if (boneIdx >= 0) {
      updated[boneIdx].weight = Math.min(1.0, updated[boneIdx].weight + delta)
    } else {
      const minIdx = updated.reduce((min, w, i) => w.weight < updated[min].weight ? i : min, 0)
      updated[minIdx] = { boneId: targetBone, weight: Math.min(1.0, delta) }
    }
    
    if (updated.length > 4) {
      updated.sort((a, b) => a.weight - b.weight)
      updated = updated.slice(0, 4)
    }
    
    return updated
  }
  
  const subtractWeight = (current: BoneWeight[], targetBone: string, delta: number): BoneWeight[] => {
    let updated = [...current]
    
    const boneIdx = updated.findIndex(w => w.boneId === targetBone)
    
    if (boneIdx >= 0) {
      updated[boneIdx].weight = Math.max(0, updated[boneIdx].weight - delta)
      
      if (updated[boneIdx].weight < 0.001) {
        const remainingWeight = 1.0 - updated[boneIdx].weight
        updated = updated.filter(w => w.boneId !== targetBone)
        
        if (updated.length > 0 && remainingWeight > 0) {
          const perBone = remainingWeight / updated.length
          updated = updated.map(w => ({
            boneId: w.boneId,
            weight: Math.min(1.0, w.weight + perBone)
          }))
        }
      }
    }
    
    return updated
  }
  
  const smoothWeights = (current: BoneWeight[], targetBone: string, delta: number): BoneWeight[] => {
    let updated = [...current]
    
    const boneIdx = updated.findIndex(w => w.boneId === targetBone)
    
    if (boneIdx >= 0) {
      const currentWeight = updated[boneIdx].weight
      const newWeight = Math.min(1.0, currentWeight + delta)
      updated[boneIdx].weight = newWeight
      
      const otherWeights = updated.filter(w => w.boneId !== targetBone)
      const otherTotal = otherWeights.reduce((sum, w) => sum + w.weight, 0)
      
      if (otherTotal > 0 && newWeight < 1.0) {
        const reductionFactor = (1.0 - newWeight) / otherTotal
        updated = updated.map(w => {
          if (w.boneId !== targetBone) {
            return { boneId: w.boneId, weight: Math.max(0, w.weight - w.weight * reductionFactor) }
          }
          return w
        })
      }
    }
    
    return updated
  }
  
  const applySymmetryToWeights = (vertexPos: [number, number, number], targetBone: string, _vertices: WeightVertex[]) => {
    const isLeftBone = targetBone.toLowerCase().includes('l') || targetBone.toLowerCase().includes('left')
    
    if (isLeftBone) {
      // Mirror logic here
    }
  }
  
  useEffect(() => {
    const canvas = document.querySelector('canvas')
    if (canvas) {
      canvas.addEventListener('pointerdown', onPointerDown)
      canvas.addEventListener('pointermove', onPointerMove)
      canvas.addEventListener('pointerup', onPointerUp)
      canvas.addEventListener('pointerleave', onPointerUp)
    }
    
    return () => {
      if (canvas) {
        canvas.removeEventListener('pointerdown', onPointerDown)
        canvas.removeEventListener('pointermove', onPointerMove)
        canvas.removeEventListener('pointerup', onPointerUp)
        canvas.removeEventListener('pointerleave', onPointerUp)
      }
    }
  }, [onPointerDown, onPointerMove, onPointerUp])
  
  return {
    isPainting,
    activeVertices: vertices,
    setActiveVertices: (v: WeightVertex[]) => setMeshData(meshId, v),
    brushRadius,
    brushStrength,
    brushMode,
    setBrushMode: (m: 'add' | 'subtract' | 'smooth' | 'normalize') => useRiggingStore.getState().setBrushMode(m),
    setBrushRadius: (r: number) => useRiggingStore.getState().setBrushRadius(r),
    setBrushStrength: (s: number) => useRiggingStore.getState().setBrushStrength(s)
  }
}