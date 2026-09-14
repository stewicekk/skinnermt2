/**
 * Undo/Redo System using Command Pattern
 * Tracks all weight modifications for reversible operations
 * Integrates with Zustand store for state management
 * 
 * Keyboard shortcuts: Ctrl+Z (undo), Ctrl+Y (redo)
 */

import type { WeightVertex, BoneWeight } from '@/stores/useRiggingStore'

export type CommandType = 'ADD_WEIGHT' | 'SUBTRACT_WEIGHT' | 'SMOOTH' | 'NORMALIZE' | 'SET_WEIGHTS'

export interface WeightCommand {
  id: string
  type: CommandType
  timestamp: number
  meshId: string
  vertexIndex: number
  previousWeights: BoneWeight[]
  newWeights: BoneWeight[]
  description: string
}

interface CommandHistory {
  past: WeightCommand[]
  future: WeightCommand[]
  maxHistory: number
}

/**
 * Generate unique command ID
 */
const generateId = (): string => {
  return `cmd_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`
}

/**
 * Create a weight modification command
 */
export const createWeightCommand = (
  meshId: string,
  vertexIndex: number,
  previousWeights: BoneWeight[],
  newWeights: BoneWeight[],
  type: CommandType,
  description: string
): WeightCommand => {
  return {
    id: generateId(),
    type,
    timestamp: Date.now(),
    meshId,
    vertexIndex,
    previousWeights: [...previousWeights],
    newWeights: [...newWeights],
    description
  }
}

/**
 * Command History Manager
 * Handles undo/redo stack with configurable max depth
 */
export class CommandHistoryManager {
  private history: CommandHistory = {
    past: [],
    future: [],
    maxHistory: 100
  }

  /**
   * Execute a new command and add to history
   */
  execute(command: WeightCommand): void {
    this.history.past.push(command)
    
    // Clear future when new command is executed
    this.history.future = []
    
    // Enforce max history limit
    if (this.history.past.length > this.history.maxHistory) {
      this.history.past.shift()
    }
  }

  /**
   * Undo last command
   * Returns the undone command or null if nothing to undo
   */
  undo(): WeightCommand | null {
    if (this.history.past.length === 0) return null

    const command = this.history.past.pop()!
    this.history.future.push(command)
    
    return command
  }

  /**
   * Redo previously undone command
   * Returns the redone command or null if nothing to redo
   */
  redo(): WeightCommand | null {
    if (this.history.future.length === 0) return null

    const command = this.history.future.pop()!
    this.history.past.push(command)
    
    return command
  }

  /**
   * Get current undo command (peek without removing)
   */
  peekUndo(): WeightCommand | null {
    return this.history.past.length > 0 ? this.history.past[this.history.past.length - 1] : null
  }

  /**
   * Get current redo command (peek without removing)
   */
  peekRedo(): WeightCommand | null {
    return this.history.future.length > 0 ? this.history.future[this.history.future.length - 1] : null
  }

  /**
   * Check if undo is possible
   */
  canUndo(): boolean {
    return this.history.past.length > 0
  }

  /**
   * Check if redo is possible
   */
  canRedo(): boolean {
    return this.history.future.length > 0
  }

  /**
   * Clear all history
   */
  clear(): void {
    this.history = {
      past: [],
      future: [],
      maxHistory: this.history.maxHistory
    }
  }

  /**
   * Get history statistics
   */
  getStats(): { pastCount: number; futureCount: number; maxSize: number } {
    return {
      pastCount: this.history.past.length,
      futureCount: this.history.future.length,
      maxSize: this.history.maxHistory
    }
  }
}

/**
 * Keyboard shortcut handler for undo/redo
 */
export const setupUndoRedoKeyboard = (
  onUndo: () => void,
  onRedo: () => void
): (() => void) => {
  const handleKeyDown = (e: KeyboardEvent) => {
    // Ctrl+Z or Ctrl+Shift+Z for undo
    if ((e.ctrlKey || e.metaKey) && e.key === 'z' && !e.shiftKey) {
      e.preventDefault()
      onUndo()
    }
    
    // Ctrl+Y or Ctrl+Shift+Z for redo
    if ((e.ctrlKey || e.metaKey) && (e.key === 'y' || (e.key === 'z' && e.shiftKey))) {
      e.preventDefault()
      onRedo()
    }
  }

  document.addEventListener('keydown', handleKeyDown)

  // Return cleanup function
  return () => {
    document.removeEventListener('keydown', handleKeyDown)
  }
}

/**
 * React hook for undo/redo integration with Zustand
 */
export const useUndoRedo = (
  historyManager: CommandHistoryManager,
  meshData: Record<string, WeightVertex[]>,
  setMeshData: (meshId: string, vertices: WeightVertex[]) => void
) => {
  const executeCommand = (command: WeightCommand) => {
    const vertices = meshData[command.meshId] || []
    if (vertices[command.vertexIndex]) {
      vertices[command.vertexIndex] = {
        ...vertices[command.vertexIndex],
        weights: command.newWeights
      }
      setMeshData(command.meshId, [...vertices])
    }
    
    // Add to history
    historyManager.execute(command)
  }

  const undo = () => {
    const command = historyManager.undo()
    if (command) {
      const vertices = meshData[command.meshId] || []
      if (vertices[command.vertexIndex]) {
        vertices[command.vertexIndex] = {
          ...vertices[command.vertexIndex],
          weights: command.previousWeights
        }
        setMeshData(command.meshId, [...vertices])
      }
    }
  }

  const redo = () => {
    const command = historyManager.redo()
    if (command) {
      const vertices = meshData[command.meshId] || []
      if (vertices[command.vertexIndex]) {
        vertices[command.vertexIndex] = {
          ...vertices[command.vertexIndex],
          weights: command.newWeights
        }
        setMeshData(command.meshId, [...vertices])
      }
    }
  }

  return {
    executeCommand,
    undo,
    redo,
    canUndo: historyManager.canUndo(),
    canRedo: historyManager.canRedo()
  }
}