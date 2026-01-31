#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <limits>
#include <stdexcept>

// Coin3D / Open Inventor
#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoUnits.h>

// tinygltf (you must add this file to your repo, see notes below)
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NOEXCEPTION
#include "tiny_gltf.h"

struct MeshOut {
  std::vector<float> positions;   // xyz xyz xyz ...
  std::vector<uint32_t> indices;  // 0..N-1
  float posMin[3] = { +std::numeric_limits<float>::infinity(),
                      +std::numeric_limits<float>::infinity(),
                      +std::numeric_limits<float>::infinity() };
  float posMax[3] = { -std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity() };
};

static inline void updateMinMax(MeshOut &m, float x, float y, float z) {
  if (x < m.posMin[0]) m.posMin[0] = x;
  if (y < m.posMin[1]) m.posMin[1] = y;
  if (z < m.posMin[2]) m.posMin[2] = z;
  if (x > m.posMax[0]) m.posMax[0] = x;
  if (y > m.posMax[1]) m.posMax[1] = y;
  if (z > m.posMax[2]) m.posMax[2] = z;
}

static double unitsScaleToMeters(SoUnits::Units u) {
  // MVP: only handle the most common CAD case explicitly; default = identity.
  // (Coin exposes current units state to SoCallbackAction.) [web:248]
  switch (u) {
    case SoUnits::MILLIMETERS: return 0.001;
    case SoUnits::CENTIMETERS: return 0.01;
    case SoUnits::METERS:      return 1.0;
    case SoUnits::KILOMETERS:  return 1000.0;
    case SoUnits::INCHES:      return 0.0254;
    case SoUnits::FEET:        return 0.3048;
    case SoUnits::YARDS:       return 0.9144;
    case SoUnits::MILES:       return 1609.344;
    default:                   return 1.0;
  }
}

// Triangle callback: called as shapes generate primitives. [web:248]
static void triangleCB(void *userdata,
                       SoCallbackAction *action,
                       const SoPrimitiveVertex *v1,
                       const SoPrimitiveVertex *v2,
                       const SoPrimitiveVertex *v3) {
  MeshOut *out = reinterpret_cast<MeshOut *>(userdata);

  // World/model transform at this point in the scene graph. [web:248]
  const SbMatrix &model = action->getModelMatrix();

  // Unit scale from current traversal state. [web:248]
  const double scale = unitsScaleToMeters(action->getUnits());

  auto pushVertex = [&](const SoPrimitiveVertex *v) -> uint32_t {
    SbVec3f p = v->getPoint();
    SbVec3f wp;
    model.multVecMatrix(p, wp);

    const float x = static_cast<float>(wp[0] * scale);
    const float y = static_cast<float>(wp[1] * scale);
    const float z = static_cast<float>(wp[2] * scale);

    uint32_t idx = static_cast<uint32_t>(out->positions.size() / 3);
    out->positions.push_back(x);
    out->positions.push_back(y);
    out->positions.push_back(z);
    updateMinMax(*out, x, y, z);
    return idx;
  };

  const uint32_t i1 = pushVertex(v1);
  const uint32_t i2 = pushVertex(v2);
  const uint32_t i3 = pushVertex(v3);

  out->indices.push_back(i1);
  out->indices.push_back(i2);
  out->indices.push_back(i3);
}

