/**
 * Multi-Agent Orchestration Framework for Metin2 Rigging Studio
 * Provides a scalable agent system for complex workflows
 * 
 * Architecture:
 * - AgentRegistry: Central registry for all agents
 * - MessageBus: Inter-agent communication
 * - TaskQueue: Distributed task processing
 * - Agent Lifecycle: Spawn, monitor, terminate
 * - Specialized Agents: WeightPainter, Exporter, Validator, AI, etc.
 */

export type AgentId = string
export type AgentType = string
export type MessageType = string

export interface AgentMessage {
  id: string
  from: AgentId
  to: AgentId | 'broadcast'
  type: MessageType
  payload: any
  timestamp: number
  correlationId?: string
}

export interface AgentTask {
  id: string
  type: string
  payload: any
  priority: number
  assignedTo?: AgentId
  status: 'pending' | 'running' | 'completed' | 'failed'
  result?: any
  error?: string
  createdAt: number
  startedAt?: number
  completedAt?: number
}

export interface AgentCapability {
  name: string
  description: string
  inputSchema: any
  outputSchema: any
}

export interface AgentConfig {
  id: AgentId
  type: AgentType
  name: string
  capabilities: AgentCapability[]
  maxConcurrency: number
  autoStart: boolean
  config: Record<string, any>
}

export interface AgentState {
  id: AgentId
  type: AgentType
  name: string
  status: 'idle' | 'busy' | 'error' | 'terminated'
  currentTask?: AgentTask
  completedTasks: number
  failedTasks: number
  uptime: number
  lastHeartbeat: number
}

/**
 * Message Bus for inter-agent communication
 */
export class MessageBus {
  private subscribers: Map<MessageType, Set<(msg: AgentMessage) => void>> = new Map()
  private messageHistory: AgentMessage[] = []
  private maxHistory = 1000

  subscribe(type: MessageType, handler: (msg: AgentMessage) => void): () => void {
    if (!this.subscribers.has(type)) {
      this.subscribers.set(type, new Set())
    }
    this.subscribers.get(type)!.add(handler)
    
    return () => {
      this.subscribers.get(type)?.delete(handler)
    }
  }

  subscribeAll(handler: (msg: AgentMessage) => void): () => void {
    const unsubscribeFns: (() => void)[] = []
    for (const type of this.subscribers.keys()) {
      unsubscribeFns.push(this.subscribe(type, handler))
    }
    return () => unsubscribeFns.forEach(fn => fn())
  }

  publish(message: AgentMessage): void {
    this.messageHistory.push(message)
    if (this.messageHistory.length > this.maxHistory) {
      this.messageHistory.shift()
    }

    const handlers = this.subscribers.get(message.type)
    if (handlers) {
      for (const handler of handlers) {
        try {
          handler(message)
        } catch (e) {
          console.error(`Message handler error for ${message.type}:`, e)
        }
      }
    }

    // Also notify wildcard subscribers
    const allHandlers = this.subscribers.get('*')
    if (allHandlers) {
      for (const handler of allHandlers) {
        try {
          handler(message)
        } catch (e) {
          console.error('Wildcard handler error:', e)
        }
      }
    }
  }

  getHistory(type?: MessageType): AgentMessage[] {
    if (type) {
      return this.messageHistory.filter(m => m.type === type)
    }
    return [...this.messageHistory]
  }
}

/**
 * Task Queue for distributed task processing
 */
export class TaskQueue {
  private tasks: Map<string, AgentTask> = new Map()
  private pendingByPriority: Map<number, string[]> = new Map()
  private processing: Set<string> = new Set()
  private subscribers: Set<(task: AgentTask) => void> = new Set()

  enqueue(task: Omit<AgentTask, 'id' | 'status' | 'createdAt'>): string {
    const id = `task_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`
    const fullTask: AgentTask = {
      ...task,
      id,
      status: 'pending',
      createdAt: Date.now()
    }

    this.tasks.set(id, fullTask)
    
    const priority = task.priority || 0
    if (!this.pendingByPriority.has(priority)) {
      this.pendingByPriority.set(priority, [])
    }
    this.pendingByPriority.get(priority)!.push(id)
    
    this.notifySubscribers(fullTask)
    return id
  }

