#include "SemWorkspace.h"
#include "SemSessionDetail.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace SemSessionNS {

using namespace detail;

SemWorkspace& Workspace() {
    static SemWorkspace w;
    return w;
}

// Stand-in handed out while no setup is open, so the SEM window can query a
// session unconditionally. It is never bound, so HasSource() stays false.
static SemSession& EmptySession() {
    static SemSession s;
    return s;
}

SemSession& SemWorkspace::Active() {
    SemSession* s = At(m_active);
    return s ? *s : EmptySession();
}

SemSession* SemWorkspace::At(int i) {
    if (i < 0 || i >= (int)m_sessions.size()) return nullptr;
    return m_sessions[i].get();
}

const SemSession* SemWorkspace::At(int i) const {
    if (i < 0 || i >= (int)m_sessions.size()) return nullptr;
    return m_sessions[i].get();
}

int SemWorkspace::IndexOf(const Primitive* p) const {
    if (!p) return -1;
    for (int i = 0; i < (int)m_sessions.size(); ++i)
        if (m_sessions[i]->SourcePrim() == p) return i;
    return -1;
}

int SemWorkspace::IndexOfWorkDir(const std::string& dir) const {
    if (dir.empty()) return -1;
    std::error_code ec;
    for (int i = 0; i < (int)m_sessions.size(); ++i) {
        const std::string& wd = m_sessions[i]->WorkDir();
        if (wd == dir || (!wd.empty() && fs::equivalent(wd, dir, ec))) return i;
    }
    return -1;
}

SemSession* SemWorkspace::SessionFor(const Primitive* p) const {
    const int i = IndexOf(p);
    return (i >= 0) ? m_sessions[i].get() : nullptr;
}

ClipPlaneNode* SemWorkspace::FindClipPlane(Primitive* rect, SemSession** owner) {
    if (owner) *owner = nullptr;
    if (!rect) return nullptr;
    for (auto& s : m_sessions)
        if (ClipPlaneNode* n = s->FindClipPlaneByRect(rect)) {
            if (owner) *owner = s.get();
            return n;
        }
    return nullptr;
}

Primitive* SemWorkspace::ActiveSourcePrim() const {
    const SemSession* s = At(m_active);
    return s ? s->SourcePrim() : nullptr;
}

std::string SemWorkspace::Label(int i) const {
    const SemSession* s = At(i);
    if (!s) return std::string();
    const std::string stem = Stem(s->SourcePath());
    if (stem.empty()) return "(empty)";
    // Two setups can share one source file; then the session folder name (its
    // "<stem>_<N>" tail) is what tells them apart.
    for (int j = 0; j < (int)m_sessions.size(); ++j)
        if (j != i && Stem(m_sessions[j]->SourcePath()) == stem)
            return BaseName(s->WorkDir()).empty() ? stem : BaseName(s->WorkDir());
    return stem;
}

bool SemWorkspace::AnyAsyncRunning() const {
    for (const auto& s : m_sessions)
        if (s->AsyncRunning()) return true;
    return false;
}

void SemWorkspace::SetActive(Scene& scene, int i) {
    SemSession* s = At(i);
    if (!s) return;
    m_active = i;
    s->Activate();
    Primitive* src = s->SourcePrim();
    if (src && src != scene.stagedPrimitive) {
        scene.stagingEnabled = true;
        scene.SetStaged(src);
    }
}

SemSession& SemWorkspace::AddSetup() {
    m_sessions.push_back(std::unique_ptr<SemSession>(new SemSession()));
    m_active = (int)m_sessions.size() - 1;
    return *m_sessions.back();
}

int SemWorkspace::Adopt(Scene& scene, Primitive* prim) {
    if (!prim || prim->semSourcePath.empty()) return -1;

    // A source whose session folder already holds a manifest is reopened through
    // the session reload, not a bare load: SEM_LoadSurface3D would rewrite that
    // manifest as a fresh source and lose every saved stage.
    const bool hasSaved = !prim->semWorkDir.empty() &&
                          fs::exists(fs::path(prim->semWorkDir) / "session3d.txt");

    SemSession& s = AddSetup();
    s.Bind(scene, prim, /*reload=*/hasSaved);
    if (!s.HasSource()) {
        s.Release();
        m_sessions.pop_back();
        m_active = m_sessions.empty() ? -1 : (int)m_sessions.size() - 1;
        return -1;
    }
    if (hasSaved) s.LoadSessionStages(scene);
    return (int)m_sessions.size() - 1;
}

void SemWorkspace::Sync(Scene& scene) {
    // Drop setups whose source primitive is gone (deleted from the tree, or the
    // whole scene cleared). Validate needs the setup's own context current: it
    // touches only the scene, but what follows it does not.
    for (int i = (int)m_sessions.size() - 1; i >= 0; --i) {
        SemSession& s = *m_sessions[i];
        s.Activate();
        s.Validate(scene);
        if (s.HasSource() || s.AsyncRunning()) continue;
        s.Release();
        m_sessions.erase(m_sessions.begin() + i);
        if (m_active == i)      m_active = -1;
        else if (m_active > i)  --m_active;
    }
    if (m_active < 0 && !m_sessions.empty()) m_active = (int)m_sessions.size() - 1;

    // Follow the staged primitive. Staging nothing (double-click in empty space)
    // deliberately does NOT unbind: the active setup survives, which is the whole
    // point of keeping the caches apart.
    Primitive* staged = scene.stagedPrimitive;
    if (staged && !staged->semSourcePath.empty()) {
        int idx = IndexOf(staged);
        if (idx < 0 && !AnyAsyncRunning()) idx = Adopt(scene, staged);
        if (idx >= 0) m_active = idx;
    }

    // Apply finished background results for every setup, each against its own
    // context, then leave the active one current for the rest of the frame.
    for (auto& s : m_sessions) {
        s->Activate();
        s->PollAsync(scene);
    }
    Active().Activate();
}

void SemWorkspace::CloseSetup(Scene& scene, int i) {
    SemSession* s = At(i);
    if (!s || s->AsyncRunning()) return;

    Primitive* src = s->SourcePrim();
    s->Release();
    // The whole pipeline (offsets, mesh, isosurface, clip planes, overlays) hangs
    // under the source, so removing it removes the setup's geometry with it.
    if (src) scene.RemovePrimitive(src);

    m_sessions.erase(m_sessions.begin() + i);
    if (m_active == i)     m_active = -1;
    else if (m_active > i) --m_active;
    if (m_active < 0 && !m_sessions.empty()) m_active = (int)m_sessions.size() - 1;

    SemSession* now = At(m_active);
    if (now) SetActive(scene, m_active);
    else     scene.ClearStaged();
}

}
