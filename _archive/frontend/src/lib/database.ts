export interface ProjectRecord {
  id: string;
  name: string;
  description?: string;
  targetClass: string;
  metadata: Record<string, unknown>;
  version: number;
  isPublic: boolean;
  ownerId: string;
  createdAt: string;
  updatedAt: string;
}

export interface ProjectVersionRecord {
  id: string;
  projectId: string;
  version: number;
  commitMessage: string;
  data: Record<string, unknown>;
  authorId: string;
  parentVersionId?: string;
  createdAt: string;
}

export interface ProjectFilters {
  targetClass?: string;
  isPublic?: boolean;
  search?: string;
}

export interface ProjectRepository {
  findById(id: string): Promise<ProjectRecord | null>;
  findByOwner(ownerId: string): Promise<ProjectRecord[]>;
  findPublic(limit: number, offset: number): Promise<ProjectRecord[]>;
  create(data: Partial<ProjectRecord>): Promise<ProjectRecord>;
  update(id: string, data: Partial<ProjectRecord>): Promise<ProjectRecord>;
  remove(id: string): Promise<void>;
  search(query: string, filters: ProjectFilters): Promise<ProjectRecord[]>;
}

export interface VersionRepository {
  findByProject(projectId: string): Promise<ProjectVersionRecord[]>;
  findById(id: string): Promise<ProjectVersionRecord | null>;
  create(data: Partial<ProjectVersionRecord>): Promise<ProjectVersionRecord>;
  getLatest(projectId: string): Promise<ProjectVersionRecord | null>;
}

const PROJECT_PREFIX = "metin2_studio_project_";
const VERSION_PREFIX = "metin2_studio_versions_";

function projectKey(id: string): string {
  return `${PROJECT_PREFIX}${id}`;
}

function versionKey(projectId: string): string {
  return `${VERSION_PREFIX}${projectId}`;
}

function projectStorageKeys(): string[] {
  const keys: string[] = [];
  for (let i = 0; i < localStorage.length; i += 1) {
    const key = localStorage.key(i);
    if (key && key.startsWith(PROJECT_PREFIX)) keys.push(key);
  }
  return keys;
}

function readProject(key: string): ProjectRecord {
  return JSON.parse(localStorage.getItem(key) ?? "{}") as ProjectRecord;
}

function readVersions(projectId: string): ProjectVersionRecord[] {
  return JSON.parse(localStorage.getItem(versionKey(projectId)) ?? "[]") as ProjectVersionRecord[];
}

export class LocalStorageProjectDatabase implements ProjectRepository, VersionRepository {
  async findById(id: string): Promise<ProjectRecord | null>;
  async findById(id: string): Promise<ProjectVersionRecord | null>;
  async findById(id: string): Promise<ProjectRecord | ProjectVersionRecord | null> {
    const stored = localStorage.getItem(projectKey(id));
    if (stored) return JSON.parse(stored) as ProjectRecord;
    for (let i = 0; i < localStorage.length; i += 1) {
      const key = localStorage.key(i);
      if (key && key.startsWith(VERSION_PREFIX)) {
        const versions = JSON.parse(localStorage.getItem(key) ?? "[]") as ProjectVersionRecord[];
        const found = versions.find((version) => version.id === id);
        if (found) return found;
      }
    }
    return null;
  }

  async findByOwner(ownerId: string): Promise<ProjectRecord[]> {
    return projectStorageKeys()
      .map(readProject)
      .filter((project) => project.ownerId === ownerId)
      .sort((a, b) => Date.parse(b.updatedAt) - Date.parse(a.updatedAt));
  }

  async findPublic(limit: number, offset: number): Promise<ProjectRecord[]> {
    return projectStorageKeys()
      .map(readProject)
      .filter((project) => project.isPublic)
      .sort((a, b) => Date.parse(b.updatedAt) - Date.parse(a.updatedAt))
      .slice(offset, offset + limit);
  }

