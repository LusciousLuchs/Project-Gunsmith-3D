// Gunsmith3D - Graphics kit: lit shader, primitive meshes, extruding outlines into meshes.
#include "app/app.hpp"
#include "app/gfx.hpp"
#include "csg.hpp"
#include "raymath.h"

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
uniform int nLamps; uniform vec4 lampPos[16]; uniform vec4 lampCol[16]; uniform vec4 lampDir[16];
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
 vec3 lampAcc=vec3(0.0); vec3 lampSpec=vec3(0.0);
 for(int i=0;i<nLamps;i++){   // the map's lamps, a gun's light, a muzzle flash: xyz + reach; colour x power; aim + cone
  vec3 Lv=lampPos[i].xyz-fragPos; float dl=length(Lv); float r=lampPos[i].w;
  if(dl>r) continue;
  vec3 l=Lv/max(dl,1e-4); float q=dl/r; float w=max(0.0,1.0-q*q*q*q);
  float att=w*w/(1.0+dl*dl);
  if(lampDir[i].w>-1.5){ float cs=dot(-l,lampDir[i].xyz); float inner=min(1.0,lampDir[i].w+0.08); att*=smoothstep(lampDir[i].w,inner,cs); }
  float ld=max(dot(n,l),0.0);
  vec3 hl=normalize(l+V); float ls=pow(max(dot(n,hl),0.0),mix(24.0,64.0,spec))*spec*ld;
  lampAcc+=lampCol[i].rgb*att*ld; lampSpec+=lampCol[i].rgb*att*ls;
 }
 c+=base*0.9*lampAcc/(1.0+0.45*lampAcc)+lampSpec/(1.0+0.6*lampSpec);   // (bright close up, without burning out to white)
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
void setLamps(const std::vector<Lamp>& lamps) {
    int n = std::min((int)lamps.size(), MAX_LAMPS);
    float P[MAX_LAMPS * 4] = {0}, C[MAX_LAMPS * 4] = {0}, D[MAX_LAMPS * 4] = {0};
    for (int i = 0; i < n; i++) {
        const Lamp& L = lamps[i];
        for (int k = 0; k < 3; k++) { P[i * 4 + k] = L.pos[k]; C[i * 4 + k] = L.col[k] * L.power; D[i * 4 + k] = L.dir[k]; }
        P[i * 4 + 3] = L.range; D[i * 4 + 3] = L.cosCone;
    }
    SetShaderValue(G.lit, G.locNLamps, &n, SHADER_UNIFORM_INT);
    if (n > 0) {
        SetShaderValueV(G.lit, G.locLampPos, P, SHADER_UNIFORM_VEC4, n);
        SetShaderValueV(G.lit, G.locLampCol, C, SHADER_UNIFORM_VEC4, n);
        SetShaderValueV(G.lit, G.locLampDir, D, SHADER_UNIFORM_VEC4, n);
    }
}
void setFog(Color c) { float f[3] = {c.r / 255.f, c.g / 255.f, c.b / 255.f}; SetShaderValue(G.lit, G.locFog, f, SHADER_UNIFORM_VEC3); }
void drawMesh(const Mesh& m, Matrix M, Color c, float alpha) {
    G.mat.maps[MATERIAL_MAP_DIFFUSE].color = Color{c.r, c.g, c.b, (unsigned char)(255 * alpha)};
    DrawMesh(m, G.mat, M);
}
void drawMeshTex(const Mesh& m, Matrix M, Color c, Texture2D tex, float alpha) {
    Texture2D was = G.mat.maps[MATERIAL_MAP_DIFFUSE].texture;
    if (tex.id) G.mat.maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    drawMesh(m, M, c, alpha);
    G.mat.maps[MATERIAL_MAP_DIFFUSE].texture = was;
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
// `round` (mm) rounds the edges where the sides meet the front and back faces: a quarter circle in a few steps.
Mesh extrude(const std::vector<V2>& localMM, float depth, float roundMM) {
    std::vector<V2> q;
    for (auto& p : localMM) q.push_back({p.x * U, -p.y * U});
    if (sim::polyArea(q) < 0) std::reverse(q.begin(), q.end());
    auto tri = triangulate(q);
    int n = (int)q.size();
    float h = depth / 2;
    // outward normal of each edge
    std::vector<V2> en(n);
    double perim = 0;
    for (int i = 0; i < n; i++) {
        V2 a = q[i], b = q[(i + 1) % n];
        double dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy);
        en[i] = l > 0 ? V2{dy / l, -dx / l} : V2{0, 0};
        perim += l;
    }
    // how far the faces can be pulled in before the shape folds over: about half the part's narrowest width
    double r = std::max(0.0, (double)roundMM * U);
    if (r > 0) r = std::min({r, h * 0.95, 0.4 * 2 * std::fabs(sim::polyArea(q)) / std::max(perim, 1e-9)});
    int steps = r > 1e-4 ? 5 : 0;
    // inward offset direction at each corner (mitred, limited at sharp points)
    std::vector<V2> mit(n);
    for (int i = 0; i < n; i++) {
        V2 a = en[(i + n - 1) % n], b = en[i];
        double d = 1 + a.x * b.x + a.y * b.y;
        V2 m = d > 1e-6 ? V2{(a.x + b.x) / d, (a.y + b.y) / d} : b;
        double l = std::sqrt(m.x * m.x + m.y * m.y);
        if (l > 2.5) m = {m.x / l * 2.5, m.y / l * 2.5};
        mit[i] = m;
    }
    int bandTris = n * 2 * steps * 2;
    int vc = (int)tri.size() * 2 + n * 6 + bandTris * 3;
    Mesh m{};
    m.vertexCount = vc;
    m.triangleCount = vc / 3;
    m.vertices = (float*)MemAlloc(vc * 3 * sizeof(float));
    m.normals = (float*)MemAlloc(vc * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(vc * 2 * sizeof(float));
    int k = 0;
    auto put = [&](float x, float y, float z, float nx, float ny, float nz) {
        m.vertices[k * 3] = x; m.vertices[k * 3 + 1] = y; m.vertices[k * 3 + 2] = z;
        m.normals[k * 3] = nx; m.normals[k * 3 + 1] = ny; m.normals[k * 3 + 2] = nz;
        m.texcoords[k * 2] = 0; m.texcoords[k * 2 + 1] = 0; k++;
    };
    // the outline pulled in by d: where tight corners would fold over, the points meet instead (as a true offset does)
    auto insetRing = [&](double d) {
        std::vector<V2> o(n);
        for (int i = 0; i < n; i++) o[i] = {q[i].x - mit[i].x * d, q[i].y - mit[i].y * d};
        if (d <= 0) return o;
        for (int pass = 0; pass < 8; pass++) {
            bool any = false;
            for (int i = 0; i < n; i++) {
                int j = (i + 1) % n;
                double ox = q[j].x - q[i].x, oy = q[j].y - q[i].y, nx = o[j].x - o[i].x, ny = o[j].y - o[i].y;
                if (ox * nx + oy * ny < 0) {  // this edge turned back on itself: collapse it
                    V2 c{(o[i].x + o[j].x) / 2, (o[i].y + o[j].y) / 2};
                    o[i] = o[j] = c; any = true;
                }
            }
            if (!any) break;
        }
        return o;
    };
    std::vector<std::vector<V2>> rings;
    if (steps == 0) rings.push_back(q);
    else for (int st = 0; st <= steps; st++) rings.push_back(insetRing(r * (1 - std::cos((PI / 2) * st / steps))));
    auto inset = [&](int i, double) { return rings.back()[i]; };
    // front and back faces (pulled in by the rounding; the original triangulation still fits the inset outline)
    for (size_t t = 0; t < tri.size(); t += 3) {
        for (int s = 0; s < 3; s++) { V2 p = inset(tri[t + s], r); put((float)p.x, (float)p.y, h, 0, 0, 1); }
        for (int s = 2; s >= 0; s--) { V2 p = inset(tri[t + s], r); put((float)p.x, (float)p.y, -h, 0, 0, -1); }
    }
    // side walls: flat across sharp corners, smooth along curves (where the outline turns only a little per point)
    auto vn = [&](int edge, int vert) {  // normal at outline point `vert` for the wall of `edge`
        int other = vert == edge ? (edge + n - 1) % n : (edge + 1) % n;
        V2 e = en[edge], o = en[other];
        if (e.x * o.x + e.y * o.y < std::cos(25 * DEG2RAD)) return e;
        double sx = e.x + o.x, sy = e.y + o.y, l = std::sqrt(sx * sx + sy * sy);
        return l > 0 ? V2{sx / l, sy / l} : e;
    };
    float hw = (float)(h - r);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        V2 a = q[i], b = q[j], na = vn(i, i), nb = vn(i, j);
        put((float)a.x, (float)a.y, -hw, (float)na.x, (float)na.y, 0); put((float)b.x, (float)b.y, -hw, (float)nb.x, (float)nb.y, 0); put((float)b.x, (float)b.y, hw, (float)nb.x, (float)nb.y, 0);
        put((float)a.x, (float)a.y, -hw, (float)na.x, (float)na.y, 0); put((float)b.x, (float)b.y, hw, (float)nb.x, (float)nb.y, 0); put((float)a.x, (float)a.y, hw, (float)na.x, (float)na.y, 0);
    }
    // the rounded bands between the walls and the faces
    for (int side = -1; side <= 1; side += 2) {
        for (int st = 0; st < steps; st++) {
            double t0 = (PI / 2) * st / steps, t1 = (PI / 2) * (st + 1) / steps;
            for (int i = 0; i < n; i++) {
                int j = (i + 1) % n;
                V2 na = vn(i, i), nb = vn(i, j);
                V2 a0 = rings[st][i], b0 = rings[st][j], a1 = rings[st + 1][i], b1 = rings[st + 1][j];
                float z0 = side * (float)(hw + r * std::sin(t0)), z1 = side * (float)(hw + r * std::sin(t1));
                float c0 = (float)std::cos(t0), s0 = side * (float)std::sin(t0), c1 = (float)std::cos(t1), s1 = side * (float)std::sin(t1);
                struct P { V2 p; float z, nx, ny, nz; };
                P A0{a0, z0, (float)na.x * c0, (float)na.y * c0, s0}, B0{b0, z0, (float)nb.x * c0, (float)nb.y * c0, s0};
                P A1{a1, z1, (float)na.x * c1, (float)na.y * c1, s1}, B1{b1, z1, (float)nb.x * c1, (float)nb.y * c1, s1};
                auto emit = [&](const P& p) { put((float)p.p.x, (float)p.p.y, p.z, p.nx, p.ny, p.nz); };
                if (side > 0) { emit(A0); emit(B0); emit(B1); emit(A0); emit(B1); emit(A1); }
                else { emit(A0); emit(B1); emit(B0); emit(A0); emit(A1); emit(B1); }
            }
        }
    }
    m.vertexCount = k; m.triangleCount = k / 3;
    UploadMesh(&m, false);
    return m;
}

