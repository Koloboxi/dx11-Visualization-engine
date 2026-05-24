#pragma once
#include <Windows.h>
#include <DirectXMath.h>
#include <vector>
#include <memory>
#include <cmath>
#include <cfloat>
#include <ctime>
#include <random>
#include <algorithm>
#include <numeric>

using namespace DirectX;

// State for the Persistent Homology demo scene.
// Stores point cloud, generation params, animation radius, topology data,
// and per-scene ImGui window open/position state (serialized to JSON).
struct PHSceneState {

    // ── Point cloud ────────────────────────────────────────────────────────────
    std::vector<XMFLOAT3> cloudPoints;

    // ── Generation parameters ──────────────────────────────────────────────────
    int   pointCount  = 20;
    float bounds      = 200.0f;   // half-extent for uniform distribution
    int   distribType = 0;        // 0=uniform box  1=Gaussian  2=sphere surface
    float gaussSigma  = 80.0f;
    float sphereR     = 120.0f;   // radius for sphere-surface distribution

    // ── Radius animation ───────────────────────────────────────────────────────
    float currentRadius = 0.0f;
    std::shared_ptr<float> radiusShared; // written by Scene::UpdateTime, read by updater lambdas
    float radiusSpeed   = 25.0f;         // scene-units per second

    // ── Primitive IDs (to locate spheres/points inside scene.primitives) ───────
    std::vector<UINT> sphereIds;
    std::vector<UINT> pointIds;

    // ── Topology (recomputed each frame / on slider drag, NOT serialized) ──────
    int   beta0        = 0;   // connected components
    int   beta1        = 0;   // independent 1-cycles (loops) in 1-skeleton
    int   numEdges     = 0;
    int   numTriangles = 0;
    float coverageR    = 0.0f; // min radius for beta0 == 1 (full connectivity)
    bool  allConnected = false;

    // ── Scene window states (serialized to JSON) ───────────────────────────────
    bool  genWindowOpen    = true;
    bool  topoWindowOpen   = true;
    float genWindowPos[2]  = {  50.f, 370.f };
    float genWindowSize[2] = { 300.f, 235.f };
    float topoWindowPos[2]  = { 365.f, 370.f };
    float topoWindowSize[2] = { 300.f, 235.f };
    bool  windowsNeedRestore = false; // set true after LoadScene, cleared after first render

    // ── Helpers ────────────────────────────────────────────────────────────────

    void GenerateRandom(unsigned seed = 0) {
        if (seed == 0) seed = (unsigned)time(nullptr) ^ (unsigned)(uintptr_t)this;
        cloudPoints.clear();
        std::mt19937 rng(seed);

        switch (distribType) {
        case 0: { // uniform box
            std::uniform_real_distribution<float> d(-bounds, bounds);
            for (int i = 0; i < pointCount; ++i)
                cloudPoints.push_back({ d(rng), d(rng), d(rng) });
            break;
        }
        case 1: { // Gaussian
            std::normal_distribution<float> d(0.f, gaussSigma);
            for (int i = 0; i < pointCount; ++i)
                cloudPoints.push_back({ d(rng), d(rng), d(rng) });
            break;
        }
        case 2: { // sphere surface
            std::normal_distribution<float> d(0.f, 1.f);
            for (int i = 0; i < pointCount; ++i) {
                float x = d(rng), y = d(rng), z = d(rng);
                float len = sqrtf(x*x + y*y + z*z);
                if (len < 1e-6f) len = 1.f;
                cloudPoints.push_back({ x/len*sphereR, y/len*sphereR, z/len*sphereR });
            }
            break;
        }
        }
        RecomputeCoverageR();
    }

    // Compute VR 1-skeleton topology at given radius r.
    // Two points are connected when distance(p_i, p_j) <= 2r (spheres overlap).
    void ComputeTopology(float r) {
        int N = (int)cloudPoints.size();
        if (N == 0) {
            beta0 = beta1 = numEdges = numTriangles = 0;
            allConnected = false;
            return;
        }

        std::vector<int> parent(N);
        std::iota(parent.begin(), parent.end(), 0);
        auto find = [&](int x) -> int {
            while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
            return x;
        };

        numEdges = 0;
        numTriangles = 0;
        const float r2 = 4.f * r * r; // (2r)^2

        // Build 1-skeleton and union-find
        for (int i = 0; i < N; ++i)
            for (int j = i + 1; j < N; ++j) {
                float dx = cloudPoints[i].x - cloudPoints[j].x;
                float dy = cloudPoints[i].y - cloudPoints[j].y;
                float dz = cloudPoints[i].z - cloudPoints[j].z;
                if (dx*dx + dy*dy + dz*dz <= r2) {
                    int pi = find(i), pj = find(j);
                    if (pi != pj) parent[pi] = pj;
                    numEdges++;
                }
            }

        // Count triangles (all triples with pairwise distance <= 2r)
        for (int i = 0; i < N; ++i)
            for (int j = i + 1; j < N; ++j)
                for (int k = j + 1; k < N; ++k) {
                    auto d2 = [&](int a, int b) -> float {
                        float dx = cloudPoints[a].x - cloudPoints[b].x;
                        float dy = cloudPoints[a].y - cloudPoints[b].y;
                        float dz = cloudPoints[a].z - cloudPoints[b].z;
                        return dx*dx + dy*dy + dz*dz;
                    };
                    if (d2(i,j) <= r2 && d2(i,k) <= r2 && d2(j,k) <= r2)
                        numTriangles++;
                }

        beta0 = 0;
        for (int i = 0; i < N; ++i) if (find(i) == i) beta0++;
        beta1 = numEdges - N + beta0;
        if (beta1 < 0) beta1 = 0;
        allConnected = (beta0 <= 1);
    }

    // Prim's MST to find the maximum spanning-tree edge length.
    // coverageR = maxEdge/2: when radius reaches this, all points connect.
    void RecomputeCoverageR() {
        int N = (int)cloudPoints.size();
        if (N < 2) { coverageR = 0.f; return; }

        std::vector<float> key(N, FLT_MAX);
        std::vector<bool>  inMST(N, false);
        float maxEdge = 0.f;
        key[0] = 0.f;

        for (int iter = 0; iter < N; ++iter) {
            int u = -1;
            for (int i = 0; i < N; ++i)
                if (!inMST[i] && (u < 0 || key[i] < key[u])) u = i;
            inMST[u] = true;
            if (key[u] < FLT_MAX) maxEdge = std::max(maxEdge, key[u]);

            for (int v = 0; v < N; ++v) {
                if (inMST[v]) continue;
                float dx = cloudPoints[u].x - cloudPoints[v].x;
                float dy = cloudPoints[u].y - cloudPoints[v].y;
                float dz = cloudPoints[u].z - cloudPoints[v].z;
                float d  = sqrtf(dx*dx + dy*dy + dz*dz);
                if (d < key[v]) key[v] = d;
            }
        }
        coverageR = maxEdge / 2.f;
    }
};