  dequeue(agentId: AgentId): AgentTask | null {
    // Get highest priority task
    const priorities = Array.from(this.pendingByPriority.keys()).sort((a, b) => b - a)
    
    for (const priority of priorities) {
      const queue = this.pendingByPriority.get(priority) || []
      const taskId = queue.shift()
      
      if (taskId) {
        const task = this.tasks.get(taskId)!
        task.status = 'running'
        task.assignedTo = agentId
        task.startedAt = Date.now()
        this.processing.add(taskId)
        this.notifySubscribers(task)
        return task
      }
    }
    
    return null
  }

  complete(taskId: string, result?: any): void {
    const task = this.tasks.get(taskId)
    if (!task) return
    
    task.status = 'completed'
    task.result = result
    task.completedAt = Date.now()
    this.processing.delete(taskId)
    this.notifySubscribers(task)
  }

  fail(taskId: string, error: string): void {
    const task = this.tasks.get(taskId)
    if (!task) return
    
    task.status = 'failed'
    task.error = error
    task.completedAt = Date.now()
    this.processing.delete(taskId)
    this.notifySubscribers(task)
  }

  getTask(id: string): AgentTask | undefined {
    return this.tasks.get(id)
  }

  getPendingCount(): number {
    let count = 0
    for (const queue of this.pendingByPriority.values()) {
      count += queue.length
    }
    return count
  }

  subscribe(handler: (task: AgentTask) => void): () => void {
    this.subscribers.add(handler)
    return () => this.subscribers.delete(handler)
  }

  private notifySubscribers(task: AgentTask): void {
    for (const handler of this.subscribers) {
      try {
        handler(task)
      } catch (e) {
        console.error('Task queue subscriber error:', e)
      }
    }
  }
}

/**
 * Base Agent Class
 */
export abstract class BaseAgent {
  public readonly id: AgentId
  public readonly type: AgentType
  public readonly name: string
  public readonly capabilities: AgentCapability[]
  public readonly maxConcurrency: number
  
  protected config: Record<string, any>
  protected messageBus: MessageBus
  protected taskQueue: TaskQueue
  protected state: AgentState
  protected running: boolean = false
  protected heartbeatInterval?: NodeJS.Timeout

  constructor(config: AgentConfig, messageBus: MessageBus, taskQueue: TaskQueue) {
    this.id = config.id
    this.type = config.type
    this.name = config.name
    this.capabilities = config.capabilities
    this.maxConcurrency = config.maxConcurrency
    this.config = config.config
    this.messageBus = messageBus
    this.taskQueue = taskQueue
    
    this.state = {
      id: this.id,
      type: this.type,
      name: this.name,
      status: 'idle',
      completedTasks: 0,
      failedTasks: 0,
      uptime: 0,
      lastHeartbeat: Date.now()
    }
  }

  abstract execute(task: AgentTask): Promise<any>

  async start(): Promise<void> {
    this.running = true
    this.heartbeatInterval = setInterval(() => this.heartbeat(), 5000)
    await this.onStart()
    console.log(`[${this.name}] Agent started`)
  }

  async stop(): Promise<void> {
    this.running = false
    if (this.heartbeatInterval) {
      clearInterval(this.heartbeatInterval)
    }
    await this.onStop()
    this.state.status = 'terminated'
    console.log(`[${this.name}] Agent stopped`)
  }

  protected async onStart(): Promise<void> {}
  protected async onStop(): Promise<void> {}

  protected heartbeat(): void {
    this.state.lastHeartbeat = Date.now()
    this.state.uptime = Date.now() - (this.state.lastHeartbeat - this.state.uptime)
  }

  async processTask(task: AgentTask): Promise<any> {
    this.state.status = 'busy'
    this.state.currentTask = task
    
    try {
      const result = await this.execute(task)
      this.state.completedTasks++
      this.state.status = 'idle'
      this.state.currentTask = undefined
      return result
    } catch (error) {
      this.state.failedTasks++
      this.state.status = 'error'
      this.state.currentTask = undefined
      throw error
    }
  }

  getState(): AgentState {
    return { ...this.state }
  }

  getCapabilities(): AgentCapability[] {
    return this.capabilities
  }

  protected sendMessage(to: AgentId | 'broadcast', type: MessageType, payload: any, correlationId?: string): void {
    this.messageBus.publish({
      id: `msg_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`,
      from: this.id,
      to,
      type,
      payload,
      timestamp: Date.now(),
      correlationId
    })
  }

  protected onMessage(type: MessageType, handler: (msg: AgentMessage) => void): () => void {
    return this.messageBus.subscribe(type, handler)
  }
}

/**
 * Agent Registry - Central management of all agents
 */