// A part turned on a lathe: `st` is (along the axis, radius) in mm, front to back; the axis runs through `o` (mm, in the
// mesh's frame but y down, as designs are) along `dir`. `S` is the cross-section for radius 1, as (up, toward the
// near side), counter-clockwise; `smooth` marks its points on curves (shaded round, the rest flat).
Mesh latheMesh(const std::vector<V2>& st, V2 o, V2 dir, const std::vector<V2>& S, const std::vector<char>& smooth) {
    std::vector<float> P, Nm;
    auto put = [&](Vector3 p, Vector3 n) { P.insert(P.end(), {p.x, p.y, p.z}); Nm.insert(Nm.end(), {n.x, n.y, n.z}); };
    Vector3 D{(float)dir.x, (float)-dir.y, 0}, UP{(float)dir.y, (float)dir.x, 0}, Z{0, 0, 1};
    auto at = [&](double u, double r, V2 s) {
        double ax = o.x + dir.x * u, ay = o.y + dir.y * u;
        Vector3 c{(float)(ax * U), (float)(-ay * U), 0};
        return Vector3Add(c, Vector3Add(Vector3Scale(UP, (float)(r * s.x * U)), Vector3Scale(Z, (float)(r * s.y * U))));
    };
    auto tri = [&](Vector3 a, Vector3 b, Vector3 c, Vector3 na, Vector3 nb, Vector3 nc) {
        Vector3 g = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        if (Vector3LengthSqr(g) < 1e-14f) return;
        if (Vector3DotProduct(g, Vector3Add(Vector3Add(na, nb), nc)) < 0) { std::swap(b, c); std::swap(nb, nc); }
        put(a, na); put(b, nb); put(c, nc);
    };
    size_t n = S.size();
    if (n < 3 || st.size() < 2) { Mesh m{}; return m; }
    // outward normal of each side of the cross-section, and at each point (for curves)
    std::vector<V2> en(n), vn(n);
    for (size_t j = 0; j < n; j++) {
        V2 a = S[j], c = S[(j + 1) % n];
        double ex = c.x - a.x, ey = c.y - a.y, l = std::hypot(ex, ey);
        en[j] = l > 0 ? V2{ey / l, -ex / l} : V2{0, 0};
    }
    for (size_t j = 0; j < n; j++) {
        V2 a = en[(j + n - 1) % n], c = en[j];
        double x = a.x + c.x, y = a.y + c.y, l = std::hypot(x, y);
        vn[j] = l > 0 ? V2{x / l, y / l} : c;
    }
    auto normal = [&](V2 n2, V2 s, double slope) {  // tilted back where the part tapers
        Vector3 v = Vector3Add(Vector3Scale(UP, (float)n2.x), Vector3Scale(Z, (float)n2.y));
        return Vector3Normalize(Vector3Subtract(v, Vector3Scale(D, (float)(slope * (n2.x * s.x + n2.y * s.y)))));
    };
    for (size_t i = 0; i + 1 < st.size(); i++) {
        double u0 = st[i].x, r0 = st[i].y, u1 = st[i + 1].x, r1 = st[i + 1].y, du = u1 - u0, dr = r1 - r0;
        if (r0 < 1e-6 && r1 < 1e-6) continue;
        if (std::fabs(du) < 1e-9) {  // a shoulder: a flat ring facing along the axis
            Vector3 f = dr > 0 ? Vector3Negate(D) : D;
            for (size_t j = 0; j < n; j++) {
                V2 a = S[j], c = S[(j + 1) % n];
                tri(at(u0, r0, a), at(u1, r1, a), at(u1, r1, c), f, f, f);
                tri(at(u0, r0, a), at(u1, r1, c), at(u0, r0, c), f, f, f);
            }
            continue;
        }
        double slope = dr / du;
        for (size_t j = 0; j < n; j++) {
            size_t k = (j + 1) % n;
            V2 a = S[j], c = S[k];
            bool sm = smooth[j] && smooth[k];
            Vector3 na = normal(sm ? vn[j] : en[j], a, slope), nc = normal(sm ? vn[k] : en[j], c, slope);
            tri(at(u0, r0, a), at(u1, r1, a), at(u1, r1, c), na, na, nc);
            tri(at(u0, r0, a), at(u1, r1, c), at(u0, r0, c), na, nc, nc);
        }
    }
    for (int end = 0; end < 2; end++) {  // flat ends, fanned from the axis
        const V2& e = end == 0 ? st.front() : st.back();
        if (e.y < 1e-6) continue;
        Vector3 cn = end == 0 ? Vector3Negate(D) : D, ctr = at(e.x, 0, {0, 0});
        for (size_t j = 0; j < n; j++) tri(ctr, at(e.x, e.y, S[j]), at(e.x, e.y, S[(j + 1) % n]), cn, cn, cn);
    }
    Mesh m{};
    m.vertexCount = (int)P.size() / 3; m.triangleCount = m.vertexCount / 3;
    m.vertices = (float*)MemAlloc(std::max<size_t>(1, P.size()) * sizeof(float));
    m.normals = (float*)MemAlloc(std::max<size_t>(1, Nm.size()) * sizeof(float));
    m.texcoords = (float*)MemAlloc(std::max(1, m.vertexCount) * 2 * sizeof(float));
    std::copy(P.begin(), P.end(), m.vertices);
    std::copy(Nm.begin(), Nm.end(), m.normals);
    for (int i = 0; i < m.vertexCount * 2; i++) m.texcoords[i] = 0;
    UploadMesh(&m, false);
    return m;
}

