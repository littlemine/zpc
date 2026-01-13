#pragma once

#ifdef ZS_ENABLE_TRACY

#if ZS_ENABLE_CUDA && defined(__CUDACC__)
#include "tracy/TracyCUDA.hpp" // 或 client 相关头
#else
#include "tracy/Tracy.hpp"
#endif



#define ZS_PROFILE_NOOP TracyNoop
#define ZS_PROFILE ZoneScoped
#define ZS_PROFILE_FRAME(x) FrameMark
#define ZS_PROFILE_SECTION(x) ZoneScopedN(x)
#define ZS_PROFILE_TAG(y, x) ZoneText(x, strlen(x))
#define ZS_PROFILE_LOG(text, size) TracyMessage(text, size)
#define ZS_PROFILE_VALUE(text, value) TracyPlot(text, value)


#if ZS_ENABLE_CUDA && defined(__CUDACC__)
struct ScopedTracyCudaContext {
    tracy::CUDACtx *ctx = nullptr;
    ScopedTracyCudaContext() {
        ctx = TracyCUDAContext(); //  new tracy::CUDACtx;
        TracyCUDAStartProfiling(ctx);
    }
    ~ScopedTracyCudaContext() {
        TracyCUDAStopProfiling(ctx);
        // TracyCUDACollect(ctx);
        TracyCUDAContextDestroy(ctx);
    }
};
#endif

#else

#define ZS_PROFILE_NOOP
#define ZS_PROFILE
#define ZS_PROFILE_FRAME(x)
#define ZS_PROFILE_SECTION(x)
#define ZS_PROFILE_TAG(y, x)
#define ZS_PROFILE_LOG(text, size)
#define ZS_PROFILE_VALUE(text, value)
#endif