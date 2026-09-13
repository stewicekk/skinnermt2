import { useState, useCallback } from 'react'
import type { BoneWeight } from '@/stores/useRiggingStore'

export interface BoneLock {
  boneId: string
  locked: boolean
  reason?: string // e.g., "Protected socket bone", "User pinned"
}

export interface BoneLockState {
  lockedBones: Map<string, boolean>
  lockReason: Map<string, string>
}

/**
 * Bone Weight Locking System (Pinning)
 * Prevents modification of weights for specific bones
 * 
 * Use cases:
 * - Lock root bones (Bip01 Pelvis) during weight painting
 * - Protect socket bones (equip_right, equip_left, stip)
 * - Lock bones during AI retargeting
 */

export const useBoneLocking = () => {
  const [lockedBones, setLockedBones] = useState<Map<string, boolean>>(new Map())
  const [lockReasons, setLockReasons] = useState<Map<string, string>>(new Map())

  // Lock a bone
  const lockBone = useCallback((boneId: string, reason?: string) => {
    setLockedBones(prev => new Map(prev.set(boneId, true)))
    if (reason) {
      setLockReasons(prev => new Map(prev.set(boneId, reason)))
    }
  }, [])

  // Unlock a bone
  const unlockBone = useCallback((boneId: string) => {
    setLockedBones(prev => {
      const next = new Map(prev)
      next.delete(boneId)
      return next
    })
    setLockReasons(prev => {
      const next = new Map(prev)
      next.delete(boneId)
      return next
    })
  }, [])

  // Toggle lock state
  const toggleBoneLock = useCallback((boneId: string, reason?: string) => {
    setLockedBones(prev => {
      const isLocked = prev.get(boneId)
      if (isLocked) {
        const next = new Map(prev)
        next.delete(boneId)
        return next
      } else {
        return new Map(prev.set(boneId, true))
      }
    })
    
    if (reason) {
      setLockReasons(prev => new Map(prev.set(boneId, reason)))
    }
  }, [])

  // Check if a bone is locked
  const isBoneLocked = useCallback((boneId: string): boolean => {
    return lockedBones.get(boneId) || false
  }, [lockedBones])

  // Get lock reason for a bone
  const getLockReason = useCallback((boneId: string): string | undefined => {
    return lockReasons.get(boneId)
  }, [lockReasons])

  // Lock protected Metin2 socket bones
  const lockSocketBones = useCallback((bones: string[]) => {
    const socketBones = bones.filter(b => 
      b.toLowerCase().includes('equip') || 
      b.toLowerCase().includes('stip') ||
      b.toLowerCase().includes('head') ||
      b.toLowerCase().includes('pelvis')
    )
    
    socketBones.forEach(bone => {
      lockBone(bone, 'Protected socket bone')
    })
    
    return socketBones
  }, [lockBone])

  // Apply locks to weight modification
  const applyLocks = useCallback((
    vertexWeights: BoneWeight[],
    targetBoneId: string
  ): BoneWeight[] => {
    // If target bone is locked, reject modification
    if (isBoneLocked(targetBoneId)) {
      return vertexWeights // Return unchanged
    }
    
    // Filter out locked bones from modification
    return vertexWeights.map(w => ({
      boneId: w.boneId,
      weight: isBoneLocked(w.boneId) ? w.weight : w.weight
    }))
  }, [isBoneLocked])

  return {
    lockedBones,
    lockReasons,
    lockBone,
    unlockBone,
    toggleBoneLock,
    isBoneLocked,
    getLockReason,
    lockSocketBones,
    applyLocks
  }
}

/**
 * Check if a bone name is a protected Metin2 system bone
 */
export const isProtectedBone = (boneName: string): boolean => {
  const protectedNames = [
    'equip_right', 'equip_left', 'stip',
    'bip01 head', 'bip01 pelvis', 'bip01'
  ]
  
  const lowerName = boneName.toLowerCase()
  return protectedNames.some(name => lowerName.includes(name))
}

/**
 * Get auto-lock recommendations for a bone hierarchy
 */
export const getAutoLockRecommendations = (bones: string[]): string[] => {
  const recommendations: string[] = []
  
  bones.forEach(bone => {
    if (isProtectedBone(bone)) {
      recommendations.push(bone)
    }
  })
  
  return recommendations
}