// Cut solids out of a part's mesh. `cutters` are closed solids in the mesh's own frame, in millimetres.
Mesh carveMesh(const Mesh& base, const std::vector<std::vector<csg::Poly>>& cutters) {
    std::vector<csg::Poly> solid;
    for (int t = 0; t < base.vertexCount / 3; t++) {
        std::vector<csg::Vert> v;
        csg::P3 ns{0, 0, 0};
        for (int k = 0; k < 3; k++) {
            int i = t * 3 + k;
            csg::P3 p{base.vertices[i * 3] / U, base.vertices[i * 3 + 1] / U, base.vertices[i * 3 + 2] / U};
            csg::P3 n{base.normals[i * 3], base.normals[i * 3 + 1], base.normals[i * 3 + 2]};
            v.push_back({p, n}); ns = ns + n;
        }
        csg::Poly q = csg::makePoly(v);
        if (csg::dot(csg::cross(v[1].p - v[0].p, v[2].p - v[0].p), csg::cross(v[1].p - v[0].p, v[2].p - v[0].p)) < 1e-14) continue;  // sliver
        if (csg::dot(q.pn, ns) < 0) { std::swap(v[1], v[2]); q = csg::makePoly(v); }  // keep every face pointing out
        solid.push_back(q);
    }
    for (auto& c : cutters) solid = csg::subtract(solid, c);
    int tris = 0;
    for (auto& q : solid) tris += (int)q.v.size() - 2;
    Mesh m{};
    m.vertexCount = tris * 3; m.triangleCount = tris;
    m.vertices = (float*)MemAlloc(std::max(1, m.vertexCount) * 3 * sizeof(float));
    m.normals = (float*)MemAlloc(std::max(1, m.vertexCount) * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(std::max(1, m.vertexCount) * 2 * sizeof(float));
    int k = 0;
    auto put = [&](const csg::Vert& v) {
        m.vertices[k * 3] = (float)(v.p.x * U); m.vertices[k * 3 + 1] = (float)(v.p.y * U); m.vertices[k * 3 + 2] = (float)(v.p.z * U);
        csg::P3 n = csg::unit(v.n);
        m.normals[k * 3] = (float)n.x; m.normals[k * 3 + 1] = (float)n.y; m.normals[k * 3 + 2] = (float)n.z;
        m.texcoords[k * 2] = 0; m.texcoords[k * 2 + 1] = 0; k++;
    };
    for (auto& q : solid)
        for (size_t i = 1; i + 1 < q.v.size(); i++) { put(q.v[0]); put(q.v[i]); put(q.v[i + 1]); }
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
    G.locNLamps = GetShaderLocation(G.lit, "nLamps");
    G.locLampPos = GetShaderLocation(G.lit, "lampPos"); G.locLampCol = GetShaderLocation(G.lit, "lampCol"); G.locLampDir = GetShaderLocation(G.lit, "lampDir");
    setLamps({});
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

Mesh depthMesh(const BodyDef& d, V2 c) {
    std::vector<P3d> v; std::vector<int> tri;
    depthTriangles(d, v, tri);
    int nt = (int)tri.size() / 3;
    Mesh m{};
    m.vertexCount = nt * 3; m.triangleCount = nt;
    m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.normals = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
    auto at = [&](int i) { return Vector3{(float)((v[i].x - c.x) * U), (float)(-(v[i].y - c.y) * U), (float)((v[i].z - d.z) * U)}; };
    int k = 0;
    for (int t = 0; t < nt; t++) {
        // turning y up mirrors the solid, so each triangle's order is turned round to keep it facing out
        Vector3 a = at(tri[t * 3]), b = at(tri[t * 3 + 2]), cc = at(tri[t * 3 + 1]);
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(cc, a)));
        for (Vector3 p : {a, b, cc}) {
            m.vertices[k * 3] = p.x; m.vertices[k * 3 + 1] = p.y; m.vertices[k * 3 + 2] = p.z;
            m.normals[k * 3] = n.x; m.normals[k * 3 + 1] = n.y; m.normals[k * 3 + 2] = n.z;
            m.texcoords[k * 2] = (float)(p.x / U / 40); m.texcoords[k * 2 + 1] = (float)(p.y / U / 40); k++;
        }
    }
    UploadMesh(&m, false);
    return m;
}

