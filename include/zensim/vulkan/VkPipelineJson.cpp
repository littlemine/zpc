#include "zensim/vulkan/VkPipelineSerialization.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

#include "zensim/zpc_tpls/fmt/format.h"
#include "zensim/zpc_tpls/magic_enum/magic_enum.hpp"
#include "zensim/zpc_tpls/rapidjson/document.h"
#include "zensim/zpc_tpls/rapidjson/istreamwrapper.h"
#include "zensim/zpc_tpls/rapidjson/ostreamwrapper.h"
#include "zensim/zpc_tpls/rapidjson/prettywriter.h"
#include "zensim/zpc_tpls/rapidjson/stringbuffer.h"

namespace zs {

  // ============================================================================
  // Aliases
  // ============================================================================

  using RjValue = rapidjson::Value;
  using RjWriter = rapidjson::PrettyWriter<rapidjson::OStreamWrapper>;

  // ============================================================================
  // Write helpers (RapidJSON PrettyWriter based)
  // ============================================================================

  namespace {

    template <typename E> void jw_enum(RjWriter& w, const char* key, E v) {
      w.Key(key);
      w.Int(static_cast<int32_t>(v));
      auto name = magic_enum::enum_name(v);
      if (!name.empty()) {
        auto nameKey = fmt::format("{}_name", key);
        w.Key(nameKey.c_str());
        w.String(name.data(), static_cast<rapidjson::SizeType>(name.size()));
      }
    }

    template <typename F> void jw_flags(RjWriter& w, const char* key, F v) {
      w.Key(key);
      w.Uint(static_cast<uint32_t>(static_cast<typename F::MaskType>(v)));
    }

    void jw_stencil_op(RjWriter& w, const char* key, const vk::StencilOpState& s) {
      w.Key(key);
      w.StartObject();
      jw_enum(w, "failOp", s.failOp);
      jw_enum(w, "passOp", s.passOp);
      jw_enum(w, "depthFailOp", s.depthFailOp);
      jw_enum(w, "compareOp", s.compareOp);
      w.Key("compareMask");
      w.Uint(s.compareMask);
      w.Key("writeMask");
      w.Uint(s.writeMask);
      w.Key("reference");
      w.Uint(s.reference);
      w.EndObject();
    }

  }  // namespace

  // ============================================================================
  // Read helpers (RapidJSON DOM based)
  // ============================================================================

  namespace {

    bool jr_bool(const RjValue& v, const char* key, bool def = false) {
      auto it = v.FindMember(key);
      return (it != v.MemberEnd() && it->value.IsBool()) ? it->value.GetBool() : def;
    }
    uint32_t jr_u32(const RjValue& v, const char* key, uint32_t def = 0) {
      auto it = v.FindMember(key);
      return (it != v.MemberEnd() && it->value.IsUint()) ? it->value.GetUint() : def;
    }
    int32_t jr_i32(const RjValue& v, const char* key, int32_t def = 0) {
      auto it = v.FindMember(key);
      return (it != v.MemberEnd() && it->value.IsInt()) ? it->value.GetInt() : def;
    }
    uint64_t jr_u64(const RjValue& v, const char* key, uint64_t def = 0) {
      auto it = v.FindMember(key);
      return (it != v.MemberEnd() && it->value.IsUint64()) ? it->value.GetUint64() : def;
    }
    float jr_float(const RjValue& v, const char* key, float def = 0.f) {
      auto it = v.FindMember(key);
      if (it != v.MemberEnd()) {
        if (it->value.IsFloat()) return it->value.GetFloat();
        if (it->value.IsDouble()) return static_cast<float>(it->value.GetDouble());
        if (it->value.IsInt()) return static_cast<float>(it->value.GetInt());
      }
      return def;
    }
    std::string jr_str(const RjValue& v, const char* key, const char* def = "") {
      auto it = v.FindMember(key);
      return (it != v.MemberEnd() && it->value.IsString())
                 ? std::string(it->value.GetString(), it->value.GetStringLength())
                 : std::string(def);
    }

    vk::StencilOpState jr_read_stencil_op(const RjValue& v) {
      vk::StencilOpState s{};
      s.failOp = static_cast<vk::StencilOp>(jr_i32(v, "failOp"));
      s.passOp = static_cast<vk::StencilOp>(jr_i32(v, "passOp"));
      s.depthFailOp = static_cast<vk::StencilOp>(jr_i32(v, "depthFailOp"));
      s.compareOp = static_cast<vk::CompareOp>(jr_i32(v, "compareOp"));
      s.compareMask = jr_u32(v, "compareMask");
      s.writeMask = jr_u32(v, "writeMask");
      s.reference = jr_u32(v, "reference");
      return s;
    }

