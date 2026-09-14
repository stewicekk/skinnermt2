import { Canvas } from '@react-three/fiber'
import { OrbitControls } from '@react-three/drei'

export const Scene: React.FC<{ children?: React.ReactNode; controlsEnabled?: boolean }> = ({ children, controlsEnabled = true }) => {
  return (
    <Canvas
      camera={{ position: [0, 0, 10], fov: 60 }}
      shadows
      style={{ width: '100%', height: '100%' }}
    >
      <ambientLight intensity={0.5} />
      <directionalLight position={[5, 5, 5]} intensity={1} castShadow />
      <OrbitControls enabled={controlsEnabled} enableZoom={true} enablePan={true} />
      {children}
    </Canvas>
  )
}