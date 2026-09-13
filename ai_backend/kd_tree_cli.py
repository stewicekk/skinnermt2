import json, sys, argparse
from pathlib import Path

def read_verts(path):
    j = json.load(open(path))
    return j.get('vertices', [])

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--in', required=True, help='input vertices JSON')
    parser.add_argument('--out', required=True, help='output pairs JSON')
    parser.add_argument('--tol', type=float, default=0.02)
    args = parser.parse_args()
    verts = read_verts(args.in)
    pts = verts
    pairs = []
    n = len(pts)
    for i in range(n):
        xi, yi, zi = pts[i]
        best = None; bd = 1e9
        for j in range(n):
            if i==j: continue
            xj,yj,zj = pts[j]
            if abs(xi + xj) <= args.tol:
                d = (yi - yj)**2 + (zi - zj)**2
                if d < bd:
                    bd = d; best = j
        if best is not None:
            pairs.append([i+1, best+1])
    json.dump(pairs, open(args.out,'w'))
    print('Wrote', args.out, 'pairs:', len(pairs))