    rapidjson::Document jr_parse(std::istream& is) {
      rapidjson::IStreamWrapper isw(is);
      rapidjson::Document doc;
      doc.ParseStream(isw);
      if (doc.HasParseError())
        throw std::runtime_error(
            fmt::format("JSON parse error at offset {}: code {}",
                        doc.GetErrorOffset(), static_cast<int>(doc.GetParseError())));
      return doc;
    }

  }  // namespace

  // ============================================================================
  // Meta (version header) -- write / read
  // ============================================================================

  namespace {

    void write_meta_obj(RjWriter& w, const DescFileHeader& hdr) {
      w.Key("_meta");
      w.StartObject();
      w.Key("magic");
      w.String("ZSPD");
      w.Key("formatVersion");
      w.Uint(hdr.formatVersion);
      w.Key("vkHeaderVersion");
      w.String(fmt::format("0x{:08x}", hdr.vkHeaderVersion).c_str());
      w.Key("zpcVersion");
      w.String(fmt::format("0x{:08x}", hdr.zpcVersion).c_str());
      w.EndObject();
    }

    DescFileHeader read_meta_from_value(const RjValue& m) {
      DescFileHeader hdr{};
      auto magic = jr_str(m, "magic");
      if (magic == "ZSPD")
        hdr.magic = 0x4450535A;
      else
        throw std::runtime_error("JSON _meta: unexpected magic '" + magic + "'");
      hdr.formatVersion = jr_u32(m, "formatVersion");
      auto vkStr = jr_str(m, "vkHeaderVersion");
      if (!vkStr.empty()) hdr.vkHeaderVersion = static_cast<u32>(std::stoul(vkStr, nullptr, 16));
      auto zpcStr = jr_str(m, "zpcVersion");
      if (!zpcStr.empty()) hdr.zpcVersion = static_cast<u32>(std::stoul(zpcStr, nullptr, 16));
      return hdr;
    }

    void read_meta_and_validate(const RjValue& doc, const char* context) {
      if (doc.HasMember("_meta")) {
        auto hdr = read_meta_from_value(doc["_meta"]);
        std::string err;
        if (!validate_header(hdr, &err))
          throw std::runtime_error(fmt::format("{} JSON: {}", context, err));
      }
    }

  }  // namespace

