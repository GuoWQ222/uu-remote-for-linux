#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#define UU_EXPORT __attribute__((visibility("default")))
#else
#define UU_EXPORT
#endif

#define UU_FRAME_BLOCK_BYTES 256u

struct uu_frame_update_stats {
    uint64_t compared_bytes;
    uint64_t copied_bytes;
    uint64_t source_changed_bytes;
    uint32_t changed_rows;
    uint32_t changed_blocks;
    uint32_t temporal_deferred_blocks;
    uint32_t temporal_suppressed_blocks;
    uint32_t temporal_published_blocks;
    uint32_t motion_bypass;
};

struct uu_frame_temporal_filter {
    size_t row_bytes;
    size_t rows;
    size_t blocks_per_row;
};

static int uu_valid_plane(size_t stride, size_t row_bytes, size_t rows)
{
    if (!row_bytes || !rows || stride < row_bytes)
        return 0;
    return rows == 1u || stride <= (SIZE_MAX - row_bytes) / (rows - 1u);
}

UU_EXPORT int uu_frame_copy_2d(
    unsigned char *target,
    size_t target_stride,
    const unsigned char *source,
    size_t source_stride,
    size_t row_bytes,
    size_t rows)
{
    size_t row;

    if (!target || !source ||
        !uu_valid_plane(target_stride, row_bytes, rows) ||
        !uu_valid_plane(source_stride, row_bytes, rows))
        return -EINVAL;
    for (row = 0; row < rows; ++row)
        memcpy(target + row * target_stride,
               source + row * source_stride, row_bytes);
    return 0;
}

UU_EXPORT void *uu_frame_temporal_filter_create(
    size_t row_bytes,
    size_t rows)
{
    struct uu_frame_temporal_filter *filter;
    size_t blocks_per_row;

    if (!row_bytes || !rows || row_bytes > SIZE_MAX - (UU_FRAME_BLOCK_BYTES - 1u))
        return NULL;
    blocks_per_row =
        (row_bytes + UU_FRAME_BLOCK_BYTES - 1u) / UU_FRAME_BLOCK_BYTES;
    if (rows > SIZE_MAX / row_bytes || rows > SIZE_MAX / blocks_per_row)
        return NULL;
    filter = (struct uu_frame_temporal_filter *)calloc(1u, sizeof(*filter));
    if (!filter)
        return NULL;
    filter->row_bytes = row_bytes;
    filter->rows = rows;
    filter->blocks_per_row = blocks_per_row;
    return filter;
}

UU_EXPORT void uu_frame_temporal_filter_destroy(void *opaque)
{
    struct uu_frame_temporal_filter *filter =
        (struct uu_frame_temporal_filter *)opaque;

    if (!filter)
        return;
    free(filter);
}

/*
 * Atomically publish one compositor sample.  The ABI retains the historical
 * temporal name because the Python bridge loads this symbol, but no block may
 * select pixels from an older sample: after a successful update the complete
 * target region is byte-for-byte equal to this source frame.  This preserves
 * latest-wins ordering for IME preedit text, candidate windows and window
 * borders instead of assembling one output from independently delayed blocks.
 *
 * The inactive target is repaired even when source equals current, so a later
 * sibling-monitor publication cannot expose stale pixels.  Motion telemetry is
 * based on the exact number of changed bytes rather than charging a full
 * 256-byte block for a one-byte difference.
 */
UU_EXPORT int uu_frame_update_temporal_2d(
    void *opaque,
    unsigned char *target,
    size_t target_stride,
    const unsigned char *current,
    size_t current_stride,
    const unsigned char *source,
    size_t source_stride,
    size_t row_bytes,
    size_t rows,
    uint32_t motion_ratio_denominator,
    int force,
    struct uu_frame_update_stats *stats)
{
    struct uu_frame_temporal_filter *filter =
        (struct uu_frame_temporal_filter *)opaque;
    size_t frame_bytes;
    size_t motion_threshold;
    size_t source_changed_bytes = 0;
    size_t row;
    int output_changed = force != 0;

    if (stats)
        memset(stats, 0, sizeof(*stats));
    if (!filter || !target || !current || !source ||
        filter->row_bytes != row_bytes || filter->rows != rows ||
        !uu_valid_plane(target_stride, row_bytes, rows) ||
        !uu_valid_plane(current_stride, row_bytes, rows) ||
        !uu_valid_plane(source_stride, row_bytes, rows) ||
        !motion_ratio_denominator ||
        rows > SIZE_MAX / row_bytes)
        return -EINVAL;

