/**
 * Auto-Save Module with IndexedDB
 * Provides persistent storage for rigging session data
 * Automatically saves every 3 minutes
 * Supports session restore after browser crash
 */

import type { DisplayTriangle, WeightVertex } from '@/stores/useRiggingStore'

export interface SavedSession {
  id: string
  timestamp: number
  meshId: string
  vertices: WeightVertex[]
  triangles: DisplayTriangle[]
  bones: string[]
  smdBones: Array<{ id: number; name: string; parentId: number; position: [number, number, number]; rotation: [number, number, number] }>
  frames: Array<{ time: number; boneTransforms: Array<{ boneId: number; position: [number, number, number]; rotation: [number, number, number] }> }>
  materials: string[]
  targetClass: string
  brushSettings: {
    radius: number
    strength: number
    mode: string
  }
  version: string
}

const DB_NAME = 'Metin2SkinningStudio'
const DB_VERSION = 1
const STORE_NAME = 'sessions'
const AUTO_SAVE_INTERVAL = 3 * 60 * 1000 // 3 minutes

/**
 * IndexedDB wrapper for session persistence
 */
class SessionStore {
  private db: IDBDatabase | null = null
  private dbReady: Promise<void>

  constructor() {
    this.dbReady = this.initDB()
  }

  /**
   * Initialize IndexedDB database
   */
  private async initDB(): Promise<void> {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(DB_NAME, DB_VERSION)

      request.onerror = () => {
        console.error('IndexedDB error:', request.error)
        reject(request.error)
      }

      request.onsuccess = () => {
        this.db = request.result
        resolve()
      }

      request.onupgradeneeded = (event) => {
        const db = (event.target as IDBOpenDBRequest).result
        
        if (!db.objectStoreNames.contains(STORE_NAME)) {
          const store = db.createObjectStore(STORE_NAME, { keyPath: 'id' })
          store.createIndex('timestamp', 'timestamp', { unique: false })
          store.createIndex('meshId', 'meshId', { unique: false })
        }
      }
    })
  }

  /**
   * Save a session to IndexedDB
   */
  async saveSession(session: SavedSession): Promise<void> {
    await this.dbReady
    
    return new Promise((resolve, reject) => {
      if (!this.db) {
        reject(new Error('Database not initialized'))
        return
      }

      const transaction = this.db.transaction([STORE_NAME], 'readwrite')
      const store = transaction.objectStore(STORE_NAME)
      
      const request = store.put(session)
      
      request.onsuccess = () => resolve()
      request.onerror = () => reject(request.error)
    })
  }

  /**
   * Get a session by ID
   */
  async getSession(id: string): Promise<SavedSession | null> {
    await this.dbReady
    
    return new Promise((resolve, reject) => {
      if (!this.db) {
        reject(new Error('Database not initialized'))
        return
      }

      const transaction = this.db.transaction([STORE_NAME], 'readonly')
      const store = transaction.objectStore(STORE_NAME)
      
      const request = store.get(id)
      
      request.onsuccess = () => resolve(request.result || null)
      request.onerror = () => reject(request.error)
    })
  }

  /**
   * Get all sessions sorted by timestamp (newest first)
   */
  async getAllSessions(): Promise<SavedSession[]> {
    await this.dbReady
    
    return new Promise((resolve, reject) => {
      if (!this.db) {
        reject(new Error('Database not initialized'))
        return
      }

      const transaction = this.db.transaction([STORE_NAME], 'readonly')
      const store = transaction.objectStore(STORE_NAME)
      const index = store.index('timestamp')
      
      const request = index.getAll()
      
      request.onsuccess = () => {
        const sessions = request.result.sort((a, b) => b.timestamp - a.timestamp)
        resolve(sessions)
      }
      request.onerror = () => reject(request.error)
    })
  }

  /**
   * Delete a session by ID
   */
  async deleteSession(id: string): Promise<void> {
    await this.dbReady
    
    return new Promise((resolve, reject) => {
      if (!this.db) {
        reject(new Error('Database not initialized'))
        return
      }

      const transaction = this.db.transaction([STORE_NAME], 'readwrite')
      const store = transaction.objectStore(STORE_NAME)
      
      const request = store.delete(id)
      
      request.onsuccess = () => resolve()
      request.onerror = () => reject(request.error)
    })
  }

  /**
   * Get the most recent session (for auto-restore)
   */
  async getLatestSession(): Promise<SavedSession | null> {
    const sessions = await this.getAllSessions()
    return sessions.length > 0 ? sessions[0] : null
  }
}

