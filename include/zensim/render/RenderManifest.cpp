/// @file RenderManifest.cpp
/// @brief RenderManifest JSON serialisation and output-dir helpers.

#include "zensim/render/RenderManifest.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace zs {
namespace render {

// ---------------------------------------------------------------
// Helper: enum → string
// ---------------------------------------------------------------

static const char* methodStr(RenderMethod m) {
  switch (m) {
    case RenderMethod::Raster_Forward:  return "raster_forward";
    case RenderMethod::Raster_Deferred: return "raster_deferred";
    case RenderMethod::PathTrace:       return "pathtrace";
    case RenderMethod::Hybrid:          return "hybrid";
    default:                            return "unknown";
  }
}

static const char* backendStr(RenderBackend b) {
  switch (b) {
    case RenderBackend::Vulkan: return "vulkan";
    case RenderBackend::CUDA:   return "cuda";
    default:                    return "unknown";
  }
}

static const char* artifactKindStr(ArtifactKind k) {
  switch (k) {
    case ArtifactKind::ColorImage:   return "color_image";
    case ArtifactKind::DepthImage:   return "depth_image";
    case ArtifactKind::TimingJSON:   return "timing_json";
    case ArtifactKind::ManifestJSON: return "manifest_json";
    case ArtifactKind::Log:          return "log";
    default:                        return "unknown";
  }
}

// ---------------------------------------------------------------
// Minimal JSON emitter (no external dependency)
// ---------------------------------------------------------------

/// Escape a string for JSON output.
static std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;      break;
    }
  }
  return out;
}

// ---------------------------------------------------------------
// writeJSON
// ---------------------------------------------------------------

std::string RenderManifest::writeJSON() const {
  if (output_dir.empty()) return {};

  // Ensure directory exists.
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    std::fprintf(stderr, "[RenderManifest] mkdir failed: %s\n",
                 ec.message().c_str());
    return {};
  }

  std::string path = output_dir + "/manifest.json";
  std::ofstream ofs(path);
  if (!ofs) {
    std::fprintf(stderr, "[RenderManifest] failed to open '%s'\n",
                 path.c_str());
    return {};
  }

  ofs << "{\n";
  ofs << "  \"run_id\": \"" << jsonEscape(run_id) << "\",\n";
  ofs << "  \"scene_name\": \"" << jsonEscape(scene_name) << "\",\n";
  ofs << "  \"method\": \"" << methodStr(method) << "\",\n";
  ofs << "  \"backend\": \"" << backendStr(backend) << "\",\n";
  ofs << "  \"total_time_us\": " << total_time_us << ",\n";
  ofs << "  \"artifact_count\": " << artifacts.size() << ",\n";
  ofs << "  \"artifacts\": [\n";

  for (size_t i = 0; i < artifacts.size(); ++i) {
    const auto& a = artifacts[i];
    ofs << "    {\n";
    ofs << "      \"frame_id\": " << a.frame_id << ",\n";
    ofs << "      \"view_name\": \"" << jsonEscape(a.view_name) << "\",\n";
    ofs << "      \"kind\": \"" << artifactKindStr(a.kind) << "\",\n";
    ofs << "      \"file_path\": \"" << jsonEscape(a.file_path) << "\",\n";
    ofs << "      \"width\": " << a.width << ",\n";
    ofs << "      \"height\": " << a.height << ",\n";
    ofs << "      \"render_time_us\": " << a.render_time_us << ",\n";
    ofs << "      \"backend\": \"" << backendStr(a.backend) << "\"\n";
    ofs << "    }";
    if (i + 1 < artifacts.size()) ofs << ",";
    ofs << "\n";
  }

  ofs << "  ]\n";
  ofs << "}\n";

  ofs.close();
  return path;
}

// ---------------------------------------------------------------
// buildOutputDir
// ---------------------------------------------------------------

std::string RenderManifest::buildOutputDir(const std::string& artifact_root,
                                           const std::string& scene_name,
                                           RenderMethod method,
                                           RenderBackend backend,
                                           const std::string& run_id) {
  // Pattern: <root>/runs/<scene>/<method>/<backend>/<run_id>/
  std::ostringstream oss;
  oss << artifact_root
      << "/runs"
      << "/" << (scene_name.empty() ? "unnamed" : scene_name)
      << "/" << methodStr(method)
      << "/" << backendStr(backend)
      << "/" << run_id;
  return oss.str();
}

}  // namespace render
}  // namespace zs
