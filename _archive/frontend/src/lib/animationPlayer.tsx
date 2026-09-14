import { useRef, useEffect, useState, useCallback } from 'react'
import * as THREE from 'three'
import type { SMDBone, SMDFrame } from './smdExporter'

export interface AnimationPlayerProps {
  bones: SMDBone[]
  frames: SMDFrame[]
  isPlaying: boolean
  onFrameChange?: (frameIndex: number) => void
}

export interface AnimationState {
  currentFrame: number
  totalFrames: number
  isPlaying: boolean
  speed: number
  loop: boolean
}

export const useAnimationPlayer = ({
  bones,
  frames,
  isPlaying: initialPlaying,
  onFrameChange
}: AnimationPlayerProps) => {
  const [state, setState] = useState<AnimationState>({
    currentFrame: 0,
    totalFrames: frames.length,
    isPlaying: initialPlaying,
    speed: 1.0,
    loop: true
  })
  
  const animationFrameRef = useRef<number>(0)
  const lastTimeRef = useRef<number>(0)
  const accumulatedTimeRef = useRef<number>(0)

  useEffect(() => {
    if (!state.isPlaying || frames.length === 0) {
      if (animationFrameRef.current) {
        cancelAnimationFrame(animationFrameRef.current)
      }
      return
    }

    const animate = (time: number) => {
      if (!state.isPlaying) return

      const deltaTime = time - lastTimeRef.current
      lastTimeRef.current = time

      accumulatedTimeRef.current += deltaTime * state.speed

      const frameInterval = 1000 / 60
      
      if (accumulatedTimeRef.current >= frameInterval) {
        const framesToAdvance = Math.floor(accumulatedTimeRef.current / frameInterval)
        accumulatedTimeRef.current -= framesToAdvance * frameInterval
        
        setState(prev => {
          let nextFrame = prev.currentFrame + framesToAdvance
          
          if (nextFrame >= frames.length) {
            if (prev.loop) {
              nextFrame = nextFrame % frames.length
            } else {
              nextFrame = frames.length - 1
              setState(s => ({ ...s, isPlaying: false }))
            }
          }
          
          return { ...prev, currentFrame: nextFrame }
        })
      }

      animationFrameRef.current = requestAnimationFrame(animate)
    }

    lastTimeRef.current = performance.now()
    animationFrameRef.current = requestAnimationFrame(animate)

    return () => {
      if (animationFrameRef.current) {
        cancelAnimationFrame(animationFrameRef.current)
      }
    }
  }, [state.isPlaying, state.speed, state.loop, frames.length])

  useEffect(() => {
    setState((prev) => ({
      ...prev,
      totalFrames: frames.length,
      currentFrame: Math.max(0, Math.min(prev.currentFrame, Math.max(0, frames.length - 1)))
    }));
  }, [frames.length]);

  useEffect(() => {
    if (onFrameChange) {
      onFrameChange(state.currentFrame)
    }
  }, [state.currentFrame, onFrameChange])

  const togglePlay = useCallback(() => {
    setState(prev => ({ ...prev, isPlaying: !prev.isPlaying }))
  }, [])

  const stop = useCallback(() => {
    setState(prev => ({ ...prev, isPlaying: false, currentFrame: 0 }))
  }, [])

  const seekToFrame = useCallback((frameIndex: number) => {
    const clampedFrame = Math.max(0, Math.min(frameIndex, frames.length - 1))
    setState(prev => ({ ...prev, currentFrame: clampedFrame }))
  }, [frames.length])

  const setSpeed = useCallback((speed: number) => {
    setState(prev => ({ ...prev, speed: Math.max(0.1, Math.min(4.0, speed)) }))
  }, [])

  const toggleLoop = useCallback(() => {
    setState(prev => ({ ...prev, loop: !prev.loop }))
  }, [])

  const currentFrame = frames[state.currentFrame] || null

  return {
    state,
    currentFrame,
    togglePlay,
    stop,
    seekToFrame,
    setSpeed,
    toggleLoop
  }
}

export const parseSMDAnimation = (smdContent: string): SMDFrame[] => {
  const lines = smdContent.split('\n')
  const frames: SMDFrame[] = []
  let currentFrame: SMDFrame | null = null

  for (const line of lines) {
    const trimmed = line.trim()
    
    if (trimmed.startsWith('time ')) {
      if (currentFrame) {
        frames.push(currentFrame)
      }
      
      const time = parseInt(trimmed.split(' ')[1])
      currentFrame = {
        time,
        boneTransforms: []
      }
    } else if (currentFrame && trimmed && !trimmed.startsWith('#')) {
      const parts = trimmed.split(/\s+/)
      if (parts.length >= 4) {
        const boneId = parseInt(parts[0])
        const x = parseFloat(parts[1])
        const y = parseFloat(parts[2])
        const z = parseFloat(parts[3])
        
        currentFrame.boneTransforms.push({
          boneId,
          position: [x, y, z],
          rotation: [0, 0, 0]
        })
      }
    }
  }
  
  if (currentFrame) {
    frames.push(currentFrame)
  }

  return frames
}

export const AnimationTimeline: React.FC<{
  state: AnimationState
  onTogglePlay: () => void
  onStop: () => void
  onSeek: (frame: number) => void
  onSpeedChange: (speed: number) => void
  onToggleLoop: () => void
}> = ({ state, onTogglePlay, onStop, onSeek, onSpeedChange, onToggleLoop }) => {
  return (
    <div className="flex items-center gap-4 p-3 bg-gray-800 rounded-lg border border-gray-700">
      <button
        onClick={onTogglePlay}
        className="px-3 py-1 rounded bg-blue-600 text-white text-sm hover:bg-blue-700"
      >
        {state.isPlaying ? '⏸' : '▶'}
      </button>
      
      <button
        onClick={onStop}
        className="px-3 py-1 rounded bg-red-600 text-white text-sm hover:bg-red-700"
      >
        ⏹
      </button>
      
      <div className="flex items-center gap-2 flex-1">
        <span className="text-xs text-gray-400">Frame</span>
        <input
          type="range"
          min="0"
          max={state.totalFrames - 1}
          value={state.currentFrame}
          onChange={(e) => onSeek(parseInt(e.target.value))}
          className="flex-1 appearance-none rounded bg-gray-600 h-2"
        />
        <span className="text-xs text-gray-400 w-12 text-right">
          {state.currentFrame} / {state.totalFrames - 1}
        </span>
      </div>
      
      <div className="flex items-center gap-2">
        <span className="text-xs text-gray-400">Speed</span>
        <select
          value={state.speed}
          onChange={(e) => onSpeedChange(parseFloat(e.target.value))}
          className="bg-gray-700 text-white text-xs rounded px-2 py-1"
        >
          <option value="0.25">0.25x</option>
          <option value="0.5">0.5x</option>
          <option value="1.0" selected>1.0x</option>
          <option value="2.0">2.0x</option>
          <option value="4.0">4.0x</option>
        </select>
      </div>
      
      <button
        onClick={onToggleLoop}
        className={`px-2 py-1 rounded text-xs ${state.loop ? 'bg-green-600 text-white' : 'bg-gray-600 text-gray-300'}`}
      >
        🔁 Loop
      </button>
    </div>
  )
}