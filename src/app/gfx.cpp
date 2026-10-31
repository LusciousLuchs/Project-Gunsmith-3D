// Gunsmith3D - Graphics kit: lit shader, primitive meshes, extruding outlines into meshes.
#include "app/app.hpp"
#include "app/gfx.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------- graphics kit
static const char* VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;
out vec2 fragTexCoord; out vec3 fragNormal; out vec3 fragPos;
void main(){ fragTexCoord=vertexTexCoord; fragNormal=normalize(vec3(matNormal*vec4(vertexNormal,0.0)));
 fragPos=vec3(matModel*vec4(vertexPosition,1.0)); gl_Position=mvp*vec4(vertexPosition,1.0); })";
static const char* FS = R"(#version 330
in vec2 fragTexCoord; in vec3 fragNormal; in vec3 fragPos;
uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec3 viewPos; uniform float spec;
uniform vec3 lightDir; uniform vec3 lightCol; uniform vec3 fillDir; uniform vec3 fillCol;
uniform vec3 ambSky; uniform vec3 ambGround; uniform vec3 fogColor; uniform vec3 fogParam;
out vec4 finalColor;
void main(){ vec4 tex=texture(texture0,fragTexCoord); vec3 n=normalize(fragNormal);
 vec3 V=normalize(viewPos-fragPos);
 if(dot(n,V)<0.0) n=-n;
 vec3 L=normalize(lightDir); vec3 F=normalize(fillDir);
 float d=max(dot(n,L),0.0), fill=max(dot(n,F),0.0);
 float hemi=0.5+0.5*n.y; vec3 amb=mix(ambGround,ambSky,hemi);
 vec3 base=tex.rgb*colDiffuse.rgb;
 vec3 c=base*(amb*0.70+lightCol*d*0.58+fillCol*fill*0.16);
 vec3 H=normalize(L+V);
 float s=pow(max(dot(n,H),0.0),mix(24.0,64.0,spec))*spec*(0.25+0.75*d);
 float rim=pow(1.0-max(dot(n,V),0.0),3.0)*0.14;
 c+=lightCol*s+amb*rim;
 float dist=length(viewPos-fragPos); float f=clamp((dist-fogParam.x)/fogParam.y,0.0,fogParam.z); c=mix(c,fogColor,f);
 finalColor=vec4(c,tex.a*colDiffuse.a); })";

Gfx G;

void setSpecular(float s) { SetShaderValue(G.lit, G.locSpec, &s, SHADER_UNIFORM_FLOAT); }
void setLighting(const Lighting& L) {
    SetShaderValue(G.lit, G.locLight, L.dir, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locLightCol, L.col, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locFill, L.fill, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locFillCol, L.fillCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locAmbSky, L.ambSky, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locAmbGround, L.ambGround, SHADER_UNIFORM_VEC3);
    SetShaderValue(G.lit, G.locFogParam, L.fog, SHADER_UNIFORM_VEC3);
}
void setFog(Color c) { float f[3] = {c.r / 255.f, c.g / 255.f, c.b / 255.f}; SetShaderValue(G.lit, G.locFog, f, SHADER_UNIFORM_VEC3); }
void drawMesh(const Mesh& m, Matrix M, Color c, float alpha) {
    G.mat.maps[MATERIAL_MAP_DIFFUSE].color = Color{c.r, c.g, c.b, (unsigned char)(255 * alpha)};
    DrawMesh(m, G.mat, M);
}
void drawBox(Vector3 center, Vector3 size, Color c, Matrix rot, float alpha) {
    drawMesh(G.cube, MatrixMultiply(MatrixMultiply(MatrixScale(size.x, size.y, size.z), rot), MatrixTranslate(center.x, center.y, center.z)), c, alpha);
}
static Matrix alignY(Vector3 dir) {  // rotation taking +Y onto dir
    Vector3 y{0, 1, 0};
    Vector3 d = Vector3Normalize(dir);
    float dot = Vector3DotProduct(y, d);
    if (dot > 0.9999f) return MatrixIdentity();
    if (dot < -0.9999f) return MatrixRotateX(PI);
    Vector3 ax = Vector3Normalize(Vector3CrossProduct(y, d));
    return MatrixRotate(ax, std::acos(dot));
}
void drawCyl(Vector3 a, Vector3 b, float r, Color c, float alpha, const Mesh* mesh) {
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    if (len < 1e-5f) return;
    Matrix M = MatrixMultiply(MatrixMultiply(MatrixScale(r, len, r), alignY(d)), MatrixTranslate(a.x, a.y, a.z));
    drawMesh(mesh ? *mesh : G.cyl, M, c, alpha);
}
void drawSphereLit(Vector3 p, float r, Color c, float alpha) {
    drawMesh(G.sphere, MatrixMultiply(MatrixScale(r, r, r), MatrixTranslate(p.x, p.y, p.z)), c, alpha);
}

