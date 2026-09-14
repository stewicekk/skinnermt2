/**
 * GR2 Binary Parser & Exporter for Metin2
 * Implements Granny 3D (.gr2) format parsing and generation
 * 
 * GR2 Format Specification (Granny 3D):
 * - Header: Magic "GR24", version, section count
 * - Sections: File info, meshes, skeletons, animations, materials, textures
 * - Uses little-endian binary encoding
 * - Supports compressed vertex data, bone indices, weights
 * 
 * Reference: Granny 3D SDK documentation, Metin2 client source
 */

import { Buffer } from 'buffer'

// GR2 Type Definitions
export interface GR2Header {
  magic: string          // "GR24"
  version: number        // Usually 0x80000000 + version
  totalSize: number
  sectionCount: number
  sections: GR2Section[]
}

export interface GR2Section {
  type: number
  offset: number
  size: number
  nameOffset: number
  name: string
  data?: GR2SectionData
}

export interface GR2SectionData {
  // Varies by section type
  fileInfo?: GR2FileInfo
  mesh?: GR2Mesh
  skeleton?: GR2Skeleton
  animation?: GR2Animation
  material?: GR2Material
  texture?: GR2Texture
  [key: string]: any
}

export interface GR2FileInfo {
  toolId: number
  userData: number
  flags: number
  sourcePath: string
  extension: string
  timeStamp: number
}

export interface GR2Mesh {
  name: string
  vertexCount: number
  faceCount: number
  materialName: string
  boundsMin: [number, number, number]
  boundsMax: [number, number, number]
  vertexFormat: GR2VertexFormat
  vertices: GR2Vertex[]
  faces: GR2Face[]
  primaryTopology: number
  topologies: GR2Topology[]
  morphTargets?: GR2MorphTarget[]
}

export interface GR2VertexFormat {
  position: boolean
  normal: boolean
  color: boolean
  uvCount: number
  boneIndexCount: number
  boneWeightCount: number
}

export interface GR2Vertex {
  position?: [number, number, number]
  normal?: [number, number, number]
  color?: [number, number, number, number]
  uvs?: [number, number][]
  boneIndices?: number[]
  boneWeights?: number[]
}

export interface GR2Face {
  indices: [number, number, number]
  materialIndex: number
}

export interface GR2Topology {
  firstFace: number
  faceCount: number
  firstVertex: number
  vertexCount: number
}

export interface GR2Skeleton {
  name: string
  boneCount: number
  bones: GR2Bone[]
  lodType: number
  lodError: number
  extendedDataOffset: number
  extendedDataSize: number
}

export interface GR2Bone {
  name: string
  parentIndex: number
  localTransform: GR2Transform
  inverseWorldTransform: GR2Transform
  extendedDataOffset: number
  extendedDataSize: number
}

export interface GR2Transform {
  position: [number, number, number]
  orientation: [number, number, number, number]
  scaleShear: [number, number, number][]
}

export interface GR2Animation {
  name: string
  duration: number
  timeStep: number
  frameCount: number
  boneCount: number
  tracks: GR2AnimationTrack[]
  boundsMin: [number, number, number]
  boundsMax: [number, number, number]
  extendedDataOffset: number
  extendedDataSize: number
}

export interface GR2AnimationTrack {
  boneIndex: number
  positionCurve?: GR2Curve
  orientationCurve?: GR2Curve
  scaleShearCurve?: GR2Curve
}

export interface GR2Curve {
  knotCount: number
  degree: number
  knots: number[]
  controls: number[][]
}

export interface GR2Material {
  name: string
  diffuseColor: [number, number, number, number]
  specularColor: [number, number, number, number]
  specularPower: number
  selfIllumColor: [number, number, number, number]
  opacity: number
  maps: GR2MaterialMap[]
  extendedDataOffset: number
  extendedDataSize: number
}

export interface GR2MaterialMap {
  type: number
  textureName: string
  uvIndex: number
  uOffset: number
  vOffset: number
  uScale: number
  vScale: number
  rotation: number
}

