import { create } from 'zustand'
import { persist, createJSONStorage } from 'zustand/middleware'

export type Bone = {
  id: string
  name: string
  parent: string | null
  children: string[]
  transform: {
    position: [number, number, number]
    rotation: [number, number, number]
    scale: [number, number, number]
  }
}

export type WeightVertex = {
  id: number
  position: [number, number, number]
  weights: BoneWeight[]
}

export type BoneWeight = {
  boneId: string
  weight: number
}

export interface DisplayTriangle {
  materialIndex: number
  vertexIndices: [number, number, number]
}

export type ViewMode = 'solid' | 'wireframe' | 'heatmap' | 'xray' | 'skeleton'

export type BrushMode = 'add' | 'subtract' | 'set' | 'smooth' | 'blur' | 'sharpen' | 'normalize' | 'prune' | 'flood'

export type CameraView = 'perspective' | 'front' | 'back' | 'left' | 'right' | 'top' | 'bottom'

export type RiggingState = {
  selectedBone: string | null
  setSelectedBone: (boneId: string | null) => void

  currentMesh: string | null
  setCurrentMesh: (meshId: string | null) => void

  meshData: Record<string, WeightVertex[]>
  setMeshData: (meshId: string, vertices: WeightVertex[]) => void
  getMeshData: (meshId: string) => WeightVertex[] | undefined

  meshTriangles: Record<string, DisplayTriangle[]>
  setMeshTriangles: (meshId: string, triangles: DisplayTriangle[]) => void
  getMeshTriangles: (meshId: string) => DisplayTriangle[] | undefined

  boneLists: Record<string, string[]>
  setBoneList: (meshId: string, bones: string[]) => void
  getBoneList: (meshId: string) => string[] | undefined

  viewMode: ViewMode
  setViewMode: (mode: ViewMode) => void

  brushRadius: number
  setBrushRadius: (radius: number) => void
  brushStrength: number
  setBrushStrength: (strength: number) => void
  brushMode: BrushMode
  setBrushMode: (mode: BrushMode) => void

  cameraView: CameraView
  setCameraView: (view: CameraView) => void

  showGrid: boolean
  setShowGrid: (show: boolean) => void
  showAxes: boolean
  setShowAxes: (show: boolean) => void
  showSkeleton: boolean
  setShowSkeleton: (show: boolean) => void

  normalizeWeights: () => boolean

  history: WeightVertex[]
  setHistory: (vertices: WeightVertex[]) => void
  undo: () => boolean
  redo: () => boolean

  symmetryEnabled: boolean
  setSymmetryEnabled: (enabled: boolean) => void
  symmetryAxis: 'x' | 'y' | 'z'
  setSymmetryAxis: (axis: 'x' | 'y' | 'z') => void

  targetClass: string
  setTargetClass: (cls: string) => void

  isProcessing: boolean
  setIsProcessing: (processing: boolean) => void
  error: string | null
  setError: (err: string | null) => void

  // Toast notifications
  toasts: Array<{id: string; type: string; title: string; message: string; duration: number}>
  addToast: (toast: Omit<{id: string; type: string; title: string; message: string; duration: number}, 'id' | 'timestamp'>) => string
  removeToast: (id: string) => void
}

