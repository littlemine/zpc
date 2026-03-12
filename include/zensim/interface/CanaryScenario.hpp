#pragma once

#include <vector>

#include "zensim/interface/InterfaceServices.hpp"

namespace zs {

  enum class CanaryParameterKind : u8 { boolean, integer, floating_point, string, enumeration };

  struct CanaryParameterOption {
    SmallString value{};
    SmallString label{};
  };

  struct CanaryParameterDescriptor {
    SmallString name{};
    SmallString label{};
    SmallString description{};
    SmallString unit{};
    SmallString defaultValue{};
    SmallString minValue{};
    SmallString maxValue{};
    CanaryParameterKind kind{CanaryParameterKind::string};
    bool required{false};
    std::vector<CanaryParameterOption> options{};
  };

  struct CanaryScenarioDescriptor {
    SmallString scenarioId{};
    SmallString label{};
    SmallString description{};
    SmallString version{};
    std::vector<SmallString> systems{};
    std::vector<SmallString> metrics{};
    std::vector<ValidationMetadataEntry> metadata{};
    std::vector<CanaryParameterDescriptor> parameters{};
  };

  struct CanaryParameterOverride {
    SmallString name{};
    SmallString value{};
  };

  struct CanaryScenarioRunRequest {
    InterfaceSessionHandle session{};
    SmallString scenarioId{};
    SmallString baselineId{};
    std::vector<CanaryParameterOverride> overrides{};
    bool compareAgainstBaseline{true};
  };

  struct CanaryScenarioRunResult {
    SmallString scenarioId{};
    ValidationSuiteReport report{};
    ValidationComparisonReport comparison{};
    bool hasComparison{false};
    bool accepted{true};
  };

  class CanaryScenarioService {
  public:
    virtual ~CanaryScenarioService() = default;

    virtual std::vector<CanaryScenarioDescriptor> list_scenarios(
        InterfaceSessionHandle session) const = 0;
    virtual bool describe_scenario(InterfaceSessionHandle session, const SmallString &scenarioId,
                                   CanaryScenarioDescriptor *descriptor) const = 0;
    virtual CanaryScenarioRunResult run_scenario(const CanaryScenarioRunRequest &request) = 0;
  };

}  // namespace zs