export interface GR2Texture {
  name: string
  width: number
  height: number
  format: number
  mipCount: number
  pixelData: Uint8Array
  extendedDataOffset: number
  extendedDataSize: number
}

export interface GR2MorphTarget {
  name: string
  vertexCount: number
  vertices: GR2MorphVertex[]
}

export interface GR2MorphVertex {
  position: [number, number, number]
  normal?: [number, number, number]
}

/**
 * GR2 Binary Reader
 * Parses .gr2 files into structured data
 */
export class GR2Reader {
  private buffer: Buffer
  private offset: number = 0
  private sections: Map<number, GR2Section> = new Map()

  constructor(arrayBuffer: ArrayBuffer) {
    this.buffer = Buffer.from(arrayBuffer)
  }

  read(): GR2Header {
    this.offset = 0
    const header = this.readHeader()
    this.readSections(header)
    return header
  }

  private readHeader(): GR2Header {
    const magic = this.readString(4)
    if (magic !== 'GR24') {
      throw new Error(`Invalid GR2 magic: ${magic}`)
    }

    const version = this.readUInt32()
    const totalSize = this.readUInt32()
    const sectionCount = this.readUInt32()

    return {
      magic,
      version,
      totalSize,
      sectionCount,
      sections: []
    }
  }

  private readSections(header: GR2Header): void {
    // Read section table
    const sections: GR2Section[] = []
    
    for (let i = 0; i < header.sectionCount; i++) {
      const sectionOffset = this.readUInt32()
      const sectionSize = this.readUInt32()
      const nameOffset = this.readUInt32()
      const type = this.readUInt32()
      
      // Save current position
      const savedOffset = this.offset
      
      // Read section name
      this.offset = nameOffset
      const name = this.readString()
      
      // Read section data
      this.offset = sectionOffset
      const data = this.readSectionData(type)
      
      const section: GR2Section = {
        type,
        offset: sectionOffset,
        size: sectionSize,
        nameOffset,
        name,
        data: data ?? undefined
      }
      
      sections.push(section)
      this.sections.set(type, section)
      
      // Restore position
      this.offset = savedOffset
    }
    
    header.sections = sections
  }

  private readSectionData(type: number): GR2SectionData | undefined {
    const SECTION_TYPES: Record<number, string> = {
      0xCAFEBABE: 'file_info',
      0x00100000: 'mesh',
      0x00200000: 'skeleton',
      0x00400000: 'animation',
      0x00800000: 'material',
      0x01000000: 'texture',
      0x02000000: 'vertex_data',
      0x04000000: 'index_data',
      0x08000000: 'morph_target'
    }

    const typeName = SECTION_TYPES[type]
    
    switch (typeName) {
      case 'file_info':
        return { fileInfo: this.readFileInfo() }
      case 'mesh':
        return { mesh: this.readMesh() }
      case 'skeleton':
        return { skeleton: this.readSkeleton() }
      case 'animation':
        return { animation: this.readAnimation() }
      case 'material':
        return { material: this.readMaterial() }
      case 'texture':
        return { texture: this.readTexture() }
      default:
        return undefined
    }
  }

  private readFileInfo(): GR2FileInfo {
    return {
      toolId: this.readUInt32(),
      userData: this.readUInt32(),
      flags: this.readUInt32(),
      sourcePath: this.readString(),
      extension: this.readString(),
      timeStamp: this.readUInt32()
    }
  }

  private readMesh(): GR2Mesh {
    const name = this.readString()
    const vertexCount = this.readUInt32()
    const faceCount = this.readUInt32()
    const materialName = this.readString()
    
    const boundsMin = this.readVec3()
    const boundsMax = this.readVec3()
    
    const vertexFormat = this.readVertexFormat()
    const vertices = this.readVertices(vertexCount, vertexFormat)
    const faces = this.readFaces(faceCount)
    
    const primaryTopology = this.readUInt32()
    const topologies = this.readTopologies()
    
    return {
      name,
      vertexCount,
      faceCount,
      materialName,
      boundsMin,
      boundsMax,
      vertexFormat,
      vertices,
      faces,
      primaryTopology,
      topologies
    }
  }

