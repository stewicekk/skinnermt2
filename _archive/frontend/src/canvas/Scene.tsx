import { Canvas, useThree } from '@react-three/fiber'
import { OrbitControls } from '@react-three/drei'
import { useEffect } from 'react'
import * as THREE from 'three'
import { useRiggingStore, type CameraView } from '@/stores/useRiggingStore'

const CAMERA_POSITIONS: Record<CameraView, [number, number, number]> = {
  perspective: [3, 2, 6],
  front: [0, 0, 8],
  back: [0, 0, -8],
  left: [-8, 0, 0],
  right: [8, 0, 0],
  top: [0, 8, 0.001],
  bottom: [0, -8, 0.001],
}

function CameraController() {
  const cameraView = useRiggingStore((s) => s.cameraView)
  const camera = useThree((s) => s.camera)
  const controls = useThree((s) => s.controls)

  useEffect(() => {
    const pos = CAMERA_POSITIONS[cameraView]
    camera.position.set(...pos)
    camera.lookAt(0, 0, 0)
    if (controls) (controls as any).update()
  }, [cameraView, camera, controls])

  return null
}

function ViewportHelpers() {
  const showGrid = useRiggingStore((s) => s.showGrid)
  const showAxes = useRiggingStore((s) => s.showAxes)

  return (
    <>
      {showGrid && <gridHelper args={[20, 20, '#374151', '#1f2937']} position={[0, -2, 0]} />}
      {showAxes && <axesHelper args={[3]} />}
    </>
  )
}

export const Scene: React.FC<{ children?: React.ReactNode; controlsEnabled?: boolean }> = ({ children, controlsEnabled = true }) => {
  return (
    <Canvas
      camera={{ position: [3, 2, 6], fov: 60 }}
      shadows
      gl={{ toneMapping: THREE.ACESFilmicToneMapping, toneMappingExposure: 1.15 }}
      style={{ width: '100%', height: '100%' }}
    >
      <color attach="background" args={['#0a0a14']} />
      <fog attach="fog" args={['#0a0a14', 12, 30]} />
      <ambientLight intensity={0.35} />
      <hemisphereLight args={['#a0b4d0', '#1a1a2e', 0.4]} />
      <directionalLight position={[5, 10, 5]} intensity={0.85} castShadow />
      <directionalLight position={[-5, 5, -5]} intensity={0.3} />
      <directionalLight position={[0, -3, 8]} intensity={0.15} color="#4a6fa5" />
      <ViewportHelpers />
      <CameraController />
      <OrbitControls enabled={controlsEnabled} enableZoom={true} enablePan={true} makeDefault />
      {children}
    </Canvas>
  )
}