export class AgentRegistry {
  private agents: Map<AgentId, BaseAgent> = new Map()
  private agentConfigs: Map<AgentId, AgentConfig> = new Map()
  private typeIndex: Map<AgentType, Set<AgentId>> = new Map()
  public messageBus: MessageBus = new MessageBus()
  public taskQueue: TaskQueue = new TaskQueue()
  private running: boolean = false

  register(config: AgentConfig, agentClass: new (config: AgentConfig, messageBus: MessageBus, taskQueue: TaskQueue) => BaseAgent): BaseAgent {
    if (this.agents.has(config.id)) {
      throw new Error(`Agent ${config.id} already registered`)
    }

    const agent = new agentClass(config, this.messageBus, this.taskQueue)
    this.agents.set(config.id, agent)
    this.agentConfigs.set(config.id, config)
    
    if (!this.typeIndex.has(config.type)) {
      this.typeIndex.set(config.type, new Set())
    }
    this.typeIndex.get(config.type)!.add(config.id)

    console.log(`[Registry] Registered agent: ${config.name} (${config.id})`)
    return agent
  }

  unregister(agentId: AgentId): boolean {
    const agent = this.agents.get(agentId)
    if (!agent) return false

    agent.stop()
    this.agents.delete(agentId)
    this.agentConfigs.delete(agentId)
    
    const typeSet = this.typeIndex.get(agent.type)
    if (typeSet) {
      typeSet.delete(agentId)
    }
    
    return true
  }

  getAgent(agentId: AgentId): BaseAgent | undefined {
    return this.agents.get(agentId)
  }

  getAgentsByType(type: AgentType): BaseAgent[] {
    const ids = this.typeIndex.get(type) || new Set()
    return Array.from(ids).map(id => this.agents.get(id)!).filter(Boolean)
  }

  getAllAgents(): BaseAgent[] {
    return Array.from(this.agents.values())
  }

  async startAll(): Promise<void> {
    this.running = true
    for (const agent of this.agents.values()) {
      if (this.agentConfigs.get(agent.id)?.autoStart) {
        await agent.start()
      }
    }
    console.log(`[Registry] Started ${this.agents.size} agents`)
  }

  async stopAll(): Promise<void> {
    this.running = false
    for (const agent of this.agents.values()) {
      await agent.stop()
    }
    console.log(`[Registry] Stopped all agents`)
  }

  getSystemStatus(): {
    totalAgents: number
    runningAgents: number
    idleAgents: number
    busyAgents: number
    errorAgents: number
    pendingTasks: number
    processingTasks: number
  } {
    let idle = 0, busy = 0, error = 0
    for (const agent of this.agents.values()) {
      switch (agent.getState().status) {
        case 'idle': idle++; break
        case 'busy': busy++; break
        case 'error': error++; break
      }
    }
    
    return {
      totalAgents: this.agents.size,
      runningAgents: this.agents.size,
      idleAgents: idle,
      busyAgents: busy,
      errorAgents: error,
      pendingTasks: this.taskQueue.getPendingCount(),
      processingTasks: 0 // Would need to track in task queue
    }
  }
}

/**
 * Specialized Agent Implementations
 */

// Weight Painting Agent
export class WeightPainterAgent extends BaseAgent {
  async execute(task: AgentTask): Promise<any> {
    const { meshId, boneId, vertices, brushSettings } = task.payload
    
    // Simulate weight painting computation
    await new Promise(r => setTimeout(r, 100))
    
    return {
      vertices: vertices.map((v: any) => ({
        ...v,
        weights: v.weights.map((w: any) => 
          w.boneId === boneId ? { ...w, weight: Math.min(1, w.weight + brushSettings.strength) } : w
        )
      }))
    }
  }
}

// SMD/GR2 Exporter Agent
export class ExportAgent extends BaseAgent {
  async execute(task: AgentTask): Promise<any> {
    const { format, data } = task.payload
    
    await new Promise(r => setTimeout(r, 500))
    
    if (format === 'smd') {
      return { format: 'smd', content: '# SMD export placeholder' }
    } else if (format === 'gr2') {
      return { format: 'gr2', buffer: new ArrayBuffer(1024) }
    } else if (format === 'msm') {
      return { format: 'msm', content: '# MSM export placeholder' }
    }
    
    throw new Error(`Unknown format: ${format}`)
  }
}