// polygon triangulation (ear clipping) for extruded parts
static bool pointInTri(V2 p, V2 a, V2 b, V2 c) {
    auto s = [](V2 p1, V2 p2, V2 p3) { return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y); };
    double d1 = s(p, a, b), d2 = s(p, b, c), d3 = s(p, c, a);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}
static std::vector<int> triangulate(const std::vector<V2>& p) {  // p must be CCW
    std::vector<int> idx(p.size()), out;
    for (size_t i = 0; i < p.size(); i++) idx[i] = (int)i;
    int guard = 0;
    while (idx.size() > 3 && guard++ < 5000) {
        bool cut = false;
        for (size_t i = 0; i < idx.size(); i++) {
            int i0 = idx[(i + idx.size() - 1) % idx.size()], i1 = idx[i], i2 = idx[(i + 1) % idx.size()];
            V2 a = p[i0], b = p[i1], c = p[i2];
            double cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (cr <= 1e-12) continue;
            bool inside = false;
            for (int k : idx) {
                if (k == i0 || k == i1 || k == i2) continue;
                if (pointInTri(p[k], a, b, c)) { inside = true; break; }
            }
            if (inside) continue;
            out.insert(out.end(), {i0, i1, i2});
            idx.erase(idx.begin() + i);
            cut = true;
            break;
        }
        if (!cut) break;
    }
    if (idx.size() == 3) out.insert(out.end(), {idx[0], idx[1], idx[2]});
    else for (size_t i = 1; i + 1 < idx.size(); i++) out.insert(out.end(), {idx[0], idx[i], idx[i + 1]});  // fallback fan
    return out;
}
// Extrude a polygon given in local mm (y down) into a flat-shaded prism centred on z=0.
Mesh extrude(const std::vector<V2>& localMM, float depth) {
    std::vector<V2> q;
    for (auto& p : localMM) q.push_back({p.x * U, -p.y * U});
    if (sim::polyArea(q) < 0) std::reverse(q.begin(), q.end());
    auto tri = triangulate(q);
    int n = (int)q.size();
    int vc = (int)tri.size() * 2 + n * 6;
    Mesh m{};
    m.vertexCount = vc;
    m.triangleCount = vc / 3;
    m.vertices = (float*)MemAlloc(vc * 3 * sizeof(float));
    m.normals = (float*)MemAlloc(vc * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(vc * 2 * sizeof(float));
    int k = 0;
    float h = depth / 2;
    auto put = [&](float x, float y, float z, float nx, float ny, float nz) {
        m.vertices[k * 3] = x; m.vertices[k * 3 + 1] = y; m.vertices[k * 3 + 2] = z;
        m.normals[k * 3] = nx; m.normals[k * 3 + 1] = ny; m.normals[k * 3 + 2] = nz;
        m.texcoords[k * 2] = 0; m.texcoords[k * 2 + 1] = 0; k++;
    };
    for (size_t t = 0; t < tri.size(); t += 3) {
        for (int s = 0; s < 3; s++) put((float)q[tri[t + s]].x, (float)q[tri[t + s]].y, h, 0, 0, 1);
        for (int s = 2; s >= 0; s--) put((float)q[tri[t + s]].x, (float)q[tri[t + s]].y, -h, 0, 0, -1);
    }
    // side walls: flat across sharp corners, smooth along curves (where the outline turns only a little per point)
    std::vector<V2> en(n);
    for (int i = 0; i < n; i++) {
        V2 a = q[i], b = q[(i + 1) % n];
        double dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy);
        en[i] = l > 0 ? V2{dy / l, -dx / l} : V2{0, 0};
    }
    auto vn = [&](int edge, int vert) {  // normal at outline point `vert` for the wall of `edge`
        int other = vert == edge ? (edge + n - 1) % n : (edge + 1) % n;
        V2 e = en[edge], o = en[other];
        if (e.x * o.x + e.y * o.y < std::cos(25 * DEG2RAD)) return e;
        double sx = e.x + o.x, sy = e.y + o.y, l = std::sqrt(sx * sx + sy * sy);
        return l > 0 ? V2{sx / l, sy / l} : e;
    };
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        V2 a = q[i], b = q[j], na = vn(i, i), nb = vn(i, j);
        put((float)a.x, (float)a.y, -h, (float)na.x, (float)na.y, 0); put((float)b.x, (float)b.y, -h, (float)nb.x, (float)nb.y, 0); put((float)b.x, (float)b.y, h, (float)nb.x, (float)nb.y, 0);
        put((float)a.x, (float)a.y, -h, (float)na.x, (float)na.y, 0); put((float)b.x, (float)b.y, h, (float)nb.x, (float)nb.y, 0); put((float)a.x, (float)a.y, h, (float)na.x, (float)na.y, 0);
    }
    UploadMesh(&m, false);
    return m;
}

