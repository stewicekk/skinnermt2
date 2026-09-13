import requests
import numpy as np
from typing import List

def batch_infer(endpoint: str, bones: List[str], vertices: List[List[float]], batch_size=4096):
    n = len(vertices)
    out_weights = []
    i = 0
    while i < n:
        chunk = vertices[i:i+batch_size]
        payload = {'bones': bones, 'vertices': chunk}
        r = requests.post(endpoint, json=payload, timeout=60)
        r.raise_for_status()
        j = r.json()
        out_weights.extend(j['weights'])
        i += batch_size
    return out_weights

if __name__ == '__main__':
    # demo
    endpoint = 'http://127.0.0.1:9000/ai_skin'
    bones = ['b' + str(i) for i in range(8)]
    verts = [[0.0,0.0,0.0],[1.0,0.0,0.0],[-1.0,0.0,0.0]]
    print(batch_infer(endpoint, bones, verts))
