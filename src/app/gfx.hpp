// Gunsmith3D - Graphics kit: lit shader, primitive meshes and extrusion.
#pragma once
#include "csg.hpp"
#include "app/app.hpp"

struct Gfx {
    Shader lit{};
    int locView = -1, locFog = -1, locSpec = -1;
    int locLight = -1, locLightCol = -1, locFill = -1, locFillCol = -1, locAmbSky = -1, locAmbGround = -1, locFogParam = -1;
    Material mat{};
    Mesh cube{}, cyl{}, cone{}, sphere{}, plate{};
    Texture2D plateTex{};
    Material plateMat{};
};
extern Gfx G;

// The lit shader's lights. The defaults are the workshop's (designer and bench); a map sets its own sun and sky.
struct Lighting {
    float dir[3] = {-0.42f, 0.85f, 0.55f}, col[3] = {1.0f, 0.96f, 0.90f};
    float fill[3] = {0.6f, 0.15f, 0.75f}, fillCol[3] = {0.78f, 0.86f, 1.0f};
    float ambSky[3] = {0.70f, 0.75f, 0.84f}, ambGround[3] = {0.42f, 0.41f, 0.40f};
    float fog[3] = {90.f, 500.f, 0.65f};   // start distance, length, strongest fog
};
void setLighting(const Lighting& L);
void setSpecular(float s);          // 0 matte .. 1 polished, for the following draws
void setFog(Color c);               // far things fade to this colour
void drawMesh(const Mesh& m, Matrix M, Color c, float alpha = 1.f);
void drawMeshTex(const Mesh& m, Matrix M, Color c, Texture2D tex, float alpha = 1);  // with a picture on it (tinted by c)
void drawBox(Vector3 center, Vector3 size, Color c, Matrix rot = MatrixIdentity(), float alpha = 1.f);
void drawAttachmentCrate(Vector3 base, float yawDeg, Color tint = Color{0, 0, 0, 0});   // a crate of attachments (metres)
void drawArmorPlate(Vector3 base, float yawDeg, Color tint = Color{0, 0, 0, 0});   // an armour plate lying flat (metres)
void drawCyl(Vector3 a, Vector3 b, float r, Color c, float alpha = 1.f, const Mesh* mesh = nullptr);
void drawSphereLit(Vector3 p, float r, Color c, float alpha = 1.f);
Mesh extrude(const std::vector<V2>& localMM, float depth, float roundMM = 0);
// A part shaped in depth (BodyDef::depthOff): its solid, flat-shaded, in the mesh frame (centred on c and the part's
// depth, y up)
struct BodyDef;
Mesh depthMesh(const BodyDef& d, V2 c);
Mesh latheMesh(const std::vector<V2>& st, V2 o, V2 dir, const std::vector<V2>& S, const std::vector<char>& smooth);  // a round (turned) part
Mesh carveMesh(const Mesh& base, const std::vector<std::vector<csg::Poly>>& cutters);  // base minus the cutters (mm, mesh frame)
void initGfx();
