export interface LearnWeightsRequest {
  source_vertices: number[][];
  target_vertices: number[][];
  source_weights: Array<Array<[string, number]>>;
  bone_map: { bones: string[] };
}

export interface LearnWeightsResponse {
  transferred_weights: number[][];
  method: string;
  source_vertices_analyzed: number;
}

export interface CompileGr2Request {
  smd_content: string;
  material_map: Record<string, string>;
}

export interface CompileGr2Response {
  filename: string;
  file_size: number;
  format: string;
  content_base64: string;
}

export interface GenerateMsmRequest {
  model_path: string;
  texture_paths: string[];
  class_name: string;
}

export interface GenerateMsmResponse {
  msm_content: string;
  format: string;
}

export function apiBaseUrl(): string {
  return import.meta.env.VITE_API_URL ?? "http://localhost:8000";
}

async function postJson<T>(path: string, body: unknown): Promise<T> {
  const response = await fetch(`${apiBaseUrl()}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });
  if (!response.ok) {
    const text = await response.text().catch(() => "");
    throw new Error(`API request failed (${response.status}): ${text || response.statusText}`);
  }
  return (await response.json()) as T;
}

export const learnWeights = (body: LearnWeightsRequest): Promise<LearnWeightsResponse> =>
  postJson<LearnWeightsResponse>("/api/v1/learn-weights", body);

export const compileGr2 = (body: CompileGr2Request): Promise<CompileGr2Response> =>
  postJson<CompileGr2Response>("/api/v1/compile-gr2", body);

export const generateMsm = (body: GenerateMsmRequest): Promise<GenerateMsmResponse> =>
  postJson<GenerateMsmResponse>("/api/v1/generate-msm", body);
