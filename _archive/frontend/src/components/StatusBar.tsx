import { useEffect, useRef, useState } from 'react'
import { useRiggingStore } from '@/stores/useRiggingStore'

export const StatusBar: React.FC = () => {
  const currentMesh = useRiggingStore((s) => s.currentMesh)
  const meshData = useRiggingStore((s) => s.meshData)
  const meshTriangles = useRiggingStore((s) => s.meshTriangles)
  const boneLists = useRiggingStore((s) => s.boneLists)
  const viewMode = useRiggingStore((s) => s.viewMode)
  const brushMode = useRiggingStore((s) => s.brushMode)
  const selectedBone = useRiggingStore((s) => s.selectedBone)
  const targetClass = useRiggingStore((s) => s.targetClass)
  const cameraView = useRiggingStore((s) => s.cameraView)

  const [fps, setFps] = useState(60)
  const frameCount = useRef(0)
  const lastTime = useRef(performance.now())

  useEffect(() => {
    let raf: number
    const loop = () => {
      frameCount.current++
      const now = performance.now()
      if (now - lastTime.current >= 1000) {
        setFps(frameCount.current)
        frameCount.current = 0
        lastTime.current = now
      }
      raf = requestAnimationFrame(loop)
    }
    raf = requestAnimationFrame(loop)
    return () => cancelAnimationFrame(raf)
  }, [])

  const vertices = currentMesh ? meshData[currentMesh] ?? [] : []
  const triangles = currentMesh ? meshTriangles[currentMesh] ?? [] : []
  const bones = currentMesh ? boneLists[currentMesh] ?? [] : []

  return (
    <footer className="flex items-center gap-4 px-4 py-1 bg-gray-900 border-t border-gray-800 text-[11px] text-gray-400 flex-shrink-0">
      <span>FPS: <span className={fps >= 50 ? 'text-green-400' : fps >= 30 ? 'text-amber-400' : 'text-red-400'}>{fps}</span></span>
      <span>Verts: <span className="text-gray-200">{vertices.length}</span></span>
      <span>Tris: <span className="text-gray-200">{triangles.length}</span></span>
      <span>Bones: <span className="text-gray-200">{bones.length}</span></span>
      <span>View: <span className="text-blue-400 capitalize">{viewMode}</span></span>
      <span>Brush: <span className="text-emerald-400 capitalize">{brushMode}</span></span>
      <span>Cam: <span className="text-purple-400 capitalize">{cameraView}</span></span>
      <span>Bone: <span className="text-gray-200">{selectedBone ?? 'None'}</span></span>
      <span>Class: <span className="text-gray-200 capitalize">{targetClass}</span></span>
      <span className="ml-auto text-gray-600">Metin2 Rigging Studio</span>
    </footer>
  )
}