void drawArmorPlate(Vector3 base, float yawDeg, Color tint) {
    // a ballistic plate in an olive cover, gently curved: a thick middle and thinner shoulders, lying flat
    Matrix R = MatrixRotateY(-yawDeg * DEG2RAD);
    Color cover = tint.a ? tint : Color{78, 84, 62, 255}, edge{58, 62, 46, 255};
    drawBox(Vector3Add(base, {0, 0.014f, 0}), {0.25f, 0.024f, 0.30f}, edge, R);
    drawBox(Vector3Add(base, {0, 0.026f, 0}), {0.21f, 0.008f, 0.27f}, cover, R);
}

void drawAttachmentCrate(Vector3 base, float yawDeg, Color tint) {
    // an olive crate with a lid and two dark bands, the size of a footlocker
    Matrix R = MatrixRotateY(-yawDeg * DEG2RAD);
    Color body = tint.a ? tint : Color{86, 92, 60, 255}, lid{70, 75, 50, 255}, band{36, 38, 32, 255};
    drawBox(Vector3Add(base, {0, 0.22f, 0}), {0.9f, 0.44f, 0.5f}, body, R);
    drawBox(Vector3Add(base, {0, 0.46f, 0}), {0.94f, 0.05f, 0.54f}, lid, R);
    for (float x : {-0.28f, 0.28f}) {
        Vector3 off = Vector3Transform({x, 0.24f, 0}, R);
        drawBox(Vector3Add(base, off), {0.05f, 0.46f, 0.52f}, band, R);
    }
}
void drawAmmoBox(Vector3 base, float yawDeg, Color tint) {
    // a stack of olive steel ammo cans, each with its lid latch and carry handle
    Matrix R = MatrixRotateY(-yawDeg * DEG2RAD);
    Color can = tint.a ? tint : Color{78, 88, 54, 255}, dark{44, 48, 34, 255}, mark{214, 196, 96, 255};
    for (int k = 0; k < 3; k++) {
        Vector3 off = Vector3Transform({k == 2 ? 0.0f : (k ? 0.17f : -0.17f), k == 2 ? 0.2f : 0.0f, 0}, R);
        Vector3 c = Vector3Add(base, off);
        drawBox(Vector3Add(c, {0, 0.1f, 0}), {0.3f, 0.2f, 0.16f}, can, R);                       // the can
        drawBox(Vector3Add(c, {0, 0.205f, 0}), {0.31f, 0.012f, 0.17f}, dark, R);                 // its lid
        drawBox(Vector3Add(c, {0, 0.215f, 0}), {0.12f, 0.012f, 0.025f}, dark, R);                // the handle
        drawBox(Vector3Add(c, Vector3Transform({0, 0.1f, 0.081f}, R)), {0.12f, 0.035f, 0.004f}, mark, R);   // a painted band
    }
}
void drawWeaponCrate(Vector3 base, float yawDeg, Color tint) {
    // a long wooden rifle crate with rope handles at the ends and stencilled boards
    Matrix R = MatrixRotateY(-yawDeg * DEG2RAD);
    Color wood = tint.a ? tint : Color{122, 96, 60, 255}, slat{96, 74, 46, 255}, rope{168, 150, 110, 255};
    drawBox(Vector3Add(base, {0, 0.2f, 0}), {1.3f, 0.4f, 0.48f}, wood, R);
    for (float x : {-0.6f, 0.0f, 0.6f}) drawBox(Vector3Add(base, Vector3Transform({x, 0.2f, 0}, R)), {0.06f, 0.42f, 0.5f}, slat, R);
    drawBox(Vector3Add(base, {0, 0.41f, 0}), {1.32f, 0.03f, 0.5f}, slat, R);
    for (float x : {-0.67f, 0.67f}) drawBox(Vector3Add(base, Vector3Transform({x, 0.3f, 0}, R)), {0.03f, 0.04f, 0.2f}, rope, R);
}
void drawStation(const std::string& type, Vector3 base, float yawDeg, Color tint) {
    // career mode's workstations and the shop counter (metres; it faces along +x turned by yaw)
    Matrix R = MatrixRotateY(-yawDeg * DEG2RAD);
    auto at = [&](float x, float y, float z) { return Vector3Add(base, Vector3Transform({x, y, z}, R)); };
    auto box = [&](float x, float y, float z, float sx, float sy, float sz, Color c) { drawBox(at(x, y, z), {sx, sy, sz}, tint.a ? tint : c, R); };
    const Color wood{140, 102, 64, 255}, top{170, 128, 82, 255}, grey{96, 102, 110, 255}, dark{52, 56, 62, 255}, green{64, 92, 78, 255}, steel{150, 156, 164, 255};
    if (type == "station_bench") {        // a workbench: thick top, legs, a shelf, a vise and a pegboard of tools
        box(0, 0.88f, 0, 0.75f, 0.06f, 1.8f, top);
        for (float z : {-0.82f, 0.82f}) for (float x : {-0.3f, 0.3f}) box(x, 0.43f, z, 0.07f, 0.86f, 0.07f, wood);
        box(0, 0.2f, 0, 0.7f, 0.03f, 1.7f, wood);
        box(0.1f, 0.98f, 0.65f, 0.16f, 0.12f, 0.2f, dark);              // the vise
        box(-0.36f, 1.35f, 0, 0.03f, 0.8f, 1.6f, Color{186, 160, 120, 255});   // pegboard
        for (int k = 0; k < 5; k++) box(-0.33f, 1.2f + 0.12f * (k % 2), -0.6f + 0.3f * k, 0.03f, 0.22f, 0.04f, steel);
    } else if (type == "station_lathe") { // a lathe: a stand, a long bed, the headstock with its chuck, a tailstock
        box(0, 0.42f, 0, 0.5f, 0.84f, 1.6f, green);
        box(0, 0.92f, 0, 0.32f, 0.16f, 1.7f, grey);
        box(0, 1.15f, -0.62f, 0.42f, 0.42f, 0.42f, green);
        drawCyl(at(0, 1.12f, -0.4f), at(0, 1.12f, -0.3f), 0.11f, tint.a ? tint : dark);
        box(0, 1.1f, 0.6f, 0.26f, 0.22f, 0.22f, green);
        box(0.05f, 1.06f, 0.05f, 0.3f, 0.12f, 0.18f, grey);              // the carriage
    } else if (type == "station_mill") {  // a milling machine: a base, a column, the head and its table
        box(0, 0.45f, 0, 0.7f, 0.9f, 0.7f, green);
        box(-0.2f, 1.35f, 0, 0.3f, 0.9f, 0.4f, green);
        box(0.12f, 1.65f, 0, 0.5f, 0.35f, 0.35f, green);
        drawCyl(at(0.25f, 1.45f, 0), at(0.25f, 1.2f, 0), 0.04f, tint.a ? tint : steel);
        box(0.25f, 0.98f, 0, 0.4f, 0.08f, 1.0f, grey);
    } else if (type == "station_qc") {    // a test bench: a short bench, a bullet trap box with an orange face, a chronograph
        box(0, 0.8f, 0, 0.7f, 0.06f, 1.4f, top);
        for (float z : {-0.62f, 0.62f}) for (float x : {-0.28f, 0.28f}) box(x, 0.39f, z, 0.07f, 0.78f, 0.07f, wood);
        box(0, 1.02f, 0.45f, 0.4f, 0.38f, 0.4f, dark);
        box(0.21f, 1.02f, 0.45f, 0.02f, 0.3f, 0.3f, Color{236, 112, 34, 255});
        box(0.05f, 0.88f, -0.2f, 0.12f, 0.1f, 0.3f, Color{40, 44, 48, 255});
    } else if (type == "shop_counter") {  // a shop counter: a glass case on a wooden base, a till
        box(0, 0.45f, 0, 0.6f, 0.9f, 2.4f, wood);
        box(0, 0.95f, 0, 0.62f, 0.08f, 2.42f, top);
        drawBox(at(0, 1.0f, 0), {0.55f, 0.02f, 2.3f}, Color{170, 210, 230, 255}, R, 0.45f);   // (glass)
        box(0, 1.1f, -0.85f, 0.35f, 0.22f, 0.32f, dark);                // the till
    }
}