    for (row = 0; row < rows; ++row) {
        size_t offset;
        int row_changed = 0;

        for (offset = 0; offset < row_bytes; offset += UU_FRAME_BLOCK_BYTES) {
            const unsigned char *current_block =
                current + row * current_stride + offset;
            const unsigned char *source_block =
                source + row * source_stride + offset;
            unsigned char *target_block =
                target + row * target_stride + offset;
            size_t block = row_bytes - offset;
            size_t index;

            if (block > UU_FRAME_BLOCK_BYTES)
                block = UU_FRAME_BLOCK_BYTES;
            if (stats)
                stats->compared_bytes += block;
            if (memcmp(source_block, current_block, block) != 0) {
                output_changed = 1;
                for (index = 0; index < block; ++index)
                    if (source_block[index] != current_block[index])
                        ++source_changed_bytes;
            }
            if (memcmp(target_block, source_block, block) != 0) {
                memcpy(target_block, source_block, block);
                row_changed = 1;
                if (stats) {
                    stats->copied_bytes += block;
                    ++stats->changed_blocks;
                }
            }
        }
        if (row_changed && stats)
            ++stats->changed_rows;
    }
    frame_bytes = row_bytes * rows;
    motion_threshold = frame_bytes / motion_ratio_denominator +
        (frame_bytes % motion_ratio_denominator != 0u);
    if (stats) {
        stats->source_changed_bytes = source_changed_bytes;
        stats->motion_bypass =
            force != 0 || source_changed_bytes >= motion_threshold;
    }
    return output_changed;
}

/*
 * Compare a fresh compositor frame with the published buffer.  If content
 * changed, update only stale 256-byte blocks in the inactive buffer.  The
 * caller publishes that buffer after this function returns, so readers never
 * observe the partial copy.
 */
UU_EXPORT int uu_frame_update_2d(
    unsigned char *target,
    size_t target_stride,
    const unsigned char *current,
    size_t current_stride,
    const unsigned char *source,
    size_t source_stride,
    size_t row_bytes,
    size_t rows,
    int force,
    struct uu_frame_update_stats *stats)
{
    size_t row;
    int changed = force != 0;

    if (stats)
        memset(stats, 0, sizeof(*stats));
    if (!target || !current || !source ||
        !uu_valid_plane(target_stride, row_bytes, rows) ||
        !uu_valid_plane(current_stride, row_bytes, rows) ||
        !uu_valid_plane(source_stride, row_bytes, rows))
        return -EINVAL;

    if (!changed) {
        for (row = 0; row < rows; ++row) {
            if (stats)
                stats->compared_bytes += row_bytes;
            if (memcmp(source + row * source_stride,
                       current + row * current_stride, row_bytes) != 0) {
                changed = 1;
                break;
            }
        }
    }
    if (!changed)
        return 0;

    for (row = 0; row < rows; ++row) {
        size_t offset;
        int row_changed = 0;

        for (offset = 0; offset < row_bytes;) {
            size_t block = row_bytes - offset;
            unsigned char *target_block =
                target + row * target_stride + offset;
            const unsigned char *source_block =
                source + row * source_stride + offset;

            if (block > UU_FRAME_BLOCK_BYTES)
                block = UU_FRAME_BLOCK_BYTES;
            if (stats)
                stats->compared_bytes += block;
            if (memcmp(target_block, source_block, block) != 0) {
                memcpy(target_block, source_block, block);
                row_changed = 1;
                if (stats) {
                    stats->copied_bytes += block;
                    ++stats->changed_blocks;
                }
            }
            offset += block;
        }
        if (row_changed && stats)
            ++stats->changed_rows;
    }
    return 1;
}