export const useRiggingStore = create<RiggingState>()(
  persist(
    (set, get) => ({
      selectedBone: null,
      setSelectedBone: (boneId: string | null) => set({ selectedBone: boneId }),

      currentMesh: null,
      setCurrentMesh: (meshId: string | null) => set({ currentMesh: meshId }),

      meshData: {},
      setMeshData: (meshId: string, vertices: WeightVertex[]) =>
        set((state) => ({ meshData: { ...state.meshData, [meshId]: vertices } })),

      getMeshData: (meshId: string) => get().meshData[meshId],

      meshTriangles: {},
      setMeshTriangles: (meshId: string, triangles: DisplayTriangle[]) =>
        set((state) => ({ meshTriangles: { ...state.meshTriangles, [meshId]: triangles } })),
      getMeshTriangles: (meshId: string) => get().meshTriangles[meshId],

      boneLists: {},
      setBoneList: (meshId: string, bones: string[]) =>
        set((state) => ({ boneLists: { ...state.boneLists, [meshId]: bones } })),
      getBoneList: (meshId: string) => get().boneLists[meshId],

      viewMode: 'heatmap',
      setViewMode: (mode: ViewMode) => set({ viewMode: mode }),

      brushRadius: 1.0,
      setBrushRadius: (radius: number) => set({ brushRadius: radius }),

      brushStrength: 1.0,
      setBrushStrength: (strength: number) => set({ brushStrength: strength }),

      brushMode: 'add',
      setBrushMode: (mode: BrushMode) =>
        set({ brushMode: mode }),

      cameraView: 'perspective',
      setCameraView: (view: CameraView) => set({ cameraView: view }),

      showGrid: true,
      setShowGrid: (show: boolean) => set({ showGrid: show }),

      showAxes: true,
      setShowAxes: (show: boolean) => set({ showAxes: show }),

      showSkeleton: false,
      setShowSkeleton: (show: boolean) => set({ showSkeleton: show }),

      normalizeWeights: (): boolean => {
        const { meshData } = get()
        let modified = false

        Object.entries(meshData).forEach(([meshId, vertices]) => {
          vertices.forEach((vertex) => {
            const totalWeight = vertex.weights.reduce(
              (sum, w) => sum + w.weight,
              0
            )

            if (Math.abs(totalWeight - 1.0) > 0.001) {
              modified = true

              if (totalWeight === 0) {
                const maxWeightBone = vertex.weights.reduce(
                  (max, w) => (w.weight > max.weight ? w : max),
                  { boneId: '', weight: 0 }
                )
                vertex.weights = [{ boneId: maxWeightBone.boneId, weight: 1.0 }]
              } else {
                const newWeights = vertex.weights.map((w) => ({
                  boneId: w.boneId,
                  weight: w.weight / totalWeight,
                }))
                vertex.weights = newWeights.slice(0, 4)
              }
            }

            if (vertex.weights.length > 4) {
              modified = true
              const sorted = [...vertex.weights].sort(
                (a, b) => b.weight - a.weight
              )
              vertex.weights = sorted.slice(0, 4)
            }
          })
        })

        return modified
      },

      history: [],
      setHistory: (vertices: WeightVertex[]) => set({ history: vertices }),

      undo: (): boolean => {
        const { history, setHistory } = get()
        if (history.length === 0) return false
        const previous = history[history.length - 1]
        setHistory(history.slice(0, -1))
        return true
      },

      redo: (): boolean => {
        const { history, setHistory } = get()
        return false
      },

      symmetryEnabled: false,
      setSymmetryEnabled: (enabled: boolean) => set({ symmetryEnabled: enabled }),

      symmetryAxis: 'x',
      setSymmetryAxis: (axis: 'x' | 'y' | 'z') => set({ symmetryAxis: axis }),

      targetClass: 'warrior',
      setTargetClass: (cls: string) => set({ targetClass: cls }),

      isProcessing: false,
      setIsProcessing: (processing: boolean) => set({ isProcessing: processing }),

    error: null,
    setError: (err: string | null) => set({ error: err }),

    // Toast notifications
    toasts: [],
    addToast: (toast) => {
      const id = `toast_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`
      const newToast = { ...toast, id }
      set((state) => ({ toasts: [...state.toasts, newToast] }))
      setTimeout(() => {
        set((state) => ({ toasts: state.toasts.filter(t => t.id !== id) }))
      }, toast.duration)
      return id
    },
    removeToast: (id) => {
      set((state) => ({ toasts: state.toasts.filter(t => t.id !== id) }))
    },
  }),
    {
      name: 'rigging-storage',
      storage: createJSONStorage(() => localStorage),
    }
  )
)