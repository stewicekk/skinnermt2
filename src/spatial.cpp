// Deterministic KD-tree over 3D points. Contract in m2rig/spatial.hpp.
// Build: recursive median split on the longest axis (fixed x>y>z priority
// on extent ties) over a total (coordinate, index) order — fully
// deterministic across compilers (no nth_element, no RNG).
// Queries return exact top-k/radius sets matching a brute-force scan with
// first-found-wins ties (distances compare via distSq, which orders
// identically to sqrt distances; index breaks ties on both paths).
#include "m2rig/spatial.hpp"

#include <algorithm>
#include <limits>

namespace m2rig {

KdTree::KdTree(const std::vector<Vec3>& points) : points_(points) {
    if (points_.empty()) return;
    std::vector<std::uint32_t> ids(points_.size());
    for (std::uint32_t i = 0; i < ids.size(); ++i) ids[i] = i;
    nodes_.reserve(points_.size());
    root_ = build(ids, 0, ids.size(), 0);
}

int KdTree::build(std::vector<std::uint32_t>& ids, std::size_t begin, std::size_t end,
                  int depth) {
    (void)depth;
    if (begin >= end) return -1;
    // Longest axis of this range (fixed priority on ties).
    Vec3 lo = points_[ids[begin]], hi = lo;
    for (std::size_t i = begin + 1; i < end; ++i) {
        const Vec3& p = points_[ids[i]];
        if (p.x < lo.x) lo.x = p.x;
        if (p.y < lo.y) lo.y = p.y;
        if (p.z < lo.z) lo.z = p.z;
        if (p.x > hi.x) hi.x = p.x;
        if (p.y > hi.y) hi.y = p.y;
        if (p.z > hi.z) hi.z = p.z;
    }
    const float ex = hi.x - lo.x, ey = hi.y - lo.y, ez = hi.z - lo.z;
    std::uint8_t axis = 0;
    if (ey > ex && ey >= ez)
        axis = 1;
    else if (ez > ex && ez > ey)
        axis = 2;
    std::sort(ids.begin() + static_cast<std::ptrdiff_t>(begin),
              ids.begin() + static_cast<std::ptrdiff_t>(end),
              [&](std::uint32_t a, std::uint32_t b) {
                  float ca = axis == 0 ? points_[a].x : (axis == 1 ? points_[a].y : points_[a].z);
                  float cb = axis == 0 ? points_[b].x : (axis == 1 ? points_[b].y : points_[b].z);
                  // Non-finite coordinates sort last: keeps a total order
                  // even for NaN (std::sort UB otherwise) and such points
                  // can never win a strict downstream comparison — the same
                  // graceful degradation the brute-force scan had.
                  if (!isFiniteF(ca)) ca = std::numeric_limits<float>::infinity();
                  if (!isFiniteF(cb)) cb = std::numeric_limits<float>::infinity();
                  if (ca != cb) return ca < cb;
                  return a < b;
              });
    const std::size_t mid = begin + (end - begin) / 2;
    const int self = static_cast<int>(nodes_.size());
    nodes_.push_back(Node{});
    nodes_[static_cast<std::size_t>(self)].point = ids[mid];
    nodes_[static_cast<std::size_t>(self)].axis = axis;
    nodes_[static_cast<std::size_t>(self)].left = build(ids, begin, mid, depth + 1);
    nodes_[static_cast<std::size_t>(self)].right = build(ids, mid + 1, end, depth + 1);
    return self;
}

std::vector<KnnHit> KdTree::query(const Vec3& q, std::size_t k) const {
    std::vector<KnnHit> best;
    if (nodes_.empty() || k == 0) return best;
    best.reserve(k < 8 ? k : 8);
    float worstSq = std::numeric_limits<float>::infinity();
    queryRec(root_, q, k, best, worstSq);
    return best;
}

void KdTree::queryRec(int node, const Vec3& q, std::size_t k, std::vector<KnnHit>& best,
                      float& worstSq) const {
    if (node < 0) return;
    const Node& nd = nodes_[static_cast<std::size_t>(node)];
    const Vec3& p = points_[nd.point];
    const float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
    const float d = dx * dx + dy * dy + dz * dz;
    // Non-finite distances never insert (mirrors the brute-force scan, whose
    // strict comparisons all fail on NaN): NaN points/queries degrade to
    // empty results, never to crashes or NaN hits. Traversal continues —
    // finite points may live under a NaN split.
    if (isFiniteF(d)) {
        std::size_t pos = 0;
        while (pos < best.size() &&
               (best[pos].distSq < d || (best[pos].distSq == d && best[pos].index < nd.point)))
            ++pos;
        if (pos < k) {
            best.insert(best.begin() + static_cast<std::ptrdiff_t>(pos), KnnHit{nd.point, d});
            if (best.size() > k) best.pop_back();
            if (best.size() >= k) worstSq = best.back().distSq;
        }
    }
    const float diff = nd.axis == 0 ? dx : (nd.axis == 1 ? dy : dz);
    const int nearNode = diff < 0.0f ? nd.left : nd.right;
    const int farNode = diff < 0.0f ? nd.right : nd.left;
    queryRec(nearNode, q, k, best, worstSq);
    // Prune only on a proven bound (== <= for finite inputs, so
    // equal-distance smaller indices are still explored); a non-finite diff
    // explores both sides instead of hiding neighbors behind NaN.
    if (!(diff * diff > worstSq)) queryRec(farNode, q, k, best, worstSq);
}

std::vector<KnnHit> KdTree::queryRadius(const Vec3& q, float radius) const {
    std::vector<KnnHit> out;
    if (nodes_.empty() || !(radius > 0.0f)) return out;
    radiusRec(root_, q, radius * radius, out);
    std::sort(out.begin(), out.end(), [](const KnnHit& a, const KnnHit& b) {
        if (a.distSq != b.distSq) return a.distSq < b.distSq;
        return a.index < b.index;
    });
    return out;
}

void KdTree::radiusRec(int node, const Vec3& q, float radiusSq,
                       std::vector<KnnHit>& out) const {
    if (node < 0) return;
    const Node& nd = nodes_[static_cast<std::size_t>(node)];
    const Vec3& p = points_[nd.point];
    const float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
    const float d = dx * dx + dy * dy + dz * dz;
    if (d < radiusSq) out.push_back(KnnHit{nd.point, d});
    const float diff = nd.axis == 0 ? dx : (nd.axis == 1 ? dy : dz);
    const int nearNode = diff < 0.0f ? nd.left : nd.right;
    const int farNode = diff < 0.0f ? nd.right : nd.left;
    radiusRec(nearNode, q, radiusSq, out);
    // Same proven-bound rule as query (== < for finite inputs).
    if (!(diff * diff >= radiusSq)) radiusRec(farNode, q, radiusSq, out);
}

}  // namespace m2rig