// Validation Agent
export class ValidationAgent extends BaseAgent {
  async execute(task: AgentTask): Promise<any> {
    const { vertices, bones } = task.payload
    
    await new Promise(r => setTimeout(r, 50))
    
    const errors: string[] = []
    const warnings: string[] = []
    
    // Check zero-weight vertices
    const zeroWeight = vertices.filter((v: any) => 
      v.weights.reduce((sum: number, w: any) => sum + w.weight, 0) < 0.001
    )
    if (zeroWeight.length > 0) {
      warnings.push(`${zeroWeight.length} zero-weight vertices`)
    }
    
    // Check bone count
    if (bones.length > 256) {
      warnings.push(`Bone count exceeds 256 limit`)
    }
    
    return { valid: errors.length === 0, errors, warnings }
  }
}

// AI Weight Transfer Agent
export class AITransferAgent extends BaseAgent {
  async execute(task: AgentTask): Promise<any> {
    const { sourceVertices, targetVertices, sourceWeights, boneMap } = task.payload
    
    // Simulate KD-Tree based transfer
    await new Promise(r => setTimeout(r, 1000))
    
    return {
      transferredWeights: targetVertices.map(() => 
        Array.from({ length: boneMap.bones.length }, () => Math.random())
      ),
      method: 'kd_tree_topological_transfer'
    }
  }
}

// Animation Preview Agent
export class AnimationPreviewAgent extends BaseAgent {
  async execute(task: AgentTask): Promise<any> {
    const { meshData, animationFrames } = task.payload
    
    await new Promise(r => setTimeout(r, 200))
    
    return {
      frameCount: animationFrames.length,
      deformedMeshes: animationFrames.map(() => meshData)
    }
  }
}

/**
 * Agent Factory - Creates pre-configured agents
 */
export const AgentFactory = {
  createWeightPainter(messageBus: MessageBus, taskQueue: TaskQueue): WeightPainterAgent {
    return new WeightPainterAgent({
      id: `weight_painter_${Date.now()}`,
      type: 'weight_painter',
      name: 'Weight Painter',
      capabilities: [
        { name: 'paint_weights', description: 'Apply brush weights to vertices', inputSchema: {}, outputSchema: {} }
      ],
      maxConcurrency: 4,
      autoStart: true,
      config: { brushRadius: 1.0, brushStrength: 0.5 }
    }, messageBus, taskQueue)
  },

  createExporter(messageBus: MessageBus, taskQueue: TaskQueue): ExportAgent {
    return new ExportAgent({
      id: `exporter_${Date.now()}`,
      type: 'exporter',
      name: 'SMD/GR2/MSM Exporter',
      capabilities: [
        { name: 'export_smd', description: 'Export to SMD format', inputSchema: {}, outputSchema: {} },
        { name: 'export_gr2', description: 'Export to GR2 binary', inputSchema: {}, outputSchema: {} },
        { name: 'export_msm', description: 'Generate MSM script', inputSchema: {}, outputSchema: {} }
      ],
      maxConcurrency: 2,
      autoStart: true,
      config: {}
    }, messageBus, taskQueue)
  },

  createValidator(messageBus: MessageBus, taskQueue: TaskQueue): ValidationAgent {
    return new ValidationAgent({
      id: `validator_${Date.now()}`,
      type: 'validator',
      name: 'Pre-Export Validator',
      capabilities: [
        { name: 'validate_weights', description: 'Validate weight constraints', inputSchema: {}, outputSchema: {} },
        { name: 'validate_bones', description: 'Validate bone hierarchy', inputSchema: {}, outputSchema: {} }
      ],
      maxConcurrency: 8,
      autoStart: true,
      config: {}
    }, messageBus, taskQueue)
  },

  createAITransfer(messageBus: MessageBus, taskQueue: TaskQueue): AITransferAgent {
    return new AITransferAgent({
      id: `ai_transfer_${Date.now()}`,
      type: 'ai_transfer',
      name: 'AI Weight Transfer',
      capabilities: [
        { name: 'transfer_weights', description: 'KD-Tree weight transfer', inputSchema: {}, outputSchema: {} },
        { name: 'smart_mirror', description: 'Topological symmetry', inputSchema: {}, outputSchema: {} }
      ],
      maxConcurrency: 2,
      autoStart: false,
      config: { modelPath: 'models/weight_net.pt' }
    }, messageBus, taskQueue)
  },

  createAnimationPreview(messageBus: MessageBus, taskQueue: TaskQueue): AnimationPreviewAgent {
    return new AnimationPreviewAgent({
      id: `anim_preview_${Date.now()}`,
      type: 'animation_preview',
      name: 'Animation Preview',
      capabilities: [
        { name: 'preview_deformation', description: 'Real-time mesh deformation', inputSchema: {}, outputSchema: {} }
      ],
      maxConcurrency: 1,
      autoStart: true,
      config: {}
    }, messageBus, taskQueue)
  }
}

