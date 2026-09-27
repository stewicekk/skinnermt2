import { useRef, useState, useEffect, useMemo } from "react";
import * as THREE from "three";
import { useThree, useFrame } from "@react-three/fiber";
import type { ThreeEvent } from "@react-three/fiber";
import type { SMDBone } from "@/lib/smdExporter";

export interface BoneGizmosProps {
  bones: SMDBone[];
  selectedBone: string | null;
  onSelectBone: (name: string) => void;
  onUpdatePosition: (name: string, position: [number, number, number]) => void;
}

export function BoneGizmos({ bones, selectedBone, onSelectBone, onUpdatePosition }: BoneGizmosProps) {
  const { camera, raycaster, pointer } = useThree();
  const [dragging, setDragging] = useState<string | null>(null);
  const dragPlane = useRef(new THREE.Plane());
  const hitPoint = useRef(new THREE.Vector3());

  const boneMap = useMemo(() => new Map(bones.map((b) => [b.id, b])), [bones]);
  const connections = useMemo(
    () =>
      bones
        .filter((b) => b.parentId >= 0)
        .map((b) => ({ child: b, parent: boneMap.get(b.parentId) }))
        .filter((c) => c.parent),
    [bones, boneMap]
  );

  useFrame(() => {
    if (!dragging) return;
    raycaster.setFromCamera(pointer, camera);
    if (raycaster.ray.intersectPlane(dragPlane.current, hitPoint.current)) {
      onUpdatePosition(dragging, [hitPoint.current.x, hitPoint.current.y, hitPoint.current.z]);
    }
  });

  useEffect(() => {
    const onUp = () => setDragging(null);
    window.addEventListener("pointerup", onUp);
    return () => window.removeEventListener("pointerup", onUp);
  }, []);

  return (
    <group>
      {/* Bone connection lines */}
      {connections.map((conn, i) => {
        const positions = new Float32Array([
          conn.parent!.position[0], conn.parent!.position[1], conn.parent!.position[2],
          conn.child.position[0], conn.child.position[1], conn.child.position[2],
        ]);
        return (
          <line key={`bone-line-${i}`}>
            <bufferGeometry>
              <bufferAttribute attach="attributes-position" args={[positions, 3]} />
            </bufferGeometry>
            <lineBasicMaterial color="#64748b" transparent opacity={0.5} />
          </line>
        );
      })}

      {/* Bone position markers — draggable anchor points */}
      {bones.map((bone) => {
        const isSelected = selectedBone === bone.name;
        return (
          <mesh
            key={bone.name}
            position={bone.position}
            onPointerDown={(e: ThreeEvent<PointerEvent>) => {
              e.stopPropagation();
              onSelectBone(bone.name);
              setDragging(bone.name);
              const normal = new THREE.Vector3();
              camera.getWorldDirection(normal);
              dragPlane.current.setFromNormalAndCoplanarPoint(
                normal,
                new THREE.Vector3(bone.position[0], bone.position[1], bone.position[2])
              );
            }}
          >
            <sphereGeometry args={[isSelected ? 0.04 : 0.025, 12, 12]} />
            <meshBasicMaterial
              color={isSelected ? "#fbbf24" : "#f97316"}
              depthTest={false}
              transparent
              opacity={0.9}
            />
          </mesh>
        );
      })}
    </group>
  );
}
