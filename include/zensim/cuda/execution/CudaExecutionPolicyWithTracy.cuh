#pragma once

#include "zensim/execution/TracyHelper.hpp"
#include "ExecutionPolicy.cuh"

namespace zs_ext {

#ifdef ZS_ENABLE_TRACY
struct CudaExecutionWithTracy: public zs::CudaExecutionPolicy {

    template<typename Ts, typename Is, typename F>
    void operator()(zs::Collapse<Ts, Is> dims, F &&f,
                    const zs::source_location &loc = zs::source_location::current()) {
        static const tracy::SourceLocationData tracy_source_loc{
            nullptr,
            loc.function_name(),
            loc.file_name(), static_cast<uint32_t>(loc.line()), 0};
        tracy::ScopedZone tracy_scoped_zone(&tracy_source_loc, 0, true);
        // 调用基类实现
        zs::CudaExecutionPolicy::operator()(dims, std::forward<F>(f), loc);
    }

    template<typename Range, typename F>
    auto operator()(Range &&range, F &&f,
    const zs::source_location &loc = zs::source_location::current()) const {
        static const tracy::SourceLocationData tracy_source_loc{
            nullptr,
            loc.function_name(),
            loc.file_name(), static_cast<uint32_t>(loc.line()), 0};
        tracy::ScopedZone tracy_scoped_zone(&tracy_source_loc, 0, true);
        // 调用基类实现
        zs::CudaExecutionPolicy::operator()(std::forward<Range>(range), std::forward<F>(f), loc);
    }

    template<typename Range, typename... Args, typename F>
    auto operator()(Range &&range, const zs::tuple<Args...> &params, F &&f,
    const zs::source_location &loc = zs::source_location::current()) const {
        static const tracy::SourceLocationData tracy_source_loc{
            nullptr,
            loc.function_name(),
            loc.file_name(), static_cast<uint32_t>(loc.line()), 0};
        tracy::ScopedZone tracy_scoped_zone(&tracy_source_loc, 0, true);
        // 调用基类实现
        zs::CudaExecutionPolicy::operator()(std::forward<Range>(range), params, std::forward<F>(f), loc);
    }
};
  constexpr CudaExecutionWithTracy cuda_exec_with_tracy() noexcept { return CudaExecutionWithTracy{}; }
#endif

}