  async create(data: Partial<ProjectRecord>): Promise<ProjectRecord>;
  async create(data: Partial<ProjectVersionRecord>): Promise<ProjectVersionRecord>;
  async create(data: Partial<ProjectRecord> | Partial<ProjectVersionRecord>): Promise<ProjectRecord | ProjectVersionRecord> {
    if ("projectId" in data && data.projectId) return this.saveVersion(data as Partial<ProjectVersionRecord>);
    return this.saveProject(data as Partial<ProjectRecord>);
  }

  async update(id: string, data: Partial<ProjectRecord>): Promise<ProjectRecord> {
    const project = await this.findProjectRecord(id);
    const updated: ProjectRecord = {
      ...project,
      ...data,
      id: project.id,
      updatedAt: new Date().toISOString()
    };
    localStorage.setItem(projectKey(id), JSON.stringify(updated));
    return updated;
  }

  async remove(id: string): Promise<void> {
    localStorage.removeItem(projectKey(id));
    localStorage.removeItem(versionKey(id));
  }

  async search(query: string, filters: ProjectFilters): Promise<ProjectRecord[]> {
    const normalized = query.trim().toLowerCase();
    return projectStorageKeys()
      .map(readProject)
      .filter((project) => {
        if (filters.targetClass && project.targetClass !== filters.targetClass) return false;
        if (filters.isPublic !== undefined && project.isPublic !== filters.isPublic) return false;
        if (!normalized) return true;
        return [project.name, project.description ?? "", project.targetClass]
          .join(" ")
          .toLowerCase()
          .includes(normalized);
      })
      .sort((a, b) => Date.parse(b.updatedAt) - Date.parse(a.updatedAt));
  }

  async findByProject(projectId: string): Promise<ProjectVersionRecord[]> {
    return readVersions(projectId).sort((a, b) => b.version - a.version);
  }

  async getLatest(projectId: string): Promise<ProjectVersionRecord | null> {
    const versions = await this.findByProject(projectId);
    return versions[0] ?? null;
  }

  private async saveProject(data: Partial<ProjectRecord>): Promise<ProjectRecord> {
    const now = new Date().toISOString();
    const project: ProjectRecord = {
      id: crypto.randomUUID(),
      name: data.name ?? "Untitled project",
      description: data.description,
      targetClass: data.targetClass ?? "warrior",
      metadata: data.metadata ?? {},
      version: 1,
      isPublic: data.isPublic ?? false,
      ownerId: data.ownerId ?? "local-user",
      createdAt: now,
      updatedAt: now
    };
    localStorage.setItem(projectKey(project.id), JSON.stringify(project));
    await this.saveVersion({
      projectId: project.id,
      version: 1,
      commitMessage: "Initial project snapshot",
      data: { name: project.name, targetClass: project.targetClass },
      authorId: project.ownerId
    });
    return project;
  }

  private async saveVersion(data: Partial<ProjectVersionRecord>): Promise<ProjectVersionRecord> {
    if (!data.projectId) throw new Error("Project ID is required to create a version.");
    const versions = readVersions(data.projectId);
    const version: ProjectVersionRecord = {
      id: crypto.randomUUID(),
      projectId: data.projectId,
      version: data.version ?? versions.length + 1,
      commitMessage: data.commitMessage ?? "Project snapshot",
      data: data.data ?? {},
      authorId: data.authorId ?? "local-user",
      parentVersionId: data.parentVersionId,
      createdAt: new Date().toISOString()
    };
    versions.push(version);
    localStorage.setItem(versionKey(data.projectId), JSON.stringify(versions));
    return version;
  }

  private async findProjectRecord(id: string): Promise<ProjectRecord> {
    const project = await this.findById(id);
    if (!project || !("ownerId" in project)) throw new Error("Project not found.");
    return project;
  }
}

let database: LocalStorageProjectDatabase | null = null;

export function getLocalProjectDatabase(): LocalStorageProjectDatabase {
  if (!database) database = new LocalStorageProjectDatabase();
  return database;
}
