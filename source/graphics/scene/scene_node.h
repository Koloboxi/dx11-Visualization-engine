#pragma once
#include <string>
#include <vector>
#include <algorithm>

class SceneNode {
public:
    virtual ~SceneNode() = default;
    virtual bool IsPrimitive()  const { return false; }
    virtual bool IsController() const { return false; }

    std::string name;
    SceneNode*  parent = nullptr;
    std::vector<SceneNode*> children;

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
