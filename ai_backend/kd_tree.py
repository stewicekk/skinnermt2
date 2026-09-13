# Simple KD-tree for 3D points (pure Python, minimal dependencies)
# Usage: build tree from list of (x,y,z) and query nearest neighbor indices
class KDNode:
    def __init__(self, point=None, idx=None, left=None, right=None, axis=0):
        self.point = point
        self.idx = idx
        self.left = left
        self.right = right
        self.axis = axis

def build_kdtree(points, indices=None, depth=0):
    if not points:
        return None
    k = len(points[0])
    axis = depth % k
    sorted_pts = sorted(zip(points, indices if indices else range(len(points))), key=lambda x: x[0][axis])
    median = len(sorted_pts) // 2
    point, idx = sorted_pts[median]
    left = build_kdtree([p for p,i in sorted_pts[:median]], [i for p,i in sorted_pts[:median]], depth+1)
    right = build_kdtree([p for p,i in sorted_pts[median+1:]], [i for p,i in sorted_pts[median+1:]], depth+1)
    return KDNode(point=point, idx=idx, left=left, right=right, axis=axis)

def nearest_neighbor(root, target, best=None):
    if root is None:
        return best
    point = root.point
    idx = root.idx
    axis = root.axis
    dist = sum((a-b)**2 for a,b in zip(point, target))
    if best is None or dist < best[0]:
        best = (dist, idx, point)
    diff = target[axis] - point[axis]
    close, away = (root.left, root.right) if diff < 0 else (root.right, root.left)
    best = nearest_neighbor(close, target, best)
    if diff*diff < best[0]:
        best = nearest_neighbor(away, target, best)
    return best

if __name__ == '__main__':
    pts = [[0,0,0],[1,0,0],[-1,0,0],[0,1,0]]
    root = build_kdtree(pts)
    print(nearest_neighbor(root, [0.2,0.0,0.0]))