// Global instance
let sessionStore: SessionStore | null = null

const getSessionStore = (): SessionStore => {
  if (!sessionStore) {
    sessionStore = new SessionStore()
  }
  return sessionStore
}

/**
 * Create a session object from current state
 */
export const createSession = (
  meshId: string,
  vertices: WeightVertex[],
  triangles: DisplayTriangle[],
  bones: string[],
  smdBones: SavedSession['smdBones'],
  frames: SavedSession['frames'],
  materials: string[],
  targetClass: string,
  brushSettings: { radius: number; strength: number; mode: string }
): SavedSession => {
  return {
    id: `session_${Date.now()}`,
    timestamp: Date.now(),
    meshId,
    vertices: vertices.map(v => ({ ...v, weights: v.weights.map(w => ({ ...w })) })),
    triangles: triangles.map(t => ({ ...t, vertexIndices: [...t.vertexIndices] as [number, number, number] })),
    bones: [...bones],
    smdBones: smdBones.map(b => ({ ...b, position: [...b.position] as [number, number, number], rotation: [...b.rotation] as [number, number, number] })),
    frames: frames.map(f => ({ ...f, boneTransforms: f.boneTransforms.map(t => ({ ...t, position: [...t.position] as [number, number, number], rotation: [...t.rotation] as [number, number, number] })) })),
    materials: [...materials],
    targetClass,
    brushSettings: { ...brushSettings },
    version: '1.0.0'
  }
}

/**
 * Auto-save current session
 * Called periodically and on significant state changes
 */
export const autoSave = async (
  meshId: string,
  vertices: WeightVertex[],
  triangles: DisplayTriangle[],
  bones: string[],
  smdBones: SavedSession['smdBones'],
  frames: SavedSession['frames'],
  materials: string[],
  targetClass: string,
  brushSettings: { radius: number; strength: number; mode: string }
): Promise<void> => {
  try {
    const store = getSessionStore()
    const session = createSession(meshId, vertices, triangles, bones, smdBones, frames, materials, targetClass, brushSettings)
    await store.saveSession(session)
  } catch (error) {
    console.error('Auto-save failed:', error)
  }
}

/**
 * Restore the latest saved session
 * Returns session data or null if no session exists
 */
export const restoreSession = async (): Promise<SavedSession | null> => {
  try {
    const store = getSessionStore()
    const session = await store.getLatestSession()
    return session
  } catch (error) {
    console.error('Session restore failed:', error)
    return null
  }
}

/**
 * Clear all saved sessions
 */
export const clearAllSessions = async (): Promise<void> => {
  try {
    const store = getSessionStore()
    const sessions = await store.getAllSessions()
    
    for (const session of sessions) {
      await store.deleteSession(session.id)
    }
  } catch (error) {
    console.error('Clear sessions failed:', error)
  }
}

/**
 * Start auto-save interval
 * Returns cleanup function to stop auto-save
 */
export const startAutoSave = (
  saveCallback: () => Promise<void>,
  intervalMs: number = AUTO_SAVE_INTERVAL
): (() => void) => {
  const intervalId = setInterval(() => {
    saveCallback()
  }, intervalMs)

  // Return cleanup function
  return () => {
    clearInterval(intervalId)
  }
}

/**
 * Check if there's a restorable session
 * Returns session metadata or null
 */
export const hasRestorableSession = async (): Promise<{
  hasSession: boolean
  timestamp?: number
  meshId?: string
}> => {
  try {
    const store = getSessionStore()
    const session = await store.getLatestSession()
    
    if (session) {
      return {
        hasSession: true,
        timestamp: session.timestamp,
        meshId: session.meshId
      }
    }
    
    return { hasSession: false }
  } catch {
    return { hasSession: false }
  }
}