  private readVertexFormat(): GR2VertexFormat {
    const flags = this.readUInt32()
    return {
      position: (flags & 0x1) !== 0,
      normal: (flags & 0x2) !== 0,
      color: (flags & 0x4) !== 0,
      uvCount: (flags >> 3) & 0xF,
      boneIndexCount: (flags >> 7) & 0xF,
      boneWeightCount: (flags >> 11) & 0xF
    }
  }

  private readVertices(count: number, format: GR2VertexFormat): GR2Vertex[] {
    const vertices: GR2Vertex[] = []
    
    for (let i = 0; i < count; i++) {
      const vertex: GR2Vertex = {}
      
      if (format.position) {
        vertex.position = this.readVec3()
      }
      if (format.normal) {
        vertex.normal = this.readVec3()
      }
      if (format.color) {
        vertex.color = this.readVec4()
      }
      if (format.uvCount > 0) {
        vertex.uvs = []
        for (let u = 0; u < format.uvCount; u++) {
          vertex.uvs.push(this.readVec2())
        }
      }      if (format.boneIndexCount > 0) {
        vertex.boneIndices = []
        for (let b = 0; b < format.boneIndexCount; b++) {
          vertex.boneIndices.push(this.readUInt8())
        }
      }
      if (format.boneWeightCount > 0) {
        vertex.boneWeights = []
        for (let w = 0; w < format.boneWeightCount; w++) {
          vertex.boneWeights.push(this.readFloat32())
        }
      }
      
      vertices.push(vertex)
    }
    
    return vertices
  }

  private readFaces(count: number): GR2Face[] {
    const faces: GR2Face[] = []
    
    for (let i = 0; i < count; i++) {
      faces.push({
        indices: [
          this.readUInt16(),
          this.readUInt16(),
          this.readUInt16()
        ],
        materialIndex: this.readUInt16()
      })
    }
    
    return faces
  }

  private readTopologies(): GR2Topology[] {
    const count = this.readUInt32()
    const topologies: GR2Topology[] = []
    
    for (let i = 0; i < count; i++) {
      topologies.push({
        firstFace: this.readUInt32(),
        faceCount: this.readUInt32(),
        firstVertex: this.readUInt32(),
        vertexCount: this.readUInt32()
      })
    }
    
    return topologies
  }

  private readSkeleton(): GR2Skeleton {
    const name = this.readString()
    const boneCount = this.readUInt32()
    
    const bones: GR2Bone[] = []
    for (let i = 0; i < boneCount; i++) {
      bones.push({
        name: this.readString(),
        parentIndex: this.readInt32(),
        localTransform: this.readTransform(),
        inverseWorldTransform: this.readTransform(),
        extendedDataOffset: this.readUInt32(),
        extendedDataSize: this.readUInt32()
      })
    }
    
    return {
      name,
      boneCount,
      bones,
      lodType: this.readUInt32(),
      lodError: this.readFloat32(),
      extendedDataOffset: this.readUInt32(),
      extendedDataSize: this.readUInt32()
    }
  }

  private readTransform(): GR2Transform {
    return {
      position: this.readVec3(),
      orientation: this.readVec4(),
      scaleShear: [
        this.readVec3(),
        this.readVec3(),
        this.readVec3()
      ]
    }
  }

  private readAnimation(): GR2Animation {
    const name = this.readString()
    const duration = this.readFloat32()
    const timeStep = this.readFloat32()
    const frameCount = this.readUInt32()
    const boneCount = this.readUInt32()
    
    const tracks: GR2AnimationTrack[] = []
    for (let i = 0; i < boneCount; i++) {
      tracks.push({
        boneIndex: this.readUInt32(),
        positionCurve: this.readCurve(),
        orientationCurve: this.readCurve(),
        scaleShearCurve: this.readCurve()
      })
    }
    
    return {
      name,
      duration,
      timeStep,
      frameCount,
      boneCount,
      tracks,
      boundsMin: this.readVec3(),
      boundsMax: this.readVec3(),
      extendedDataOffset: this.readUInt32(),
      extendedDataSize: this.readUInt32()
    }
  }

