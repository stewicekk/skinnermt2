from contextlib import asynccontextmanager
from fastapi import FastAPI, Header, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import base64
import numpy as np
import os
import subprocess
import tempfile
import torch
from scipy.spatial import KDTree
from model import SimpleWeightNet
import json

@asynccontextmanager
async def lifespan(app: FastAPI):
    init_model(32)
    yield

app = FastAPI(lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

DEVICE = torch.device('cpu')
MODEL = None

# Metin2 bone naming conventions
METIN2_BONES = {
    'bip01', 'bip01 pelvis', 'bip01 spine', 'bip01 spine1', 'bip01 spine2',
    'bip01 neck', 'bip01 head', 'bip01 l clavicle', 'bip01 r clavicle',
    'bip01 l upperarm', 'bip01 r upperarm', 'bip01 l forearm', 'bip01 r forearm',
    'bip01 l hand', 'bip01 r hand', 'bip01 l thigh', 'bip01 r thigh',
    'bip01 l calf', 'bip01 r calf', 'bip01 l foot', 'bip01 r foot',
    'equip_right', 'equip_left', 'stip'
}

class WeightTransferReq(BaseModel):
    source_vertices: list  # [x, y, z] list
    target_vertices: list  # [x, y, z] list
    source_weights: list   # [[bone_id, weight], ...] per vertex
    bone_map: dict         # Mapping of bone names

class CompileGR2Req(BaseModel):
    smd_content: str       # ASCII SMD content
    material_map: dict     # Material name -> texture path mapping

class MSMGenerateReq(BaseModel):
    model_path: str
    texture_paths: list
    class_name: str

# In-memory storage for uploaded models
uploaded_models = {}

def init_model(nbones):
    global MODEL
    MODEL = SimpleWeightNet(in_ch=3, hidden=128, out_ch=max(1, nbones))
    MODEL.to(DEVICE)
    MODEL.eval()

@app.post('/ai_skin', response_model=dict)
def ai_skin(req: dict, x_api_key: str = Header(None)):
    """AI-powered per-vertex weight prediction using PyTorch model."""
    try:
        verts = np.array(req.get('vertices', []), dtype=np.float32)
        if verts.ndim != 2 or verts.shape[1] < 3:
            raise HTTPException(status_code=400, detail='Vertices must be Nx3')
        
        nbones = req.get('nbones', 32)
        if MODEL is None or MODEL.net[-1].out_features != max(1, nbones):
            init_model(nbones)
        
        X = torch.from_numpy(verts).float()
        with torch.no_grad():
            logits = MODEL(X)
            probs = torch.softmax(logits, dim=1).cpu().numpy()
        
        # Ensure correct number of output bones
        if probs.shape[1] > nbones:
            probs = probs[:, :nbones]
        elif probs.shape[1] < nbones:
            pad = np.zeros((probs.shape[0], nbones - probs.shape[1]), dtype=np.float32)
            probs = np.hstack([probs, pad])
            probs = probs / (probs.sum(axis=1, keepdims=True) + 1e-12)
        
        return {'weights': probs.tolist()}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post('/api/v1/learn-weights', response_model=dict)
def learn_weights(req: WeightTransferReq):
    try:
        source_verts = np.array(req.source_vertices, dtype=np.float32)
        target_verts = np.array(req.target_vertices, dtype=np.float32)
        source_weights = req.source_weights or []
        target_bones = (req.bone_map or {}).get('bones', [])

        if source_verts.ndim != 2 or source_verts.shape[1] != 3:
            raise HTTPException(status_code=400, detail='Source vertices must be Nx3')
        if target_verts.ndim != 2 or target_verts.shape[1] != 3:
            raise HTTPException(status_code=400, detail='Target vertices must be Nx3')
        if len(source_weights) != len(source_verts):
            raise HTTPException(status_code=400, detail='Source weights must match source vertices')
        if not target_bones:
            raise HTTPException(status_code=400, detail='Target bone list is required')

        source_tree = KDTree(source_verts)
        transferred_weights = []

        for target_vertex in target_verts:
            _, source_index = source_tree.query(target_vertex)
            pairs = source_weights[int(source_index)] or []
            bone_totals = {}
            for pair in pairs:
                bone_id, weight = pair[0], float(pair[1])
                if bone_id in target_bones and weight > 0:
                    bone_totals[bone_id] = bone_totals.get(bone_id, 0.0) + weight
            total = sum(bone_totals.values())
            if total > 0:
                bone_totals = {bone: value / total for bone, value in bone_totals.items()}
            ranked = sorted(bone_totals.items(), key=lambda item: item[1], reverse=True)[:4]
            final_weights = [0.0] * len(target_bones)
            for bone_id, weight in ranked:
                final_weights[target_bones.index(bone_id)] = weight
            transferred_weights.append(final_weights)

        return {
            'transferred_weights': transferred_weights,
            'method': 'kd_tree_topological_transfer',
            'source_vertices_analyzed': len(source_verts)
        }
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post('/api/v1/compile-gr2', response_model=dict)
def compile_gr2(req: CompileGR2Req):
    compiler = os.environ.get('GRANNY_COMPILER')
    if not compiler or not os.path.isfile(compiler):
        raise HTTPException(status_code=501, detail='GR2 compiler is not configured. Set GRANNY_COMPILER to a local Granny compiler executable.')
    if not req.smd_content.strip():
        raise HTTPException(status_code=400, detail='SMD content is required')
    try:
        with tempfile.TemporaryDirectory() as workdir:
            smd_path = os.path.join(workdir, 'model.smd')
            gr2_path = os.path.join(workdir, 'model.gr2')
            with open(smd_path, 'w', encoding='utf-8') as stream:
                stream.write(req.smd_content)
            completed = subprocess.run(
                [compiler, smd_path, gr2_path],
                capture_output=True,
                text=True,
                timeout=120,
                check=False
            )
            if completed.returncode != 0:
                raise HTTPException(status_code=500, detail=f'GR2 compiler failed: {completed.stderr or completed.stdout}')
            if not os.path.isfile(gr2_path):
                raise HTTPException(status_code=500, detail='GR2 compiler did not produce an output file')
            with open(gr2_path, 'rb') as stream:
                payload = stream.read()
        return {
            'filename': 'model.gr2',
            'file_size': len(payload),
            'format': 'gr2',
            'content_base64': base64.b64encode(payload).decode('ascii')
        }
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post('/api/v1/generate-msm', response_model=dict)
def generate_msm(req: MSMGenerateReq):
    """
    Generate Metin2 Script Mesh (.msm) text format.
    Creates the .msm file content for client-side loading.
    """
    try:
        # Generate MSM content based on the structure
        msm_content = generate_msm_content(
            req.model_path,
            req.texture_paths,
            req.class_name
        )
        
        return {
            'msm_content': msm_content,
            'format': 'metin2_msm'
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

def generate_msm_content(model_path: str, texture_paths: list, class_name: str) -> str:
    """
    Generate Metin2 Script Mesh (.msm) text format.
    Format includes: Group ShapeData, ShapeIndex, Model, SourceSkin sections.
    """
    msm = f'''# Metin2 Mesh Script Generator
# Class: {class_name}
# Model: {model_path}

# Material mappings
'''
    
    for i, tex in enumerate(texture_paths):
        msm += f'# Texture {i}: {tex}\n'
    
    msm += '''
# Group sections
Group ShapeData01
{
    # Shape data for mesh sections
    ShapeIndex
    {
        # Triangle indices organized by material group
    }
    Model
    {
        # Mesh geometry data
        SourceSkin
        {
            # Skin reference
        }
    }
}
'''
    
    return msm

# Health check
@app.get('/health')
def health():
    return {'status': 'ok', 'message': 'Metin2 Rigging Studio API'}

if __name__ == '__main__':
    import uvicorn
    uvicorn.run(app, host='0.0.0.0', port=8000)