UU_EXPORT uint32_t uu_frame_helper_self_test(void)
{
    unsigned char target[60];
    unsigned char current[60];
    unsigned char source[60];
    struct uu_frame_update_stats stats;
    size_t index;

    memset(target, 0, sizeof(target));
    memset(current, 0, sizeof(current));
    memset(source, 0, sizeof(source));
    for (index = 0; index < 3u; ++index)
        memset(source + index * 20u, (int)(index + 1u), 16u);
    if (uu_frame_update_2d(target, 20u, current, 20u, source, 20u,
                           16u, 3u, 1, &stats) != 1 ||
        stats.copied_bytes != 48u || stats.changed_rows != 3u)
        return 0u;
    if (memcmp(target, source, sizeof(target)) != 0)
        return 0u;
    memcpy(current, source, sizeof(current));
    if (uu_frame_update_2d(target, 20u, current, 20u, source, 20u,
                           16u, 3u, 0, &stats) != 0 ||
        stats.copied_bytes != 0u)
        return 0u;
    source[21] ^= 0x7fu;
    if (uu_frame_update_2d(target, 20u, current, 20u, source, 20u,
                           16u, 3u, 0, &stats) != 1 ||
        stats.copied_bytes != 16u || stats.changed_rows != 1u ||
        memcmp(target, source, sizeof(target)) != 0)
        return 0u;
    memset(target, 0, sizeof(target));
    if (uu_frame_copy_2d(target, 20u, source, 20u, 16u, 3u) != 0 ||
        memcmp(target, source, sizeof(target)) != 0)
        return 0u;
    {
        const size_t temporal_bytes = 16384u;
        unsigned char *temporal_target =
            (unsigned char *)calloc(temporal_bytes, 1u);
        unsigned char *temporal_current =
            (unsigned char *)calloc(temporal_bytes, 1u);
        unsigned char *temporal_source =
            (unsigned char *)calloc(temporal_bytes, 1u);
        void *filter = uu_frame_temporal_filter_create(temporal_bytes, 1u);
        int result = 0;

        if (!temporal_target || !temporal_current || !temporal_source || !filter)
            result = 1;
        if (!result) {
            memset(temporal_source, 0x11, UU_FRAME_BLOCK_BYTES);
            if (uu_frame_update_temporal_2d(
                    filter, temporal_target, temporal_bytes,
                    temporal_current, temporal_bytes,
                    temporal_source, temporal_bytes,
                    temporal_bytes, 1u, 32u, 0, &stats) != 1 ||
                memcmp(temporal_target, temporal_source, temporal_bytes) != 0 ||
                stats.source_changed_bytes != UU_FRAME_BLOCK_BYTES ||
                stats.temporal_deferred_blocks != 0u ||
                stats.temporal_suppressed_blocks != 0u ||
                stats.temporal_published_blocks != 0u ||
                stats.motion_bypass != 0u)
                result = 1;
            memcpy(temporal_current, temporal_target, temporal_bytes);

            memset(temporal_source, 0x22, UU_FRAME_BLOCK_BYTES);
            if (uu_frame_update_temporal_2d(
                    filter, temporal_target, temporal_bytes,
                    temporal_current, temporal_bytes,
                    temporal_source, temporal_bytes,
                    temporal_bytes, 1u, 32u, 0, &stats) != 1 ||
                memcmp(temporal_target, temporal_source, temporal_bytes) != 0)
                result = 1;
            memcpy(temporal_current, temporal_target, temporal_bytes);

            memset(temporal_source, 0x33, UU_FRAME_BLOCK_BYTES);
            if (uu_frame_update_temporal_2d(
                    filter, temporal_target, temporal_bytes,
                    temporal_current, temporal_bytes,
                    temporal_source, temporal_bytes,
                    temporal_bytes, 1u, 32u, 0, &stats) != 1 ||
                memcmp(temporal_target, temporal_source, temporal_bytes) != 0)
                result = 1;
            memcpy(temporal_current, temporal_target, temporal_bytes);

            memset(temporal_source, 0x11, UU_FRAME_BLOCK_BYTES);
            if (uu_frame_update_temporal_2d(
                    filter, temporal_target, temporal_bytes,
                    temporal_current, temporal_bytes,
                    temporal_source, temporal_bytes,
                    temporal_bytes, 1u, 32u, 0, &stats) != 1 ||
                memcmp(temporal_target, temporal_source, temporal_bytes) != 0)
                result = 1;
            memcpy(temporal_current, temporal_target, temporal_bytes);

            temporal_target[0] ^= 0xffu;
            if (uu_frame_update_temporal_2d(
                    filter, temporal_target, temporal_bytes,
                    temporal_current, temporal_bytes,
                    temporal_source, temporal_bytes,
                    temporal_bytes, 1u, 32u, 0, &stats) != 0 ||
                memcmp(temporal_target, temporal_source, temporal_bytes) != 0)
                result = 1;

            temporal_source[0] ^= 0x01u;
            if (uu_frame_update_temporal_2d(
                    filter, temporal_target, temporal_bytes,
                    temporal_current, temporal_bytes,
                    temporal_source, temporal_bytes,
                    temporal_bytes, 1u, 32u, 0, &stats) != 1 ||
                stats.source_changed_bytes != 1u ||
                memcmp(temporal_target, temporal_source, temporal_bytes) != 0)
                result = 1;
            memcpy(temporal_current, temporal_target, temporal_bytes);

            memset(temporal_source, 0x44, UU_FRAME_BLOCK_BYTES * 2u);
            if (uu_frame_update_temporal_2d(
                    filter, temporal_target, temporal_bytes,
                    temporal_current, temporal_bytes,
                    temporal_source, temporal_bytes,
                    temporal_bytes, 1u, 32u, 0, &stats) != 1 ||
                stats.motion_bypass != 1u ||
                memcmp(temporal_target, temporal_source,
                       UU_FRAME_BLOCK_BYTES * 2u) != 0)
                result = 1;
        }
        uu_frame_temporal_filter_destroy(filter);
        free(temporal_source);
        free(temporal_current);
        free(temporal_target);
        if (result)
            return 0u;
    }
    return 1u;
}
