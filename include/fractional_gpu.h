/* This file is header file for exposing API to external application */
#ifndef __FRACTIONAL_GPU_H__
#define __FRACTIONAL_GPU_H__

#include <inttypes.h>

#include <persistent.h>

typedef struct fgpu_bindexes fgpu_bindexes_t;
typedef struct fgpu_indicators fgpu_indicators_t;

/* This structure is context that is handed over to kernel by host */
typedef struct fgpu_ctx {
    volatile fgpu_indicators_t *d_indicators;  /* Used to indicate launch completion */
    fgpu_bindexes_t *d_bindex;        /* Used to gather block indexes */
    int color;                      /* Color to be used by the kernel */
    int index;                      /* Index within the color */
    uint3 gridDim;                  /* User provided grid dimensions */
    uint3 blockDim;                 /* User provided block dimensions */
    int num_blocks;                 /* Number of blocks to be spawned */
    int start_sm;
    int end_sm;
} fgpu_ctx_t;

int fgpu_init(void);
void fgpu_deinit(void);
int fgpu_prepare_launch_kernel(fgpu_ctx_t *ctx, uint3 *_gridDim, cudaStream_t **stream);
int fgpu_wait_for_kernel(int tag);


/* Macro to launch kernel - Returns a tag - Negative if error */
#define FGPU_LAUNCH_KERNEL(_color, _gridDim, _blockDim, sharedMem, func, ...)  \
({                                                                          \
    fgpu_ctx_t fctx;                                                        \
    int tag;                                                                \
    uint3 _lgridDim;                                                        \
    cudaStream_t *stream;                                                   \
    fctx.color = _color;                                                    \
    fctx.gridDim = _gridDim;                                                \
    fctx.blockDim = _blockDim;                                              \
    tag = fgpu_prepare_launch_kernel(&fctx, &_lgridDim, &stream);           \
    if (tag >= 0) {                                                         \
        func<<<_lgridDim, _blockDim, sharedMem, *stream>>>(fctx, __VA_ARGS__); \
    }                                                                       \
                                                                            \
    tag;                                                                    \
})

#define FGPU_LAUNCH_VOID_KERNEL(_color, _gridDim, _blockDim, sharedMem, func)  \
({                                                                          \
    fgpu_ctx_t fctx;                                                        \
    int tag;                                                                \
    uint3 _lgridDim;                                                        \
    cudaStream_t *stream;                                                   \
    fctx.color = _color;                                                    \
    fctx.gridDim = _gridDim;                                                \
    fctx.blockDim = _blockDim;                                              \
    tag = fgpu_prepare_launch_kernel(&fctx, &_lgridDim, &stream);           \
    if (tag >= 0) {                                                         \
        func<<<_lgridDim, _blockDim, sharedMem, *stream>>>(fctx);           \
    }                                                                       \
                                                                            \
    tag;                                                                    \
})

/* Macro to define (modified) kernels (with no args) */
#define FGPU_DEFINE_VOID_KERNEL(func)                                       \
    __global__ void func(fgpu_ctx_t fctx)

/* Macro to define (modified) kernels */
#define FGPU_DEFINE_KERNEL(func, ...)                                       \
    __global__ void func(fgpu_ctx_t fctx, __VA_ARGS__)

__device__ __forceinline__
int fgpu_device_init(const fgpu_ctx_t *ctx)
{
    uint sm;
    asm("mov.u32 %0, %smid;" : "=r"(sm));
    if (threadIdx.x == 0 && threadIdx.y == 0 && threadIdx.z == 0) {
        ctx->d_indicators->indicators[blockIdx.x].started[ctx->color] = true;
    }
    if (sm < ctx->start_sm || sm > ctx->end_sm) {
        return -1;
    }
    /* Prepare for the next function */
    ctx->d_bindex->bindexes[ctx->color].index[ctx->index ^ 1] = 0;
    return 0;
}

__device__ __forceinline__
int fgpu_device_get_blockIdx(const fgpu_ctx_t *ctx, uint3 *_blockIdx)
{
    __shared__ int lblockIdx;
    __shared__ uint3 lblockIdx3D;

    if (threadIdx.x == 0 && threadIdx.y == 0 && threadIdx.z == 0) {
        uint blocks_left;
        uint num2Dblocks;
        uint x, y, z;

        lblockIdx = atomicAdd(&ctx->d_bindex->bindexes[ctx->color].index[ctx->index], 1);

        num2Dblocks = ctx->gridDim.x * ctx->gridDim.y;
        z = lblockIdx / (num2Dblocks);
        blocks_left = lblockIdx - (z * num2Dblocks);
        y = blocks_left / ctx->gridDim.x;
        x = blocks_left - y * ctx->gridDim.x;
        lblockIdx3D.x = x;
        lblockIdx3D.y = y;
        lblockIdx3D.z = z;
    }
    __syncthreads();

    if (lblockIdx >= ctx->num_blocks)
        return -1;
    
    *_blockIdx = lblockIdx3D;

    return 0;
}

#define FGPU_DEVICE_INIT()                                                  \
    if (fgpu_device_init(&fctx) < 0)                                        \
        return;

#define FGPU_FOR_EACH_DEVICE_BLOCK(_blockIdx)                               \
    for (; fgpu_device_get_blockIdx(&fctx, &_blockIdx) == 0;)

#endif /* FRACTIONAL_GPU */