/**
 * Orchestrator - High-level workflow management
 */
export class Orchestrator {
  private registry: AgentRegistry
  private workflows: Map<string, Workflow> = new Map()

  constructor(registry: AgentRegistry) {
    this.registry = registry
  }

  async executeWorkflow(workflowId: string, input: any): Promise<any> {
    const workflow = this.workflows.get(workflowId)
    if (!workflow) throw new Error(`Workflow ${workflowId} not found`)

    const context: WorkflowContext = {
      input,
      results: {},
      registry: this.registry
    }

    for (const step of workflow.steps) {
      const agent = this.registry.getAgent(step.agentId)
      if (!agent) throw new Error(`Agent ${step.agentId} not found`)

      const taskId = this.registry.taskQueue.enqueue({
        type: step.taskType,
        payload: { ...input, ...step.payload, ...context.results },
        priority: step.priority || 0
      })

      // Wait for completion
      const result = await this.waitForTask(taskId)
      context.results[step.name] = result
    }

    return context.results
  }

  registerWorkflow(workflow: Workflow): void {
    this.workflows.set(workflow.id, workflow)
  }

  private waitForTask(taskId: string): Promise<any> {
    return new Promise((resolve, reject) => {
      const check = () => {
        const task = this.registry.taskQueue.getTask(taskId)
        if (!task) {
          reject(new Error(`Task ${taskId} not found`))
          return
        }
        if (task.status === 'completed') {
          resolve(task.result)
        } else if (task.status === 'failed') {
          reject(new Error(task.error || 'Task failed'))
        } else {
          setTimeout(check, 100)
        }
      }
      check()
    });
  }
}

interface Workflow {
  id: string
  name: string
  steps: WorkflowStep[]
}

interface WorkflowStep {
  name: string
  agentId: string
  taskType: string
  payload: any
  priority?: number
}

interface WorkflowContext {
  input: any
  results: Record<string, any>
  registry: AgentRegistry
}

// Singleton registry instance
let globalRegistry: AgentRegistry | null = null

export function getAgentRegistry(): AgentRegistry {
  if (!globalRegistry) {
    globalRegistry = new AgentRegistry()
    
    // Register default agents
    const messageBus = globalRegistry.messageBus
    const taskQueue = globalRegistry.taskQueue
    
    globalRegistry.register(
      {
        id: 'weight_painter_main',
        type: 'weight_painter',
        name: 'Main Weight Painter',
        capabilities: [{ name: 'paint', description: 'Weight painting', inputSchema: {}, outputSchema: {} }],
        maxConcurrency: 4,
        autoStart: true,
        config: {}
      },
      WeightPainterAgent
    )
    
    globalRegistry.register(
      {
        id: 'exporter_main',
        type: 'exporter',
        name: 'Main Exporter',
        capabilities: [
          { name: 'export_smd', description: 'SMD export', inputSchema: {}, outputSchema: {} },
          { name: 'export_gr2', description: 'GR2 export', inputSchema: {}, outputSchema: {} },
          { name: 'export_msm', description: 'MSM export', inputSchema: {}, outputSchema: {} }
        ],
        maxConcurrency: 2,
        autoStart: true,
        config: {}
      },
      ExportAgent
    )
    
    globalRegistry.register(
      {
        id: 'validator_main',
        type: 'validator',
        name: 'Main Validator',
        capabilities: [
          { name: 'validate', description: 'Pre-export validation', inputSchema: {}, outputSchema: {} }
        ],
        maxConcurrency: 8,
        autoStart: true,
        config: {}
      },
      ValidationAgent
    )
  }
  
  return globalRegistry!
}

/**
 * React Hook for using agents in components
 */
export function useAgent(agentId: string) {
  const registry = getAgentRegistry()
  const agent = registry.getAgent(agentId)
  
  return {
    agent,
    state: agent?.getState(),
    execute: (task: Omit<AgentTask, 'id' | 'status' | 'createdAt'>) => {
      if (!agent) throw new Error(`Agent ${agentId} not found`)
      const taskId = registry.taskQueue.enqueue(task)
      return registry.taskQueue.getTask(taskId)!
    }
  }
}

// Export all
export * from './gr2Binary'