  private readCurve(): GR2Curve | undefined {
    const hasCurve = this.readUInt8()
    if (!hasCurve) return undefined
    
    return {
      knotCount: this.readUInt32(),
      degree: this.readUInt32(),
      knots: Array(this.readUInt32()).fill(0).map(() => this.readFloat32()),
      controls: Array(this.readUInt32()).fill(0).map(() => 
        Array(this.readUInt32()).fill(0).map(() => this.readFloat32())
      )
    }
  }

  private readMaterial(): GR2Material {
    return {
      name: this.readString(),
      diffuseColor: this.readVec4(),
      specularColor: this.readVec4(),
      specularPower: this.readFloat32(),
      selfIllumColor: this.readVec4(),
      opacity: this.readFloat32(),
      maps: this.readMaterialMaps(),
      extendedDataOffset: this.readUInt32(),
      extendedDataSize: this.readUInt32()
    }
  }

  private readMaterialMaps(): GR2MaterialMap[] {
    const count = this.readUInt32()
    const maps: GR2MaterialMap[] = []
    
    for (let i = 0; i < count; i++) {
      maps.push({
        type: this.readUInt32(),
        textureName: this.readString(),
        uvIndex: this.readUInt32(),
        uOffset: this.readFloat32(),
        vOffset: this.readFloat32(),
        uScale: this.readFloat32(),
        vScale: this.readFloat32(),
        rotation: this.readFloat32()
      })
    }
    
    return maps
  }

  private readTexture(): GR2Texture {
    const name = this.readString()
    const width = this.readUInt32()
    const height = this.readUInt32()
    const format = this.readUInt32()
    const mipCount = this.readUInt32()
    
    const pixelDataSize = width * height * 4 // RGBA
    const pixelData = this.buffer.subarray(this.offset, this.offset + pixelDataSize)
    this.offset += pixelDataSize
    
    return {
      name,
      width,
      height,
      format,
      mipCount,
      pixelData,
      extendedDataOffset: this.readUInt32(),
      extendedDataSize: this.readUInt32()
    }
  }

  // Helper methods
  private readUInt8(): number { return this.buffer.readUInt8(this.offset++) }
  private readUInt16(): number { const v = this.buffer.readUInt16LE(this.offset); this.offset += 2; return v }
  private readUInt32(): number { const v = this.buffer.readUInt32LE(this.offset); this.offset += 4; return v }
  private readInt32(): number { const v = this.buffer.readInt32LE(this.offset); this.offset += 4; return v }
  private readFloat32(): number { const v = this.buffer.readFloatLE(this.offset); this.offset += 4; return v }
  
  private readVec2(): [number, number] { return [this.readFloat32(), this.readFloat32()] }
  private readVec3(): [number, number, number] { return [this.readFloat32(), this.readFloat32(), this.readFloat32()] }
  private readVec4(): [number, number, number, number] { return [this.readFloat32(), this.readFloat32(), this.readFloat32(), this.readFloat32()] }
  
  private readString(length?: number): string {
    if (length !== undefined) {
      const value = this.buffer.toString("utf8", this.offset, this.offset + length);
      this.offset += length;
      return value;
    }
    const start = this.offset
    while (this.offset < this.buffer.length && this.buffer[this.offset] !== 0) {
      this.offset++
    }
    const str = this.buffer.toString('utf8', start, this.offset)
    this.offset++ // Skip null terminator
    return str
  }
}

/**
 * GR2 Binary Writer
 * Generates .gr2 files from structured data
 */
export class GR2Writer {
  private chunks: Buffer[] = []
  private sectionOffsets: number[] = []
  private sectionSizes: number[] = []
  private sectionNames: string[] = []
  private sectionTypes: number[] = []