static bool writeGLB(const MeshOut &mesh, const std::string &outPath, std::string &err) {
  if (mesh.positions.empty() || mesh.indices.empty()) {
    err = "No triangles extracted from scene graph.";
    return false;
  }

  tinygltf::Model model;
  model.asset.version = "2.0";
  model.asset.generator = "coin3d-iv2glb-mvp";

  // --- Pack buffers ---
  const size_t posBytes = mesh.positions.size() * sizeof(float);
  const size_t idxBytes = mesh.indices.size() * sizeof(uint32_t);

  tinygltf::Buffer buffer;
  buffer.data.resize(posBytes + idxBytes);

  // positions at offset 0
  std::memcpy(buffer.data.data(), mesh.positions.data(), posBytes);
  // indices after positions
  std::memcpy(buffer.data.data() + posBytes, mesh.indices.data(), idxBytes);

  model.buffers.push_back(std::move(buffer));

  // BufferView: positions
  tinygltf::BufferView bvPos;
  bvPos.buffer = 0;
  bvPos.byteOffset = 0;
  bvPos.byteLength = posBytes;
  bvPos.target = TINYGLTF_TARGET_ARRAY_BUFFER;
  model.bufferViews.push_back(bvPos);
  const int bvPosIndex = static_cast<int>(model.bufferViews.size() - 1);

  // BufferView: indices
  tinygltf::BufferView bvIdx;
  bvIdx.buffer = 0;
  bvIdx.byteOffset = posBytes;
  bvIdx.byteLength = idxBytes;
  bvIdx.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
  model.bufferViews.push_back(bvIdx);
  const int bvIdxIndex = static_cast<int>(model.bufferViews.size() - 1);

  // Accessor: positions
  tinygltf::Accessor accPos;
  accPos.bufferView = bvPosIndex;
  accPos.byteOffset = 0;
  accPos.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
  accPos.count = mesh.positions.size() / 3;
  accPos.type = TINYGLTF_TYPE_VEC3;
  accPos.minValues = { mesh.posMin[0], mesh.posMin[1], mesh.posMin[2] };
  accPos.maxValues = { mesh.posMax[0], mesh.posMax[1], mesh.posMax[2] };
  model.accessors.push_back(accPos);
  const int accPosIndex = static_cast<int>(model.accessors.size() - 1);

  // Accessor: indices
  tinygltf::Accessor accIdx;
  accIdx.bufferView = bvIdxIndex;
  accIdx.byteOffset = 0;
  accIdx.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
  accIdx.count = mesh.indices.size();
  accIdx.type = TINYGLTF_TYPE_SCALAR;
  model.accessors.push_back(accIdx);
  const int accIdxIndex = static_cast<int>(model.accessors.size() - 1);

  // Default material (plain grey)
  tinygltf::Material mat;
  mat.pbrMetallicRoughness.baseColorFactor = {0.8, 0.8, 0.8, 1.0};
  mat.pbrMetallicRoughness.metallicFactor = 0.0;
  mat.pbrMetallicRoughness.roughnessFactor = 1.0;
  model.materials.push_back(mat);
  const int matIndex = static_cast<int>(model.materials.size() - 1);

  // Primitive
  tinygltf::Primitive prim;
  prim.attributes["POSITION"] = accPosIndex;
  prim.indices = accIdxIndex;
  prim.material = matIndex;
  prim.mode = TINYGLTF_MODE_TRIANGLES;

  // Mesh
  tinygltf::Mesh gltfMesh;
  gltfMesh.primitives.push_back(prim);
  model.meshes.push_back(gltfMesh);
  const int meshIndex = static_cast<int>(model.meshes.size() - 1);

  // Node
  tinygltf::Node node;
  node.mesh = meshIndex;
  model.nodes.push_back(node);
  const int nodeIndex = static_cast<int>(model.nodes.size() - 1);

  // Scene
  tinygltf::Scene scene;
  scene.nodes.push_back(nodeIndex);
  model.scenes.push_back(scene);
  model.defaultScene = 0;

  tinygltf::TinyGLTF gltf;
  std::string warn;
  const bool ok = gltf.WriteGltfSceneToFile(
      &model,
      outPath,
      /*embedImages*/ true,
      /*embedBuffers*/ true,
      /*prettyPrint*/ false,
      /*writeBinary*/ true);

  if (!warn.empty()) {
    // tinygltf collects warnings in some builds; keep err minimal.
  }
  if (!ok) {
    err = "tinygltf failed to write GLB.";
    return false;
  }
  return true;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "Usage: iv2glb <input.iv> <output.glb>\n");
    return 2;
  }

  const std::string inPath = argv[1];
  const std::string outPath = argv[2];

  // Initialize Coin database (required before reading). [web:211]
  SoDB::init();

  SoInput in;
  if (!in.openFile(inPath.c_str())) { // Open Inventor file input. [web:209]
    std::fprintf(stderr, "Failed to open input file: %s\n", inPath.c_str());
    return 3;
  }

  SoNode *root = SoDB::readAll(&in); // Read full scene graph. [web:211]
  if (!root) {
    std::fprintf(stderr, "SoDB::readAll() failed (invalid/unsupported .iv).\n");
    return 4;
  }
  root->ref();

  MeshOut mesh;

  SoCallbackAction action;
  action.addTriangleCallback(SoShape::getClassTypeId(), triangleCB, &mesh); // [web:248]
  action.apply(root);

  root->unref();

  std::string err;
  if (!writeGLB(mesh, outPath, err)) {
    std::fprintf(stderr, "GLB export failed: %s\n", err.c_str());
    return 5;
  }

  std::fprintf(stdout, "OK: wrote %s (%zu triangles)\n",
               outPath.c_str(), mesh.indices.size() / 3);
  return 0;
}
