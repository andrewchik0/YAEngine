#include "CoreComponentSerializers.h"

#include "ComponentRegistry.h"
#include "Components.h"
#include "YamlUtils.h"
#include "Assets/AssetManager.h"

namespace YAEngine
{

  static void SerializeTextureField(YAML::Node& node, const std::string& key,
    TextureHandle handle, TextureManager& textures, AssetManager& assets)
  {
    if (!handle) return;
    auto info = textures.GetPath(handle);
    if (info.path.empty()) return;
    YAML::Node texNode;
    texNode["path"] = assets.MakeRelative(info.path);
    if (info.linear)
      texNode["linear"] = true;
    node[key] = texNode;
  }

  static TextureHandle DeserializeTextureField(const YAML::Node& node, const std::string& key,
    AssetManager& assets, bool* hasAlpha = nullptr)
  {
    if (!node[key]) return {};
    auto texNode = node[key];
    if (texNode.IsScalar())
      return assets.Textures().Load(assets.ResolvePath(texNode.as<std::string>()), hasAlpha);
    auto path = texNode["path"].as<std::string>();
    bool linear = texNode["linear"] ? texNode["linear"].as<bool>() : false;
    return assets.Textures().Load(assets.ResolvePath(path), hasAlpha, linear);
  }

  void RegisterCoreComponentSerializers(ComponentRegistry& registry, AssetManager& assets)
  {
    // CameraComponent
    registry.Register<CameraComponent>("camera",
      [](const entt::registry& reg, entt::entity e) -> YAML::Node {
        auto& c = reg.get<CameraComponent>(e);
        YAML::Node n;
        n["fov"] = c.fov;
        n["nearPlane"] = c.nearPlane;
        n["farPlane"] = c.farPlane;
        return n;
      },
      [](entt::registry& reg, entt::entity e, const YAML::Node& n) {
        CameraComponent c;
        if (n["fov"]) c.fov = n["fov"].as<float>();
        if (n["nearPlane"]) c.nearPlane = n["nearPlane"].as<float>();
        if (n["farPlane"]) c.farPlane = n["farPlane"].as<float>();
        reg.emplace_or_replace<CameraComponent>(e, c);
      }
    );

    // LightComponent
    registry.Register<LightComponent>("light",
      [](const entt::registry& reg, entt::entity e) -> YAML::Node {
        auto& l = reg.get<LightComponent>(e);
        YAML::Node n;
        const char* typeStr = "point";
        if (l.type == LightType::Spot) typeStr = "spot";
        else if (l.type == LightType::Directional) typeStr = "directional";
        n["type"] = typeStr;
        n["color"] = SerializeVec3(l.color);
        n["intensity"] = l.intensity;
        if (l.type != LightType::Directional)
          n["radius"] = l.radius;
        if (l.type == LightType::Spot)
        {
          n["innerCone"] = l.innerCone;
          n["outerCone"] = l.outerCone;
        }
        if (l.castShadow)
          n["castShadow"] = true;
        return n;
      },
      [](entt::registry& reg, entt::entity e, const YAML::Node& n) {
        LightComponent l;
        if (n["type"])
        {
          auto t = n["type"].as<std::string>();
          if (t == "spot") l.type = LightType::Spot;
          else if (t == "directional") l.type = LightType::Directional;
          else l.type = LightType::Point;
        }
        if (n["color"]) l.color = DeserializeVec3(n["color"]);
        if (n["intensity"]) l.intensity = n["intensity"].as<float>();
        if (n["radius"]) l.radius = n["radius"].as<float>();
        if (n["innerCone"]) l.innerCone = n["innerCone"].as<float>();
        if (n["outerCone"]) l.outerCone = n["outerCone"].as<float>();
        if (n["castShadow"]) l.castShadow = n["castShadow"].as<bool>();
        reg.emplace_or_replace<LightComponent>(e, l);
      }
    );

    // MeshComponent (primitive meshes only)
    registry.Register<MeshComponent>("mesh",
      [&assets](const entt::registry& reg, entt::entity e) -> YAML::Node {
        auto& mc = reg.get<MeshComponent>(e);
        PrimitiveSource src;
        if (!assets.Primitives().GetSource(mc.asset, src))
          return {};

        YAML::Node n;
        switch (src.type)
        {
          case PrimitiveType::Sphere:
            n["primitive"] = "sphere";
            n["radius"] = src.params.radius;
            n["stacks"] = src.params.stacks;
            n["slices"] = src.params.slices;
            break;
          case PrimitiveType::Box:
            n["primitive"] = "box";
            n["sizeX"] = src.params.sizeX;
            n["sizeY"] = src.params.sizeY;
            n["sizeZ"] = src.params.sizeZ;
            break;
          case PrimitiveType::Plane:
            n["primitive"] = "plane";
            n["size"] = src.params.size;
            n["uvScale"] = src.params.uvScale;
            break;
        }
        return n;
      },
      [&assets](entt::registry& reg, entt::entity e, const YAML::Node& n) {
        auto primName = n["primitive"].as<std::string>();
        PrimitiveParams params;
        MeshHandle handle;

        if (primName == "sphere")
        {
          if (n["radius"]) params.radius = n["radius"].as<float>();
          if (n["stacks"]) params.stacks = n["stacks"].as<uint32_t>();
          if (n["slices"]) params.slices = n["slices"].as<uint32_t>();
          handle = assets.Primitives().Create(PrimitiveType::Sphere, params);
        }
        else if (primName == "box")
        {
          if (n["sizeX"]) params.sizeX = n["sizeX"].as<float>();
          if (n["sizeY"]) params.sizeY = n["sizeY"].as<float>();
          if (n["sizeZ"]) params.sizeZ = n["sizeZ"].as<float>();
          handle = assets.Primitives().Create(PrimitiveType::Box, params);
        }
        else if (primName == "plane")
        {
          if (n["size"]) params.size = n["size"].as<float>();
          if (n["uvScale"]) params.uvScale = n["uvScale"].as<float>();
          handle = assets.Primitives().Create(PrimitiveType::Plane, params);
        }

        if (handle)
          reg.emplace_or_replace<MeshComponent>(e, handle);
      }
    );

    // MaterialComponent (inline material definition)
    registry.Register<MaterialComponent>("material",
      [&assets](const entt::registry& reg, entt::entity e) -> YAML::Node {
        auto& mc = reg.get<MaterialComponent>(e);
        if (!assets.Materials().Has(mc.asset))
          return {};

        auto& mat = assets.Materials().Get(mc.asset);
        YAML::Node n;

        if (!mat.name.empty()) n["name"] = mat.name;
        n["albedo"] = SerializeVec3(mat.albedo);
        n["roughness"] = mat.roughness;
        n["metallic"] = mat.metallic;

        if (mat.emissivity != glm::vec3(0))
          n["emissivity"] = SerializeVec3(mat.emissivity);
        if (mat.specular != 0.5f)
          n["specular"] = mat.specular;
        if (mat.doubleSided)
          n["doubleSided"] = true;
        if (mat.shadingModel != ShadingModel::Lit)
          n["shadingModel"] = "unlit";

        auto& textures = assets.Textures();
        SerializeTextureField(n, "baseColorTexture", mat.baseColorTexture, textures, assets);
        SerializeTextureField(n, "metallicTexture", mat.metallicTexture, textures, assets);
        SerializeTextureField(n, "roughnessTexture", mat.roughnessTexture, textures, assets);
        SerializeTextureField(n, "specularTexture", mat.specularTexture, textures, assets);
        SerializeTextureField(n, "emissiveTexture", mat.emissiveTexture, textures, assets);
        SerializeTextureField(n, "normalTexture", mat.normalTexture, textures, assets);
        SerializeTextureField(n, "heightTexture", mat.heightTexture, textures, assets);

        return n;
      },
      [&assets](entt::registry& reg, entt::entity e, const YAML::Node& n) {
        auto handle = assets.Materials().Create();
        auto& mat = assets.Materials().Get(handle);

        if (n["name"]) mat.name = n["name"].as<std::string>();
        if (n["albedo"]) mat.albedo = DeserializeVec3(n["albedo"]);
        if (n["roughness"]) mat.roughness = n["roughness"].as<float>();
        if (n["metallic"]) mat.metallic = n["metallic"].as<float>();
        if (n["emissivity"]) mat.emissivity = DeserializeVec3(n["emissivity"]);
        if (n["specular"]) mat.specular = n["specular"].as<float>();
        if (n["doubleSided"]) mat.doubleSided = n["doubleSided"].as<bool>();
        if (n["shadingModel"])
        {
          auto sm = n["shadingModel"].as<std::string>();
          mat.shadingModel = (sm == "unlit") ? ShadingModel::Unlit : ShadingModel::Lit;
        }

        mat.baseColorTexture = DeserializeTextureField(n, "baseColorTexture", assets, &mat.hasAlpha);
        mat.metallicTexture = DeserializeTextureField(n, "metallicTexture", assets);
        mat.roughnessTexture = DeserializeTextureField(n, "roughnessTexture", assets);
        mat.specularTexture = DeserializeTextureField(n, "specularTexture", assets);
        mat.emissiveTexture = DeserializeTextureField(n, "emissiveTexture", assets);
        mat.normalTexture = DeserializeTextureField(n, "normalTexture", assets);
        mat.heightTexture = DeserializeTextureField(n, "heightTexture", assets);

        reg.emplace_or_replace<MaterialComponent>(e, handle);
      }
    );

    // ModelSourceComponent
    registry.Register<ModelSourceComponent>("model",
      [&assets](const entt::registry& reg, entt::entity e) -> YAML::Node {
        auto& m = reg.get<ModelSourceComponent>(e);
        YAML::Node n;
        n["path"] = assets.MakeRelative(m.path);
        if (m.combinedTextures)
          n["combinedTextures"] = true;
        return n;
      },
      [](entt::registry& reg, entt::entity e, const YAML::Node& n) {
        ModelSourceComponent m;
        m.path = n["path"].as<std::string>();
        if (n["combinedTextures"]) m.combinedTextures = n["combinedTextures"].as<bool>();
        reg.emplace_or_replace<ModelSourceComponent>(e, m);
      }
    );

    // HiddenTag
    registry.Register<HiddenTag>("hidden",
      [](const entt::registry& reg, entt::entity e) -> YAML::Node {
        YAML::Node n;
        n = true;
        return n;
      },
      [](entt::registry& reg, entt::entity e, const YAML::Node&) {
        if (!reg.all_of<HiddenTag>(e))
          reg.emplace<HiddenTag>(e);
      }
    );
  }
}
