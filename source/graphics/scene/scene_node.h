#pragma once
#include <string>
#include <vector>
#include <algorithm>

class Primitive;

class SceneNode {
public:
    virtual ~SceneNode() = default;
    virtual bool IsPrimitive()  const { return false; }
    virtual bool IsController() const { return false; }
    virtual bool IsVertexPointsGroup() const { return false; }

    std::string name;
    SceneNode*  parent = nullptr;
    std::vector<SceneNode*> children;

    // Visibility lives on the base node so empty (non-primitive) grouping nodes
    // participate in the tree's eye toggle and cascade like primitives do.
    bool visible = true;

    void AddChild(SceneNode* n) {
        if (!n) return;
        n->parent = this;
        children.push_back(n);
    }

    void RemoveChild(SceneNode* n) {
        auto it = std::find(children.begin(), children.end(), n);
        if (it != children.end()) {
            (*it)->parent = nullptr;
            children.erase(it);
        }
    }
};

// An empty grouping node that owns the per-vertex marker points of `source`.
// Created hidden and empty; its marker children are generated lazily the first
// time it is made visible (see Scene::SetNodeVisibleCascade).
class VertexPointsGroup : public SceneNode {
public:
    bool IsVertexPointsGroup() const override { return true; }
    Primitive* source = nullptr;
    bool generated = false;
};
