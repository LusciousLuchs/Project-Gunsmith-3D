// Gunsmith3D - modelled characters (see charmodel.hpp).
#include "app/charmodel.hpp"
#include "app/gfx.hpp"
#include "app/paths.hpp"
#include "rig.hpp"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace {
struct Loaded {
    bool ok = false;
    std::string error;
    Model model{};
    std::vector<rig::Joint> J;
    std::map<std::string, std::vector<int>> maps;   // by the NPC's bone names
    int triangles = 0;
};
std::map<std::string, Loaded> gModels;

std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

Loaded& load(const std::string& rel) {
    auto it = gModels.find(rel);
    if (it != gModels.end()) return it->second;
    Loaded& L = gModels[rel];
    std::string path = std::string(MODELS_DIR) + "/" + rel, ext = lower(fs::path(rel).extension().string());
    std::error_code ec;
    if (ext != ".glb" && ext != ".gltf") { L.error = "Only .glb and .gltf files can be used."; return L; }
    if (!fs::exists(path, ec)) { L.error = "The file isn't in the Models folder any more."; return L; }
    L.model = LoadModel(path.c_str());
    if (L.model.meshCount == 0 || !L.model.meshes) { L.error = "It couldn't be read as a 3D model."; return L; }
    if (L.model.boneCount <= 0 || !L.model.bindPose) { L.error = "It has no skeleton. Export it with its armature (and skin)."; return L; }
    bool skinned = false;
    for (int m = 0; m < L.model.meshCount; m++) {
        L.triangles += L.model.meshes[m].triangleCount;
        if (L.model.meshes[m].boneIds && L.model.meshes[m].boneWeights) skinned = true;
    }
    if (!skinned) { L.error = "Its meshes aren't skinned to the skeleton (no joint weights)."; return L; }
    for (int i = 0; i < L.model.boneCount; i++) {
        rig::Joint j;
        j.name = L.model.bones[i].name;
        j.parent = L.model.bones[i].parent;
        const Transform& t = L.model.bindPose[i];
        j.bind.p[0] = t.translation.x; j.bind.p[1] = t.translation.y; j.bind.p[2] = t.translation.z;
        j.bind.q[0] = t.rotation.x; j.bind.q[1] = t.rotation.y; j.bind.q[2] = t.rotation.z; j.bind.q[3] = t.rotation.w;
        L.J.push_back(j);
    }
    L.ok = true;
    return L;
}
const std::vector<int>& mapFor(Loaded& L, const NpcDef& n) {
    std::string key;
    for (auto& b : n.bones) if (!b.gen || b.body) key += b.name + "|";
    auto it = L.maps.find(key);
    if (it != L.maps.end()) return it->second;
    return L.maps[key] = rig::mapJoints(L.J, n);
}
Matrix ofCols(const float f[16]) { return Matrix{f[0], f[4], f[8], f[12], f[1], f[5], f[9], f[13], f[2], f[6], f[10], f[14], f[3], f[7], f[11], f[15]}; }
Matrix boneFrame(const NpcBoneAt& p) {   // the bone's own frame (mm) in the NPC's
    return Matrix{(float)p.x[0], (float)p.y[0], (float)p.z[0], (float)p.a[0], (float)p.x[1], (float)p.y[1], (float)p.z[1], (float)p.a[1],
                  (float)p.x[2], (float)p.y[2], (float)p.z[2], (float)p.a[2], 0, 0, 0, 1};
}
}  // namespace

bool charModelUsable(const NpcDef& n) { return !n.model.empty() && load(n.model).ok; }

CharModelInfo charModelInfo(const std::string& rel, const NpcDef& n) {
    CharModelInfo I;
    if (rel.empty()) return I;
    Loaded& L = load(rel);
    I.ok = L.ok; I.error = L.error;
    if (!L.ok) return I;
    I.joints = (int)L.J.size(); I.triangles = L.triangles;
    I.report = rig::mapReport(L.J, mapFor(L, n), n);
    return I;
}

