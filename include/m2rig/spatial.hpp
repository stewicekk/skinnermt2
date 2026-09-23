#pragma once
// Deterministic KD-tree over 3D points (Wave 26, zero third-party deps).
// Owns a copy of the points (lifetime-safe). Used by weight transfer
// (kNN donors), mirror pairing (radius search) and anywhere else a brute
// O(n*m) point loop appears. Contract:
//  - query() returns the exact top-k by (distSq, index): bit-identical to a
//    full brute-force scan with first-found-wins ties (distances compare via
//    distSq, which orders identically to sqrt distances; ties break by the
//    smaller index on both paths).
//  - queryRadius() returns all points with distSq < radius^2 (strict, like
//    the mirror greedy threshold), sorted by (distSq, index).
//  - Fully deterministic: median splits on a total (coord, index) order,
//    fixed axis priority (x > y > z on extent ties), no RNG, no threading.
//    Non-finite coordinates sort last and never match strict queries (mesh
//    validation flags non-finite verts upstream; queries come from the same
//    validated positions).
#include <cstddef>
#include <cstdint>
#include <vector>

#include "m2rig/math.hpp"

namespace m2rig {

struct KnnHit {
    std::uint32_t index = 0;  // position in the indexed point array
    float distSq = 0.0f;      // squared Euclidean distance to the query
};

class KdTree {
public:
    KdTree() = default;
    explicit KdTree(const std::vector<Vec3>& points);

    bool empty() const { return points_.empty(); }
    std::size_t size() const { return points_.size(); }

    // Exact top-k by (distSq, index). k==0 returns empty; k > size returns
    // all points sorted. Allocations may throw bad_alloc (propagates); the
    // queries themselves have no other failure mode.
    std::vector<KnnHit> query(const Vec3& q, std::size_t k) const;
    // All points strictly inside radius, sorted by (distSq, index).
    std::vector<KnnHit> queryRadius(const Vec3& q, float radius) const;

private:
    struct Node {
        std::uint32_t point = 0;
        std::uint8_t axis = 0;
        int left = -1;
        int right = -1;
    };
    std::vector<Vec3> points_;
    std::vector<Node> nodes_;
    int root_ = -1;

    int build(std::vector<std::uint32_t>& ids, std::size_t begin, std::size_t end, int depth);
    void queryRec(int node, const Vec3& q, std::size_t k, std::vector<KnnHit>& best,
                  float& worstSq) const;
    void radiusRec(int node, const Vec3& q, float radiusSq,
                   std::vector<KnnHit>& out) const;
};

}  // namespace m2rig