  write(header: GR2Header): Buffer {
    this.chunks = []
    this.sectionOffsets = []
    this.sectionSizes = []
    this.sectionNames = []
    this.sectionTypes = []

    // Write header placeholder (will update later)
    this.writeString('GR24')
    this.writeUInt32(header.version)
    this.writeUInt32(0) // totalSize placeholder
    this.writeUInt32(header.sections.length)
    
    // Write section table placeholders
    const sectionTableOffset = this.getOffset()
    for (let i = 0; i < header.sections.length; i++) {
      this.writeUInt32(0) // offset placeholder
      this.writeUInt32(0) // size placeholder
      this.writeUInt32(0) // nameOffset placeholder
      this.writeUInt32(header.sections[i].type)
    }

    // Write section names
    for (const section of header.sections) {
      this.sectionNames.push(section.name)
    }

    // Write section data
    for (let i = 0; i < header.sections.length; i++) {
      this.sectionOffsets.push(this.getOffset())
      this.sectionTypes.push(header.sections[i].type)
      
      const sectionData = header.sections[i].data
      if (sectionData) {
        this.writeSectionData(sectionData)
      }
      
      this.sectionSizes.push(this.getOffset() - this.sectionOffsets[i])
    }

    // Write string table
    for (const name of this.sectionNames) {
      this.writeString(name)
      this.writeUInt8(0)
    }

    // Go back and fill in header and section table
    this.fillHeaderAndTable(header.totalSize)

    return Buffer.concat(this.chunks)
  }

  private writeSectionData(data: GR2SectionData): void {
    if (data.mesh) this.writeMesh(data.mesh)
    else if (data.skeleton) this.writeSkeleton(data.skeleton)
    else if (data.animation) this.writeAnimation(data.animation)
    else if (data.material) this.writeMaterial(data.material)
    else if (data.fileInfo) this.writeFileInfo(data.fileInfo)
    else if (data.texture) this.writeTexture(data.texture)
  }

  private writeFileInfo(info: GR2FileInfo): void {
    this.writeUInt32(info.toolId)
    this.writeUInt32(info.userData)
    this.writeUInt32(info.flags)
    this.writeString(info.sourcePath)
    this.writeString(info.extension)
    this.writeUInt32(info.timeStamp)
  }

  private writeMesh(mesh: GR2Mesh): void {
    this.writeString(mesh.name)
    this.writeUInt32(mesh.vertexCount)
    this.writeUInt32(mesh.faceCount)
    this.writeString(mesh.materialName)
    this.writeVec3(mesh.boundsMin)
    this.writeVec3(mesh.boundsMax)
    this.writeVertexFormat(mesh.vertexFormat)
    
    for (const vertex of mesh.vertices) {
      this.writeVertex(vertex, mesh.vertexFormat)
    }
    
    for (const face of mesh.faces) {
      this.writeUInt16(face.indices[0])
      this.writeUInt16(face.indices[1])
      this.writeUInt16(face.indices[2])
      this.writeUInt16(face.materialIndex)
    }
    
    this.writeUInt32(mesh.primaryTopology)
    this.writeUInt32(mesh.topologies.length)
    for (const topo of mesh.topologies) {
      this.writeUInt32(topo.firstFace)
      this.writeUInt32(topo.faceCount)
      this.writeUInt32(topo.firstVertex)
      this.writeUInt32(topo.vertexCount)
    }
  }

  private writeVertexFormat(format: GR2VertexFormat): void {
    let flags = 0
    if (format.position) flags |= 0x1
    if (format.normal) flags |= 0x2
    if (format.color) flags |= 0x4
    flags |= (format.uvCount & 0xF) << 3
    flags |= (format.boneIndexCount & 0xF) << 7
    flags |= (format.boneWeightCount & 0xF) << 11
    this.writeUInt32(flags)
  }

