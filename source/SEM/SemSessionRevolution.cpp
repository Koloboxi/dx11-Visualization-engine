#include "SemSessionDetail.h"

namespace SemSessionNS {

using namespace detail;

bool SemSession::ValidateRevolutionContour(Scene& scene) {
    if (!HasSource()) { Report(scene, false, "Revolution: no staged contour."); return false; }
    CSV3DLoader::CSV3DData d;
    if (!CSV3DLoader::Load(m_srcPath, d)) { Report(scene, false, "Revolution: cannot load contour."); return false; }
    int pos = 0, neg = 0;
    for (const auto& nd : d.nodes) {
        if (nd.pos.x >  1e-4f) ++pos;
        else if (nd.pos.x < -1e-4f) ++neg;
    }
    if (pos > 0 && neg > 0) {
        Report(scene, false,
            "Revolution contour invalid: vertices on both sides of the Y axis "
            "(X>0 and X<0). A revolution profile must lie entirely on one side.");
        return false;
    }
    if (pos == 0 && neg == 0) {
        Report(scene, false, "Revolution contour invalid: contour lies on the Y axis (zero radius).");
        return false;
    }
    return true;
}

bool SemSession::SetRevolutionMode(Scene& scene, bool enable) {
    if (!enable) {
        SEM_SetRevolution(0, kRevolutionAxisY);
        revolutionMode = false;
        return false;
    }
    if (!ValidateRevolutionContour(scene)) {
        SEM_SetRevolution(0, kRevolutionAxisY);
        revolutionMode = false;
        return false;
    }
    int rc = SEM_SetRevolution(1, kRevolutionAxisY);
    if (rc != 0) {
        CheckRc(scene, false, "SEM_SetRevolution", rc,
                { "", "no source", "bad axis", "endpoints off axis", "contour crosses axis" });
        SEM_SetRevolution(0, kRevolutionAxisY);
        revolutionMode = false;
        return false;
    }
    revolutionMode = true;
    return true;
}

void SemSession::ShowSourceRevolution(Scene& scene, bool show) {
    if (show) BuildSourceRevolution(scene);
    else if (Alive(scene, m_srcRevSurf)) m_srcRevSurf->visible = false;
}

void SemSession::ShowIsolineRevolution(Scene& scene, bool show) {
    if (show) BuildIsolineRevolution(scene);
    else if (Alive(scene, m_isoRevSurf)) m_isoRevSurf->visible = false;
}

void SemSession::SetSrcRevAlpha(Scene& scene, float a) {
    srcRevAlpha = a;
    if (Alive(scene, m_srcRevSurf)) { XMFLOAT4 c = m_srcRevSurf->GetColor(); c.w = a; m_srcRevSurf->SetColor(c); }
}
void SemSession::SetIsoRevAlpha(Scene& scene, float a) {
    isoRevAlpha = a;
    if (Alive(scene, m_isoRevSurf)) { XMFLOAT4 c = m_isoRevSurf->GetColor(); c.w = a; m_isoRevSurf->SetColor(c); }
}

void SemSession::BuildSourceRevolution(Scene& scene) {
    if (Alive(scene, m_srcRevSurf)) { m_srcRevSurf->visible = true; return; }
    std::vector<XMFLOAT3> prof;
    if (!OrderedContourFromCSV3D(m_srcPath, prof)) {
        Report(scene, false, "Revolution: cannot read source contour."); return;
    }
    const XMFLOAT4 frontCol(Colors::FRONT_FACE_WHITE.x, Colors::FRONT_FACE_WHITE.y, Colors::FRONT_FACE_WHITE.z, srcRevAlpha);
    m_srcRevSurf = scene.AddRevolutionSurface(prof, (UINT)revSegments, frontCol,
                                              "revsurf_src_" + Stem(m_srcPath), AttachParent());
    if (m_srcRevSurf) m_srcRevSurf->SetTwoSided(true, Colors::BACK_FACE_RED);
    scene.UpdateLight();
    if (m_srcRevSurf) snprintf(status, sizeof(status), "Source revolution surface built.");
}

void SemSession::BuildIsolineRevolution(Scene& scene) {
    if (Alive(scene, m_isoRevSurf)) { m_isoRevSurf->visible = true; return; }
    if (m_isolinePath.empty()) { Report(scene, false, "Revolution: extract the isotherm first."); return; }
    std::vector<XMFLOAT3> prof;
    if (!OrderedContourFromCSV3D(m_isolinePath, prof)) {
        Report(scene, false, "Revolution: cannot read isotherm contour."); return;
    }
    const XMFLOAT4 frontCol(Colors::FRONT_FACE_WHITE.x, Colors::FRONT_FACE_WHITE.y, Colors::FRONT_FACE_WHITE.z, isoRevAlpha);
    SceneNode* parent = Alive(scene, m_isoline) ? static_cast<SceneNode*>(m_isoline) : AttachParent();
    m_isoRevSurf = scene.AddRevolutionSurface(prof, (UINT)revSegments, frontCol,
                                              "revsurf_iso_" + Stem(m_srcPath), parent);
    if (m_isoRevSurf) m_isoRevSurf->SetTwoSided(true, Colors::BACK_FACE_RED);
    scene.UpdateLight();
    if (m_isoRevSurf) snprintf(status, sizeof(status), "Isotherm revolution surface built.");
}

void SemSession::DropSrcRev(Scene& scene) {
    if (Alive(scene, m_srcRevSurf)) scene.RemovePrimitive(m_srcRevSurf);
    m_srcRevSurf = nullptr;
}
void SemSession::DropIsoRev(Scene& scene) {
    if (Alive(scene, m_isoRevSurf)) scene.RemovePrimitive(m_isoRevSurf);
    m_isoRevSurf = nullptr;
}

}
