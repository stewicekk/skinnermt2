import { useState, useCallback } from 'react'
import type { Bone } from '@/stores/useRiggingStore'

export interface BoneTreeProps {
  bones: Bone[]
  selectedBone: string | null
  onSelectBone: (boneId: string) => void
  lockedBones: string[]
  onToggleLock: (boneId: string) => void
  hiddenBones: string[]
  onToggleHidden: (boneId: string) => void
}

/**
 * Bone Hierarchy Tree Component
 * Displays skeleton hierarchy with lock/hide controls
 */
export const BoneTree: React.FC<BoneTreeProps> = ({
  bones,
  selectedBone,
  onSelectBone,
  lockedBones,
  onToggleLock,
  hiddenBones,
  onToggleHidden
}) => {
  const [expandedNodes, setExpandedNodes] = useState<Set<string>>(new Set())

  // Toggle node expansion
  const toggleNode = useCallback((boneId: string) => {
    setExpandedNodes(prev => {
      const next = new Set(prev)
      if (next.has(boneId)) {
        next.delete(boneId)
      } else {
        next.add(boneId)
      }
      return next
    })
  }, [])

  // Build tree structure from flat bone list
  const buildTree = useCallback((boneList: Bone[]): Bone[] => {
    const boneMap = new Map(boneList.map(b => [b.id, b]))
    const roots: Bone[] = []

    boneList.forEach(bone => {
      if (bone.parent && boneMap.has(bone.parent)) {
        const parent = boneMap.get(bone.parent)
        if (parent) {
          if (!parent.children) parent.children = []
          parent.children.push(bone.id)
        }
      } else {
        roots.push(bone)
      }
    })

    return roots
  }, [])

  // Render bone node recursively
  const renderBone = (bone: Bone, depth: number = 0): JSX.Element => {
    const isLocked = lockedBones.includes(bone.id)
    const isHidden = hiddenBones.includes(bone.id)
    const isSelected = selectedBone === bone.id
    const hasChildren = bone.children && bone.children.length > 0
    const isExpanded = expandedNodes.has(bone.id)

    return (
      <div key={bone.id} style={{ paddingLeft: `${depth * 16}px` }}>
        <div
          className={`flex items-center gap-2 px-2 py-1 rounded cursor-pointer text-xs transition-colors ${
            isSelected
              ? 'bg-blue-600 text-white'
              : 'text-gray-300 hover:bg-gray-700'
          } ${isHidden ? 'opacity-50 italic' : ''}`}
          onClick={() => onSelectBone(bone.id)}
        >
          {/* Expand/collapse arrow */}
          {hasChildren ? (
            <button
              onClick={(e) => {
                e.stopPropagation()
                toggleNode(bone.id)
              }}
              className="w-4 h-4 flex items-center justify-center text-gray-400 hover:text-white"
            >
              {isExpanded ? '▼' : '▶'}
            </button>
          ) : (
            <span className="w-4" />
          )}

          {/* Bone name */}
          <span className="flex-1 truncate">{bone.name}</span>

          {/* Lock button */}
          <button
            onClick={(e) => {
              e.stopPropagation()
              onToggleLock(bone.id)
            }}
            className={`w-4 h-4 flex items-center justify-center ${
              isLocked ? 'text-yellow-400' : 'text-gray-500 hover:text-yellow-300'
            }`}
            title={isLocked ? 'Unlock bone' : 'Lock bone'}
          >
            {isLocked ? '🔒' : '🔓'}
          </button>

          {/* Hide button */}
          <button
            onClick={(e) => {
              e.stopPropagation()
              onToggleHidden(bone.id)
            }}
            className={`w-4 h-4 flex items-center justify-center ${
              isHidden ? 'text-red-400' : 'text-gray-500 hover:text-red-300'
            }`}
            title={isHidden ? 'Show bone' : 'Hide bone'}
          >
            {isHidden ? '👁' : '🚫'}
          </button>
        </div>

        {/* Render children */}
        {hasChildren && isExpanded && (
          <div>
            {bone.children!.map(childId => {
              const childBone = bones.find(b => b.id === childId)
              return childBone ? renderBone(childBone, depth + 1) : null
            })}
          </div>
        )}
      </div>
    )
  }

  const treeRoots = buildTree(bones)

  return (
    <div className="bone-tree">
      <div className="flex items-center justify-between mb-2">
        <h3 className="text-xs uppercase text-gray-400">Bones</h3>
        <span className="text-xs text-gray-500">{bones.length}</span>
      </div>
      <div className="space-y-0.5">
        {treeRoots.map(bone => renderBone(bone))}
      </div>
    </div>
  )
}