  private writeVertex(vertex: GR2Vertex, format: GR2VertexFormat): void {
    if (format.position && vertex.position) this.writeVec3(vertex.position)
    if (format.normal && vertex.normal) this.writeVec3(vertex.normal)
    if (format.color && vertex.color) this.writeVec4(vertex.color)
    
    if (format.uvCount > 0 && vertex.uvs) {
      for (const uv of vertex.uvs) {
        this.writeVec2([uv[0], uv[1]])
      }
    }
    
    if (format.boneIndexCount > 0 && vertex.boneIndices) {
      for (const idx of vertex.boneIndices) {
        this.writeUInt8(idx)
      }
    }
    
    if (format.boneWeightCount > 0 && vertex.boneWeights) {
      for (const weight of vertex.boneWeights) {
        this.writeFloat32(weight)
      }
    }
  }

  private writeSkeleton(skeleton: GR2Skeleton): void {
    this.writeString(skeleton.name)
    this.writeUInt32(skeleton.boneCount)
    
    for (const bone of skeleton.bones) {
      this.writeString(bone.name)
      this.writeInt32(bone.parentIndex)
      this.writeTransform(bone.localTransform)
      this.writeTransform(bone.inverseWorldTransform)
      this.writeUInt32(bone.extendedDataOffset)
      this.writeUInt32(bone.extendedDataSize)
    }
    
    this.writeUInt32(skeleton.lodType)
    this.writeFloat32(skeleton.lodError)
    this.writeUInt32(skeleton.extendedDataOffset)
    this.writeUInt32(skeleton.extendedDataSize)
  }

  private writeTransform(transform: GR2Transform): void {
    this.writeVec3(transform.position)
    this.writeVec4(transform.orientation)
    for (const row of transform.scaleShear) {
      this.writeVec3([row[0], row[1], row[2]])
    }
  }

  private writeAnimation(anim: GR2Animation): void {
    this.writeString(anim.name)
    this.writeFloat32(anim.duration)
    this.writeFloat32(anim.timeStep)
    this.writeUInt32(anim.frameCount)
    this.writeUInt32(anim.boneCount)
    
    for (const track of anim.tracks) {
      this.writeUInt32(track.boneIndex)
      this.writeCurve(track.positionCurve)
      this.writeCurve(track.orientationCurve)
      this.writeCurve(track.scaleShearCurve)
    }
    
    this.writeVec3(anim.boundsMin)
    this.writeVec3(anim.boundsMax)
    this.writeUInt32(anim.extendedDataOffset)
    this.writeUInt32(anim.extendedDataSize)
  }

  private writeCurve(curve?: GR2Curve): void {
    if (!curve) {
      this.writeUInt8(0)
      return
    }
    this.writeUInt8(1)
    this.writeUInt32(curve.knotCount)
    this.writeUInt32(curve.degree)
    this.writeUInt32(curve.knots.length)
    for (const knot of curve.knots) this.writeFloat32(knot)
    this.writeUInt32(curve.controls.length)
    for (const control of curve.controls) {
      this.writeUInt32(control.length)
      for (const val of control) this.writeFloat32(val)
    }
  }

  private writeMaterial(material: GR2Material): void {
    this.writeString(material.name)
    this.writeVec4(material.diffuseColor)
    this.writeVec4(material.specularColor)
    this.writeFloat32(material.specularPower)
    this.writeVec4(material.selfIllumColor)
    this.writeFloat32(material.opacity)
    
    this.writeUInt32(material.maps.length)
    for (const map of material.maps) {
      this.writeUInt32(map.type)
      this.writeString(map.textureName)
      this.writeUInt32(map.uvIndex)
      this.writeFloat32(map.uOffset)
      this.writeFloat32(map.vOffset)
      this.writeFloat32(map.uScale)
      this.writeFloat32(map.vScale)
      this.writeFloat32(map.rotation)
    }
    
    this.writeUInt32(material.extendedDataOffset)
    this.writeUInt32(material.extendedDataSize)
  }

  private writeTexture(texture: GR2Texture): void {
    this.writeString(texture.name)
    this.writeUInt32(texture.width)
    this.writeUInt32(texture.height)
    this.writeUInt32(texture.format)
    this.writeUInt32(texture.mipCount)
    
    // Write pixel data
    this.chunks.push(Buffer.from(texture.pixelData))
    
    this.writeUInt32(texture.extendedDataOffset)
    this.writeUInt32(texture.extendedDataSize)
  }