void initGfx() {
    G.lit = LoadShaderFromMemory(VS, FS);
    G.lit.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(G.lit, "matModel");
    G.lit.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(G.lit, "matNormal");
    G.locView = GetShaderLocation(G.lit, "viewPos");
    G.locFog = GetShaderLocation(G.lit, "fogColor");
    G.locSpec = GetShaderLocation(G.lit, "spec");
    G.locLight = GetShaderLocation(G.lit, "lightDir"); G.locLightCol = GetShaderLocation(G.lit, "lightCol");
    G.locFill = GetShaderLocation(G.lit, "fillDir"); G.locFillCol = GetShaderLocation(G.lit, "fillCol");
    G.locAmbSky = GetShaderLocation(G.lit, "ambSky"); G.locAmbGround = GetShaderLocation(G.lit, "ambGround");
    G.locFogParam = GetShaderLocation(G.lit, "fogParam");
    setLighting(Lighting{});
    setSpecular(0.3f);
    setFog(Color{199, 219, 239, 255});
    G.mat = LoadMaterialDefault();
    G.mat.shader = G.lit;
    G.cube = GenMeshCube(1, 1, 1);
    G.cyl = GenMeshCylinder(1, 1, 14);
    G.cone = GenMeshCone(1, 1, 14);
    G.sphere = GenMeshSphere(1, 10, 14);
    // studded baseplate tile: one tile = 10 mm
    Image img = GenImageColor(32, 32, Color{124, 132, 140, 255});
    ImageDrawRectangle(&img, 0, 0, 32, 1, Color{104, 111, 119, 255});
    ImageDrawRectangle(&img, 0, 0, 1, 32, Color{104, 111, 119, 255});
    ImageDrawCircle(&img, 16, 16, 9, Color{134, 142, 150, 255});
    ImageDrawCircle(&img, 15, 15, 7, Color{142, 150, 158, 255});
    G.plateTex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&G.plateTex);
    SetTextureFilter(G.plateTex, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(G.plateTex, TEXTURE_WRAP_REPEAT);
    const float S = 300.f;
    Mesh p{};
    p.vertexCount = 6; p.triangleCount = 2;
    p.vertices = (float*)MemAlloc(18 * sizeof(float));
    p.normals = (float*)MemAlloc(18 * sizeof(float));
    p.texcoords = (float*)MemAlloc(12 * sizeof(float));
    float V[6][2] = {{-S, -S}, {S, S}, {S, -S}, {-S, -S}, {-S, S}, {S, S}};
    for (int i = 0; i < 6; i++) {
        p.vertices[i * 3] = V[i][0]; p.vertices[i * 3 + 1] = 0; p.vertices[i * 3 + 2] = V[i][1];
        p.normals[i * 3] = 0; p.normals[i * 3 + 1] = 1; p.normals[i * 3 + 2] = 0;
        p.texcoords[i * 2] = V[i][0]; p.texcoords[i * 2 + 1] = V[i][1];
    }
    UploadMesh(&p, false);
    G.plate = p;
    G.plateMat = LoadMaterialDefault();
    G.plateMat.shader = G.lit;
    G.plateMat.maps[MATERIAL_MAP_DIFFUSE].texture = G.plateTex;
}