  void write_json_meta(std::ostream& os, const DescFileHeader& hdr, int /*indent*/) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    w.StartObject();
    write_meta_obj(w, hdr);
    w.EndObject();
  }

  DescFileHeader read_json_meta(std::istream& is) {
    auto doc = jr_parse(is);
    if (!doc.IsObject() || !doc.HasMember("_meta")) return {};
    return read_meta_from_value(doc["_meta"]);
  }

  // ============================================================================
  // Internal write helpers -- emit sub-desc into an active RjWriter
  // ============================================================================

  namespace {

    void write_vertex_input(RjWriter& w, const VertexInputStateDesc& desc) {
      w.StartObject();
      w.Key("bindings");
      w.StartArray();
      for (const auto& b : desc.bindings) {
        w.StartObject();
        w.Key("binding");
        w.Uint(b.binding);
        w.Key("stride");
        w.Uint(b.stride);
        jw_enum(w, "inputRate", b.inputRate);
        w.EndObject();
      }
      w.EndArray();
      w.Key("attributes");
      w.StartArray();
      for (const auto& a : desc.attributes) {
        w.StartObject();
        w.Key("location");
        w.Uint(a.location);
        w.Key("binding");
        w.Uint(a.binding);
        jw_enum(w, "format", a.format);
        w.Key("offset");
        w.Uint(a.offset);
        w.EndObject();
      }
      w.EndArray();
      w.EndObject();
    }

    void write_input_assembly(RjWriter& w, const InputAssemblyStateDesc& desc) {
      w.StartObject();
      jw_enum(w, "topology", desc.topology);
      w.Key("primitiveRestartEnable");
      w.Bool(desc.primitiveRestartEnable);
      w.EndObject();
    }

    void write_viewport(RjWriter& w, const ViewportStateDesc& desc) {
      w.StartObject();
      w.Key("viewportCount");
      w.Uint(desc.viewportCount);
      w.Key("scissorCount");
      w.Uint(desc.scissorCount);
      w.EndObject();
    }

    void write_rasterization(RjWriter& w, const RasterizationStateDesc& desc) {
      w.StartObject();
      w.Key("depthClampEnable");
      w.Bool(desc.depthClampEnable);
      w.Key("rasterizerDiscardEnable");
      w.Bool(desc.rasterizerDiscardEnable);
      jw_enum(w, "polygonMode", desc.polygonMode);
      jw_flags(w, "cullMode", desc.cullMode);
      jw_enum(w, "frontFace", desc.frontFace);
      w.Key("depthBiasEnable");
      w.Bool(desc.depthBiasEnable);
      w.Key("lineWidth");
      w.Double(static_cast<double>(desc.lineWidth));
      w.Key("depthBiasConstantFactor");
      w.Double(static_cast<double>(desc.depthBiasConstantFactor));
      w.Key("depthBiasClamp");
      w.Double(static_cast<double>(desc.depthBiasClamp));
      w.Key("depthBiasSlopeFactor");
      w.Double(static_cast<double>(desc.depthBiasSlopeFactor));
      w.EndObject();
    }

    void write_multisample(RjWriter& w, const MultisampleStateDesc& desc) {
      w.StartObject();
      w.Key("sampleShadingEnable");
      w.Bool(desc.sampleShadingEnable);
      jw_enum(w, "rasterizationSamples", desc.rasterizationSamples);
      w.Key("minSampleShading");
      w.Double(static_cast<double>(desc.minSampleShading));
      w.Key("alphaToCoverageEnable");
      w.Bool(desc.alphaToCoverageEnable);
      w.Key("alphaToOneEnable");
      w.Bool(desc.alphaToOneEnable);
      w.EndObject();
    }

    void write_depth_stencil(RjWriter& w, const DepthStencilStateDesc& desc) {
      w.StartObject();
      w.Key("depthTestEnable");
      w.Bool(desc.depthTestEnable);
      w.Key("depthWriteEnable");
      w.Bool(desc.depthWriteEnable);
      jw_enum(w, "depthCompareOp", desc.depthCompareOp);
      w.Key("depthBoundsTestEnable");
      w.Bool(desc.depthBoundsTestEnable);
      w.Key("stencilTestEnable");
      w.Bool(desc.stencilTestEnable);
      jw_stencil_op(w, "front", desc.front);
      jw_stencil_op(w, "back", desc.back);
      w.Key("minDepthBounds");
      w.Double(static_cast<double>(desc.minDepthBounds));
      w.Key("maxDepthBounds");
      w.Double(static_cast<double>(desc.maxDepthBounds));
      w.EndObject();
    }

    void write_color_blend(RjWriter& w, const ColorBlendStateDesc& desc) {
      w.StartObject();
      w.Key("logicOpEnable");
      w.Bool(desc.logicOpEnable);
      jw_enum(w, "logicOp", desc.logicOp);
      w.Key("attachments");
      w.StartArray();
      for (const auto& a : desc.attachments) {
        w.StartObject();
        w.Key("blendEnable");
        w.Bool(a.blendEnable);
        jw_enum(w, "srcColorBlendFactor", a.srcColorBlendFactor);
        jw_enum(w, "dstColorBlendFactor", a.dstColorBlendFactor);
        jw_enum(w, "colorBlendOp", a.colorBlendOp);
        jw_enum(w, "srcAlphaBlendFactor", a.srcAlphaBlendFactor);
        jw_enum(w, "dstAlphaBlendFactor", a.dstAlphaBlendFactor);
        jw_enum(w, "alphaBlendOp", a.alphaBlendOp);
        w.Key("colorWriteMask");
        w.Uint(static_cast<uint32_t>(static_cast<VkColorComponentFlags>(a.colorWriteMask)));
        w.EndObject();
      }
      w.EndArray();
      w.Key("blendConstants");
      w.StartArray();
      for (int i = 0; i < 4; ++i) w.Double(static_cast<double>(desc.blendConstants[i]));
      w.EndArray();
      w.EndObject();
    }

    void write_dynamic_state(RjWriter& w, const DynamicStateDesc& desc) {
      w.StartObject();
      w.Key("states");
      w.StartArray();
      for (const auto& s : desc.states) w.Int(static_cast<int32_t>(s));
      w.EndArray();
      w.Key("states_names");
      w.StartArray();
      for (const auto& s : desc.states) {
        auto name = magic_enum::enum_name(s);
        w.String(name.data(), static_cast<rapidjson::SizeType>(name.size()));
      }
      w.EndArray();
      w.EndObject();
    }

    void write_shader_stage(RjWriter& w, const ShaderStageDesc& desc) {
      w.StartObject();
      jw_enum(w, "stage", desc.stage);
      w.Key("entryPoint");
      w.String(desc.entryPoint.c_str());
      if (!desc.sourceKey.empty()) {
        w.Key("sourceKey");
        w.String(desc.sourceKey.c_str());
      }
      w.Key("spirvWordCount");
      w.Uint(static_cast<uint32_t>(desc.spirv.size()));
      w.Key("spirv");
      w.StartArray();
      for (const auto& word : desc.spirv)
        w.String(fmt::format("0x{:08x}", word).c_str());
      w.EndArray();
      w.EndObject();
    }

    // ---- per-desc read from RjValue ----

    void read_vertex_input(const RjValue& v, VertexInputStateDesc& desc) {
      desc = {};
      if (v.HasMember("bindings") && v["bindings"].IsArray()) {
        for (const auto& b : v["bindings"].GetArray()) {
          vk::VertexInputBindingDescription bd{};
          bd.binding = jr_u32(b, "binding");
          bd.stride = jr_u32(b, "stride");
          bd.inputRate = static_cast<vk::VertexInputRate>(jr_i32(b, "inputRate"));
          desc.bindings.push_back(bd);
        }
      }
      if (v.HasMember("attributes") && v["attributes"].IsArray()) {
        for (const auto& a : v["attributes"].GetArray()) {
          vk::VertexInputAttributeDescription ad{};
          ad.location = jr_u32(a, "location");
          ad.binding = jr_u32(a, "binding");
          ad.format = static_cast<vk::Format>(jr_i32(a, "format"));
          ad.offset = jr_u32(a, "offset");
          desc.attributes.push_back(ad);
        }
      }
    }

    void read_input_assembly(const RjValue& v, InputAssemblyStateDesc& desc) {
      desc = {};
      desc.topology = static_cast<vk::PrimitiveTopology>(jr_i32(v, "topology"));
      desc.primitiveRestartEnable = jr_bool(v, "primitiveRestartEnable");
    }

    void read_viewport(const RjValue& v, ViewportStateDesc& desc) {
      desc = {};
      desc.viewportCount = jr_u32(v, "viewportCount", 1);
      desc.scissorCount = jr_u32(v, "scissorCount", 1);
    }

    void read_rasterization(const RjValue& v, RasterizationStateDesc& desc) {
      desc = {};
      desc.depthClampEnable = jr_bool(v, "depthClampEnable");
      desc.rasterizerDiscardEnable = jr_bool(v, "rasterizerDiscardEnable");
      desc.polygonMode = static_cast<vk::PolygonMode>(jr_i32(v, "polygonMode"));
      desc.cullMode = vk::CullModeFlags(static_cast<VkCullModeFlags>(jr_u32(v, "cullMode")));
      desc.frontFace = static_cast<vk::FrontFace>(jr_i32(v, "frontFace"));
      desc.depthBiasEnable = jr_bool(v, "depthBiasEnable");
      desc.lineWidth = jr_float(v, "lineWidth", 1.f);
      desc.depthBiasConstantFactor = jr_float(v, "depthBiasConstantFactor");
      desc.depthBiasClamp = jr_float(v, "depthBiasClamp");
      desc.depthBiasSlopeFactor = jr_float(v, "depthBiasSlopeFactor");
    }

    void read_multisample(const RjValue& v, MultisampleStateDesc& desc) {
      desc = {};
      desc.sampleShadingEnable = jr_bool(v, "sampleShadingEnable");
      desc.rasterizationSamples
          = static_cast<vk::SampleCountFlagBits>(jr_i32(v, "rasterizationSamples", 1));
      desc.minSampleShading = jr_float(v, "minSampleShading", 1.f);
      desc.alphaToCoverageEnable = jr_bool(v, "alphaToCoverageEnable");
      desc.alphaToOneEnable = jr_bool(v, "alphaToOneEnable");
    }

    void read_depth_stencil(const RjValue& v, DepthStencilStateDesc& desc) {
      desc = {};
      desc.depthTestEnable = jr_bool(v, "depthTestEnable", true);
      desc.depthWriteEnable = jr_bool(v, "depthWriteEnable", true);
      desc.depthCompareOp = static_cast<vk::CompareOp>(jr_i32(v, "depthCompareOp", 3));
      desc.depthBoundsTestEnable = jr_bool(v, "depthBoundsTestEnable");
      desc.stencilTestEnable = jr_bool(v, "stencilTestEnable");
      if (v.HasMember("front") && v["front"].IsObject())
        desc.front = jr_read_stencil_op(v["front"]);
      if (v.HasMember("back") && v["back"].IsObject())
        desc.back = jr_read_stencil_op(v["back"]);
      desc.minDepthBounds = jr_float(v, "minDepthBounds");
      desc.maxDepthBounds = jr_float(v, "maxDepthBounds", 1.f);
    }

    void read_color_blend(const RjValue& v, ColorBlendStateDesc& desc) {
      desc = {};
      desc.logicOpEnable = jr_bool(v, "logicOpEnable");
      desc.logicOp = static_cast<vk::LogicOp>(jr_i32(v, "logicOp"));
      if (v.HasMember("attachments") && v["attachments"].IsArray()) {
        for (const auto& a : v["attachments"].GetArray()) {
          vk::PipelineColorBlendAttachmentState s{};
          s.blendEnable = jr_bool(a, "blendEnable");
          s.srcColorBlendFactor = static_cast<vk::BlendFactor>(jr_i32(a, "srcColorBlendFactor"));
          s.dstColorBlendFactor = static_cast<vk::BlendFactor>(jr_i32(a, "dstColorBlendFactor"));
          s.colorBlendOp = static_cast<vk::BlendOp>(jr_i32(a, "colorBlendOp"));
          s.srcAlphaBlendFactor = static_cast<vk::BlendFactor>(jr_i32(a, "srcAlphaBlendFactor"));
          s.dstAlphaBlendFactor = static_cast<vk::BlendFactor>(jr_i32(a, "dstAlphaBlendFactor"));
          s.alphaBlendOp = static_cast<vk::BlendOp>(jr_i32(a, "alphaBlendOp"));
          s.colorWriteMask = vk::ColorComponentFlags(
              static_cast<VkColorComponentFlags>(jr_u32(a, "colorWriteMask")));
          desc.attachments.push_back(s);
        }
      }
      if (v.HasMember("blendConstants") && v["blendConstants"].IsArray()) {
        const auto& arr = v["blendConstants"].GetArray();
        for (rapidjson::SizeType i = 0; i < arr.Size() && i < 4; ++i) {
          if (arr[i].IsDouble())
            desc.blendConstants[i] = static_cast<float>(arr[i].GetDouble());
          else if (arr[i].IsFloat())
            desc.blendConstants[i] = arr[i].GetFloat();
          else if (arr[i].IsInt())
            desc.blendConstants[i] = static_cast<float>(arr[i].GetInt());
        }
      }
    }

    void read_dynamic_state(const RjValue& v, DynamicStateDesc& desc) {
      desc = {};
      if (v.HasMember("states") && v["states"].IsArray()) {
        for (const auto& s : v["states"].GetArray())
          desc.states.push_back(static_cast<vk::DynamicState>(s.GetInt()));
      }
    }

    void read_shader_stage(const RjValue& v, ShaderStageDesc& desc) {
      desc = {};
      desc.stage = static_cast<vk::ShaderStageFlagBits>(jr_i32(v, "stage"));
      desc.entryPoint = jr_str(v, "entryPoint", "main");
      desc.sourceKey = jr_str(v, "sourceKey", "");
      if (v.HasMember("spirv") && v["spirv"].IsArray()) {
        for (const auto& s : v["spirv"].GetArray()) {
          if (s.IsString())
            desc.spirv.push_back(static_cast<u32>(std::stoul(s.GetString(), nullptr, 16)));
        }
      }
    }

  }  // namespace

  // ============================================================================
  // Public write_json / read_json -- individual sub-descriptors
  // ============================================================================

  void write_json(std::ostream& os, const VertexInputStateDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    write_vertex_input(w, desc);
  }
  void read_json(std::istream& is, VertexInputStateDesc& desc) {
    read_vertex_input(jr_parse(is), desc);
  }

  void write_json(std::ostream& os, const InputAssemblyStateDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    write_input_assembly(w, desc);
  }
  void read_json(std::istream& is, InputAssemblyStateDesc& desc) {
    read_input_assembly(jr_parse(is), desc);
  }

  void write_json(std::ostream& os, const ViewportStateDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    write_viewport(w, desc);
  }
  void read_json(std::istream& is, ViewportStateDesc& desc) {
    read_viewport(jr_parse(is), desc);
  }

  void write_json(std::ostream& os, const RasterizationStateDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    write_rasterization(w, desc);
  }
  void read_json(std::istream& is, RasterizationStateDesc& desc) {
    read_rasterization(jr_parse(is), desc);
  }

  void write_json(std::ostream& os, const MultisampleStateDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    write_multisample(w, desc);
  }
  void read_json(std::istream& is, MultisampleStateDesc& desc) {
    read_multisample(jr_parse(is), desc);
  }

  void write_json(std::ostream& os, const DepthStencilStateDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    write_depth_stencil(w, desc);
  }
  void read_json(std::istream& is, DepthStencilStateDesc& desc) {
    read_depth_stencil(jr_parse(is), desc);
  }

  void write_json(std::ostream& os, const ColorBlendStateDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    write_color_blend(w, desc);
  }
  void read_json(std::istream& is, ColorBlendStateDesc& desc) {
    read_color_blend(jr_parse(is), desc);
  }

  void write_json(std::ostream& os, const DynamicStateDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    write_dynamic_state(w, desc);
  }
  void read_json(std::istream& is, DynamicStateDesc& desc) {
    read_dynamic_state(jr_parse(is), desc);
  }

  void write_json(std::ostream& os, const ShaderStageDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    write_shader_stage(w, desc);
  }
  void read_json(std::istream& is, ShaderStageDesc& desc) {
    read_shader_stage(jr_parse(is), desc);
  }

  // ============================================================================
  // GraphicsPipelineDesc
  // ============================================================================

  void write_json(std::ostream& os, const GraphicsPipelineDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    w.StartObject();

    write_meta_obj(w, current_desc_file_header());

    w.Key("shaderStages");
    w.StartArray();
    for (const auto& s : desc.shaderStages) write_shader_stage(w, s);
    w.EndArray();

    w.Key("vertexInput");
    write_vertex_input(w, desc.vertexInput);
    w.Key("inputAssembly");
    write_input_assembly(w, desc.inputAssembly);
    w.Key("viewport");
    write_viewport(w, desc.viewport);
    w.Key("rasterization");
    write_rasterization(w, desc.rasterization);
    w.Key("multisample");
    write_multisample(w, desc.multisample);
    w.Key("depthStencil");
    write_depth_stencil(w, desc.depthStencil);
    w.Key("colorBlend");
    write_color_blend(w, desc.colorBlend);
    w.Key("dynamicState");
    write_dynamic_state(w, desc.dynamicState);

    w.Key("pushConstantRanges");
    w.StartArray();
    for (const auto& r : desc.pushConstantRanges) {
      w.StartObject();
      w.Key("stageFlags");
      w.Uint(static_cast<uint32_t>(static_cast<VkShaderStageFlags>(r.stageFlags)));
      w.Key("offset");
      w.Uint(r.offset);
      w.Key("size");
      w.Uint(r.size);
      w.EndObject();
    }
    w.EndArray();

    w.Key("subpass");
    w.Uint(desc.subpass);

    w.EndObject();
  }

  void read_json(std::istream& is, GraphicsPipelineDesc& desc) {
    auto doc = jr_parse(is);
    desc = {};
    read_meta_and_validate(doc, "GraphicsPipelineDesc");

    if (doc.HasMember("shaderStages") && doc["shaderStages"].IsArray()) {
      for (const auto& s : doc["shaderStages"].GetArray()) {
        ShaderStageDesc sd;
        read_shader_stage(s, sd);
        desc.shaderStages.push_back(std::move(sd));
      }
    }
    if (doc.HasMember("vertexInput")) read_vertex_input(doc["vertexInput"], desc.vertexInput);
    if (doc.HasMember("inputAssembly"))
      read_input_assembly(doc["inputAssembly"], desc.inputAssembly);
    if (doc.HasMember("viewport")) read_viewport(doc["viewport"], desc.viewport);
    if (doc.HasMember("rasterization"))
      read_rasterization(doc["rasterization"], desc.rasterization);
    if (doc.HasMember("multisample")) read_multisample(doc["multisample"], desc.multisample);
    if (doc.HasMember("depthStencil")) read_depth_stencil(doc["depthStencil"], desc.depthStencil);
    if (doc.HasMember("colorBlend")) read_color_blend(doc["colorBlend"], desc.colorBlend);
    if (doc.HasMember("dynamicState")) read_dynamic_state(doc["dynamicState"], desc.dynamicState);
    if (doc.HasMember("pushConstantRanges") && doc["pushConstantRanges"].IsArray()) {
      for (const auto& r : doc["pushConstantRanges"].GetArray()) {
        vk::PushConstantRange pcr{};
        pcr.stageFlags
            = vk::ShaderStageFlags(static_cast<VkShaderStageFlags>(jr_u32(r, "stageFlags")));
        pcr.offset = jr_u32(r, "offset");
        pcr.size = jr_u32(r, "size");
        desc.pushConstantRanges.push_back(pcr);
      }
    }
    desc.subpass = jr_u32(doc, "subpass");
  }

  // ============================================================================
  // TransientBufferDesc
  // ============================================================================

  void write_json(std::ostream& os, const TransientBufferDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    w.StartObject();
    write_meta_obj(w, current_desc_file_header());
    w.Key("size");
    w.Uint64(desc.size);
    jw_flags(w, "usage", desc.usage);
    jw_flags(w, "memoryProperties", desc.memoryProperties);
    w.EndObject();
  }

  void read_json(std::istream& is, TransientBufferDesc& desc) {
    auto doc = jr_parse(is);
    desc = {};
    read_meta_and_validate(doc, "TransientBufferDesc");
    desc.size = static_cast<vk::DeviceSize>(jr_u64(doc, "size"));
    desc.usage = vk::BufferUsageFlags(static_cast<VkBufferUsageFlags>(jr_u32(doc, "usage")));
    desc.memoryProperties = vk::MemoryPropertyFlags(
        static_cast<VkMemoryPropertyFlags>(jr_u32(doc, "memoryProperties")));
  }

  // ============================================================================
  // TransientImageDesc
  // ============================================================================

  void write_json(std::ostream& os, const TransientImageDesc& desc, int) {
    rapidjson::OStreamWrapper osw(os);
    RjWriter w(osw);
    w.SetIndent(' ', 2);
    w.StartObject();
    write_meta_obj(w, current_desc_file_header());

    w.Key("extent");
    w.StartObject();
    w.Key("width");
    w.Uint(desc.extent.width);
    w.Key("height");
    w.Uint(desc.extent.height);
    w.Key("depth");
    w.Uint(desc.extent.depth);
    w.EndObject();

    jw_enum(w, "format", desc.format);
    jw_flags(w, "usage", desc.usage);
    jw_enum(w, "samples", desc.samples);
    w.Key("mipLevels");
    w.Uint(desc.mipLevels);
    w.Key("arrayLayers");
    w.Uint(desc.arrayLayers);
    jw_enum(w, "imageType", desc.imageType);
    jw_flags(w, "memoryProperties", desc.memoryProperties);
    w.EndObject();
  }

  void read_json(std::istream& is, TransientImageDesc& desc) {
    auto doc = jr_parse(is);
    desc = {};
    read_meta_and_validate(doc, "TransientImageDesc");
    if (doc.HasMember("extent") && doc["extent"].IsObject()) {
      const auto& e = doc["extent"];
      desc.extent.width = jr_u32(e, "width");
      desc.extent.height = jr_u32(e, "height");
      desc.extent.depth = jr_u32(e, "depth", 1);
    }
    desc.format = static_cast<vk::Format>(jr_i32(doc, "format"));
    desc.usage = vk::ImageUsageFlags(static_cast<VkImageUsageFlags>(jr_u32(doc, "usage")));
    desc.samples = static_cast<vk::SampleCountFlagBits>(jr_i32(doc, "samples", 1));
    desc.mipLevels = jr_u32(doc, "mipLevels", 1);
    desc.arrayLayers = jr_u32(doc, "arrayLayers", 1);
    desc.imageType = static_cast<vk::ImageType>(jr_i32(doc, "imageType"));
    desc.memoryProperties = vk::MemoryPropertyFlags(
        static_cast<VkMemoryPropertyFlags>(jr_u32(doc, "memoryProperties")));
  }

}  // namespace zs