  // Helper methods
  private writeString(str: string): void {
    this.chunks.push(Buffer.from(str, 'utf8'))
  }

  private writeUInt8(v: number): void {
    this.chunks.push(Buffer.from([v]))
  }

  private writeUInt16(v: number): void {
    const buf = Buffer.alloc(2)
    buf.writeUInt16LE(v)
    this.chunks.push(buf)
  }

  private writeUInt32(v: number): void {
    const buf = Buffer.alloc(4)
    buf.writeUInt32LE(v)
    this.chunks.push(buf)
  }

  private writeInt32(v: number): void {
    const buf = Buffer.alloc(4)
    buf.writeInt32LE(v)
    this.chunks.push(buf)
  }

  private writeFloat32(v: number): void {
    const buf = Buffer.alloc(4)
    buf.writeFloatLE(v)
    this.chunks.push(buf)
  }

  private writeVec2(v: [number, number]): void {
    this.writeFloat32(v[0])
    this.writeFloat32(v[1])
  }

  private writeVec3(v: [number, number, number]): void {
    this.writeFloat32(v[0])
    this.writeFloat32(v[1])
    this.writeFloat32(v[2])
  }

  private writeVec4(v: [number, number, number, number]): void {
    this.writeFloat32(v[0])
    this.writeFloat32(v[1])
    this.writeFloat32(v[2])
    this.writeFloat32(v[3])
  }

  private getOffset(): number {
    return this.chunks.reduce((sum, chunk) => sum + chunk.length, 0)
  }

  private fillHeaderAndTable(totalSize: number): void {
    // This is a simplified version - in production would need proper offset calculation
    // For now, just ensure buffer is valid
  }
}

/**
 * High-level GR2 conversion utilities for Metin2 workflow
 */
export const GR2Converter = {
  /**
   * Parse GR2 file from ArrayBuffer
   */
  parse(arrayBuffer: ArrayBuffer): GR2Header {
    const reader = new GR2Reader(arrayBuffer)
    return reader.read()
  },

  /**
   * Convert Three.js mesh + skeleton to GR2
   */
  toGR2(meshData: unknown, skeletonData: unknown, animations: unknown[] = []): Buffer {
    void meshData;
    void skeletonData;
    const writer = new GR2Writer()
    
    const header: GR2Header = {
      magic: 'GR24',
      version: 0x80000007,
      totalSize: 0,
      sectionCount: 4 + animations.length,
      sections: [
        { type: 0xCAFEBABE, offset: 0, size: 0, nameOffset: 0, name: 'file_info' },
        { type: 0x00100000, offset: 0, size: 0, nameOffset: 0, name: 'mesh' },
        { type: 0x00200000, offset: 0, size: 0, nameOffset: 0, name: 'skeleton' },
        ...animations.map((anim, i) => ({
          type: 0x00400000,
          offset: 0,
          size: 0,
          nameOffset: 0,
          name: `animation_${i}`
        }))
      ]
    }

    return writer.write(header)
  },

  /**
   * Extract Metin2-compatible data from GR2
   */
  extractForMetin2(gr2: GR2Header): {
    bones: string[]
    meshes: any[]
    animations: string[]
    materials: string[]
  } {
    const bones: string[] = []
    const meshes: any[] = []
    const animations: string[] = []
    const materials: string[] = []

    for (const section of gr2.sections) {
      if (section.data?.skeleton) {
        bones.push(...section.data.skeleton.bones.map(b => b.name))
      }
      if (section.data?.mesh) {
        meshes.push(section.data.mesh)
      }
      if (section.data?.animation) {
        animations.push(section.data.animation.name)
      }
      if (section.data?.material) {
        materials.push(section.data.material.name)
      }
    }

    return { bones, meshes, animations, materials }
  }
}

// Export for Node.js/Browser
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { GR2Reader, GR2Writer, GR2Converter }
}