bool drawCharModel(const NpcDef& n, const std::vector<Matrix>& M, const Color* tint) {
    if (n.model.empty() || M.size() < n.bones.size() || n.bones.empty()) return false;
    Loaded& L = load(n.model);
    if (!L.ok) return false;
    const auto& map = mapFor(L, n);
    // where each bone is now, in the NPC's own frame (the world frames taken back through the root's)
    const auto stand = npcPose(n);
    Matrix W = MatrixMultiply(MatrixInvert(boneFrame(stand[0])), M[0]);   // the NPC's frame (mm) -> the world
    Matrix Wi = MatrixInvert(W);
    std::vector<NpcBoneAt> P(n.bones.size());
    for (size_t i = 0; i < n.bones.size(); i++) {
        Matrix A = MatrixMultiply(M[i], Wi);
        Vector3 x = Vector3Normalize({A.m0, A.m1, A.m2}), y = Vector3Normalize({A.m4, A.m5, A.m6}), z = Vector3Normalize({A.m8, A.m9, A.m10});
        NpcBoneAt& p = P[i];
        p.a[0] = A.m12; p.a[1] = A.m13; p.a[2] = A.m14;
        p.x[0] = x.x; p.x[1] = x.y; p.x[2] = x.z; p.y[0] = y.x; p.y[1] = y.y; p.y[2] = y.z; p.z[0] = z.x; p.z[1] = z.y; p.z[2] = z.z;
        for (int k = 0; k < 3; k++) p.b[k] = p.a[k] + p.y[k] * n.bones[i].len;
    }
    auto X = rig::poseJoints(L.J, map, n, P);
    std::vector<Matrix> S(L.J.size());
    for (size_t j = 0; j < L.J.size(); j++) { float f[16]; rig::skinMatrix(L.J[j].bind, X[j], f); S[j] = ofCols(f); }
    // skinned here (each person posed differently, one model shared), then drawn at once
    for (int m = 0; m < L.model.meshCount; m++) {
        Mesh& mesh = L.model.meshes[m];
        if (!mesh.boneIds || !mesh.boneWeights || !mesh.animVertices) continue;
        for (int v = 0; v < mesh.vertexCount; v++) {
            Vector3 base{mesh.vertices[3 * v], mesh.vertices[3 * v + 1], mesh.vertices[3 * v + 2]};
            Vector3 nb = mesh.normals ? Vector3{mesh.normals[3 * v], mesh.normals[3 * v + 1], mesh.normals[3 * v + 2]} : Vector3{0, 1, 0};
            Vector3 o{0, 0, 0}, on{0, 0, 0};
            float wsum = 0;
            for (int k = 0; k < 4; k++) {
                float w = mesh.boneWeights[4 * v + k];
                if (w <= 0) continue;
                int b = mesh.boneIds[4 * v + k];
                if (b < 0 || b >= (int)S.size()) continue;
                o = Vector3Add(o, Vector3Scale(Vector3Transform(base, S[b]), w));
                Matrix R = S[b]; R.m12 = R.m13 = R.m14 = 0;
                on = Vector3Add(on, Vector3Scale(Vector3Transform(nb, R), w));
                wsum += w;
            }
            if (wsum <= 0) { o = base; on = nb; }
            else if (std::fabs(wsum - 1) > 1e-3f) o = Vector3Scale(o, 1 / wsum);
            mesh.animVertices[3 * v] = o.x; mesh.animVertices[3 * v + 1] = o.y; mesh.animVertices[3 * v + 2] = o.z;
            if (mesh.animNormals) { on = Vector3Normalize(on); mesh.animNormals[3 * v] = on.x; mesh.animNormals[3 * v + 1] = on.y; mesh.animNormals[3 * v + 2] = on.z; }
        }
        rlUpdateVertexBuffer(mesh.vboId[0], mesh.animVertices, mesh.vertexCount * 3 * sizeof(float), 0);
        if (mesh.animNormals && mesh.vboId[2]) rlUpdateVertexBuffer(mesh.vboId[2], mesh.animNormals, mesh.vertexCount * 3 * sizeof(float), 0);
    }
    double s = rig::modelScale(L.J, map, n);   // (the model at the NPC's size: its hips at the NPC's pelvis height)
    Matrix toWorld = MatrixMultiply(MatrixScale((float)(1 / s), (float)(1 / s), (float)(1 / s)), W);
    rlDisableBackfaceCulling();
    for (int m = 0; m < L.model.meshCount; m++) {
        int mi = L.model.meshMaterial ? L.model.meshMaterial[m] : 0;
        const MaterialMap& d = L.model.materials[std::clamp(mi, 0, L.model.materialCount - 1)].maps[MATERIAL_MAP_DIFFUSE];
        Color c = tint ? *tint : d.color;
        if (c.a == 0) c.a = 255;
        drawMeshTex(L.model.meshes[m], toWorld, c, d.texture);
    }
    rlEnableBackfaceCulling();
    return true;
}

std::vector<std::string> charModelFiles(const std::string& sub) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::path root = fs::path(MODELS_DIR);
    for (auto it = fs::recursive_directory_iterator(root / sub, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string e = lower(it->path().extension().string());
        if (e == ".glb" || e == ".gltf") out.push_back(fs::relative(it->path(), root, ec).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string shopPersonModel(int id, bool female) {
    static std::vector<std::string> files; static double when = -100;
    if (GetTime() - when > 5) { files = charModelFiles("Shop people"); when = GetTime(); }   // (new files show up without a restart)
    if (files.empty()) return "";
    // a woman's model for a woman, a man's for a man, by the file's name; else any
    std::vector<std::string> pick;
    for (auto& f : files) {
        std::string l = lower(fs::path(f).filename().string());
        bool fem = l.find("female") != std::string::npos || l.find("woman") != std::string::npos || l.find("women") != std::string::npos || l.find("girl") != std::string::npos;
        bool male = !fem && (l.find("male") != std::string::npos || l.find("man") != std::string::npos || l.find("men") != std::string::npos || l.find("boy") != std::string::npos);
        if ((female && !male) || (!female && !fem)) pick.push_back(f);
    }
    if (pick.empty()) pick = files;
    unsigned long long z = (unsigned long long)id * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z ^= z >> 31;
    return pick[z % pick.size()];
}

void charModelsForget() {
    for (auto& [k, L] : gModels) if (L.ok || L.model.meshCount) UnloadModel(L.model);
    gModels.clear();
}
