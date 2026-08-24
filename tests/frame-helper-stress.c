#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

extern void *uu_frame_temporal_filter_create(size_t row_bytes, size_t rows);
extern void uu_frame_temporal_filter_destroy(void *opaque);
extern int uu_frame_update_temporal_2d(
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
    struct uu_frame_update_stats *stats
);
extern int uu_frame_copy_2d(
    unsigned char *target,
    size_t target_stride,
    const unsigned char *source,
    size_t source_stride,
    size_t row_bytes,
    size_t rows
);

struct worker {
    uint64_t seed;
    unsigned long iterations;
    unsigned int identifier;
};

static void fail(const char *message, unsigned long iteration)
{
    fprintf(stderr, "frame helper stress failure at iteration %lu: %s\n",
            iteration, message);
    exit(1);
}

static uint64_t random_next(uint64_t *state)
{
    uint64_t value = *state;

    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    *state = value;
    return value * UINT64_C(2685821657736338717);
}

static void random_fill(unsigned char *buffer, size_t size, uint64_t *state)
{
    size_t index;
    uint64_t value = 0;

    for (index = 0; index < size; ++index) {
        if ((index & 7u) == 0u)
            value = random_next(state);
        buffer[index] = (unsigned char)(value >> ((index & 7u) * 8u));
    }
}

static void copy_rows(
    unsigned char *target,
    size_t target_stride,
    const unsigned char *source,
    size_t source_stride,
    size_t row_bytes,
    size_t rows)
{
    size_t row;

    for (row = 0; row < rows; ++row)
        memcpy(target + row * target_stride,
               source + row * source_stride, row_bytes);
}

static int rows_equal(
    const unsigned char *left,
    size_t left_stride,
    const unsigned char *right,
    size_t right_stride,
    size_t row_bytes,
    size_t rows)
{
    size_t row;

    for (row = 0; row < rows; ++row)
        if (memcmp(left + row * left_stride,
                   right + row * right_stride, row_bytes) != 0)
            return 0;
    return 1;
}

static void expected_stats(
    const unsigned char *target,
    size_t target_stride,
    const unsigned char *current,
    size_t current_stride,
    const unsigned char *source,
    size_t source_stride,
    size_t row_bytes,
    size_t rows,
    uint64_t *source_changed_bytes,
    uint64_t *copied_bytes,
    uint32_t *changed_rows,
    uint32_t *changed_blocks)
{
    size_t row;

    *source_changed_bytes = 0;
    *copied_bytes = 0;
    *changed_rows = 0;
    *changed_blocks = 0;
    for (row = 0; row < rows; ++row) {
        size_t offset;
        int row_changed = 0;

        for (offset = 0; offset < row_bytes; offset += UU_FRAME_BLOCK_BYTES) {
            size_t block = row_bytes - offset;
            size_t index;
            const unsigned char *target_block =
                target + row * target_stride + offset;
            const unsigned char *current_block =
                current + row * current_stride + offset;
            const unsigned char *source_block =
                source + row * source_stride + offset;

            if (block > UU_FRAME_BLOCK_BYTES)
                block = UU_FRAME_BLOCK_BYTES;
            for (index = 0; index < block; ++index)
                if (current_block[index] != source_block[index])
                    ++*source_changed_bytes;
            if (memcmp(target_block, source_block, block) != 0) {
                *copied_bytes += block;
                ++*changed_blocks;
                row_changed = 1;
            }
        }
        if (row_changed)
            ++*changed_rows;
    }
}

static void run_random_case(uint64_t *state, unsigned long iteration)
{
    size_t row_bytes = 1u + (size_t)(random_next(state) % 4096u);
    size_t rows = 1u + (size_t)(random_next(state) % 48u);
    size_t target_stride = row_bytes + (size_t)(random_next(state) % 65u);
    size_t current_stride = row_bytes + (size_t)(random_next(state) % 65u);
    size_t source_stride = row_bytes + (size_t)(random_next(state) % 65u);
    size_t target_size = target_stride * rows;
    size_t current_size = current_stride * rows;
    size_t source_size = source_stride * rows;
    unsigned char *target = malloc(target_size);
    unsigned char *target_before = malloc(target_size);
    unsigned char *copy = malloc(target_size);
    unsigned char *current = malloc(current_size);
    unsigned char *source = malloc(source_size);
    struct uu_frame_update_stats stats;
    uint64_t source_changed_bytes;
    uint64_t copied_bytes;
    uint32_t changed_rows;
    uint32_t changed_blocks;
    uint32_t denominator = 1u + (uint32_t)(random_next(state) % 128u);
    int force = (random_next(state) % 31u) == 0u;
    int expected_changed;
    int result;
    void *filter;
    size_t row;

    if (!target || !target_before || !copy || !current || !source)
        fail("allocation failed", iteration);
    random_fill(target, target_size, state);
    random_fill(current, current_size, state);
    random_fill(source, source_size, state);
    copy_rows(source, source_stride, current, current_stride, row_bytes, rows);

    switch (random_next(state) % 4u) {
    case 0:
        break;
    case 1: {
        size_t changes = 1u + (size_t)(random_next(state) % 31u);
        size_t change;

        for (change = 0; change < changes; ++change) {
            row = (size_t)(random_next(state) % rows);
            source[row * source_stride + random_next(state) % row_bytes] ^=
                (unsigned char)(1u + random_next(state) % 255u);
        }
        break;
    }
    default:
        for (row = 0; row < rows; ++row)
            random_fill(source + row * source_stride, row_bytes, state);
        break;
    }

    memcpy(target_before, target, target_size);
    expected_stats(
        target, target_stride, current, current_stride,
        source, source_stride, row_bytes, rows,
        &source_changed_bytes, &copied_bytes, &changed_rows, &changed_blocks
    );
    expected_changed = force || source_changed_bytes != 0u;
    filter = uu_frame_temporal_filter_create(row_bytes, rows);
    if (!filter)
        fail("filter allocation failed", iteration);
    result = uu_frame_update_temporal_2d(
        filter,
        target, target_stride,
        current, current_stride,
        source, source_stride,
        row_bytes, rows, denominator, force, &stats
    );
    if (result != expected_changed)
        fail("wrong change result", iteration);
    if (!rows_equal(target, target_stride, source, source_stride, row_bytes, rows))
        fail("published rows are not the latest source", iteration);
    for (row = 0; row < rows; ++row)
        if (target_stride > row_bytes &&
            memcmp(target + row * target_stride + row_bytes,
                   target_before + row * target_stride + row_bytes,
                   target_stride - row_bytes) != 0)
            fail("target row padding was modified", iteration);
    if (stats.compared_bytes != row_bytes * rows ||
        stats.source_changed_bytes != source_changed_bytes ||
        stats.copied_bytes != copied_bytes ||
        stats.changed_rows != changed_rows ||
        stats.changed_blocks != changed_blocks)
        fail("statistics mismatch", iteration);
    if (stats.temporal_deferred_blocks ||
        stats.temporal_suppressed_blocks ||
        stats.temporal_published_blocks)
        fail("legacy temporal counters unexpectedly changed", iteration);
    if (stats.motion_bypass !=
        (uint32_t)(force || source_changed_bytes >=
            (row_bytes * rows + denominator - 1u) / denominator))
        fail("motion threshold mismatch", iteration);

    memcpy(copy, target_before, target_size);
    if (uu_frame_copy_2d(
            copy, target_stride, source, source_stride,
            row_bytes, rows) != 0 ||
        !rows_equal(copy, target_stride, source, source_stride, row_bytes, rows))
        fail("2D copy mismatch", iteration);
    for (row = 0; row < rows; ++row)
        if (target_stride > row_bytes &&
            memcmp(copy + row * target_stride + row_bytes,
                   target_before + row * target_stride + row_bytes,
                   target_stride - row_bytes) != 0)
            fail("2D copy modified row padding", iteration);

    uu_frame_temporal_filter_destroy(filter);
    free(source);
    free(current);
    free(copy);
    free(target_before);
    free(target);
}

static void *run_worker(void *opaque)
{
    struct worker *worker = opaque;
    unsigned long iteration;

    for (iteration = 0; iteration < worker->iterations; ++iteration)
        run_random_case(
            &worker->seed,
            (unsigned long)worker->identifier * worker->iterations + iteration
        );
    return NULL;
}

static void run_full_frame_case(
    size_t width,
    size_t height,
    unsigned long frames,
    uint64_t *state)
{
    size_t row_bytes = width * 4u;
    size_t stride = row_bytes + 64u;
    size_t size = stride * height;
    unsigned char *target = calloc(size, 1u);
    unsigned char *current = calloc(size, 1u);
    unsigned char *source = calloc(size, 1u);
    struct uu_frame_update_stats stats;
    void *filter = uu_frame_temporal_filter_create(row_bytes, height);
    unsigned long frame;

    if (!target || !current || !source || !filter)
        fail("full-frame allocation failed", 0);
    for (frame = 0; frame < frames; ++frame) {
        size_t row = (size_t)(random_next(state) % height);
        size_t offset = (size_t)(random_next(state) % row_bytes);
        int result;

        source[row * stride + offset] ^=
            (unsigned char)(1u + random_next(state) % 255u);
        if ((frame % 29u) == 0u) {
            row = (size_t)(random_next(state) % height);
            random_fill(source + row * stride, row_bytes, state);
        }
        result = uu_frame_update_temporal_2d(
            filter,
            target, stride,
            current, stride,
            source, stride,
            row_bytes, height, 32u, frame == 0u, &stats
        );
        if (result != 1 ||
            !rows_equal(target, stride, source, stride, row_bytes, height))
            fail("full-frame latest-wins mismatch", frame);
        copy_rows(current, stride, target, stride, row_bytes, height);
    }
    uu_frame_temporal_filter_destroy(filter);
    free(source);
    free(current);
    free(target);
}

static void run_invalid_cases(void)
{
    unsigned char byte = 0;
    struct uu_frame_update_stats stats;
    void *filter = uu_frame_temporal_filter_create(16u, 2u);

    if (!filter || uu_frame_temporal_filter_create(0u, 1u) ||
        uu_frame_temporal_filter_create(1u, 0u) ||
        uu_frame_temporal_filter_create(SIZE_MAX, 2u))
        fail("invalid filter dimensions accepted", 0);
    if (uu_frame_copy_2d(NULL, 1u, &byte, 1u, 1u, 1u) != -EINVAL ||
        uu_frame_copy_2d(&byte, 0u, &byte, 1u, 1u, 1u) != -EINVAL)
        fail("invalid copy accepted", 0);
    if (uu_frame_update_temporal_2d(
            filter, &byte, 1u, &byte, 1u, &byte, 1u,
            1u, 1u, 32u, 0, &stats) != -EINVAL ||
        uu_frame_update_temporal_2d(
            filter, &byte, 16u, &byte, 16u, &byte, 16u,
            16u, 2u, 0u, 0, &stats) != -EINVAL)
        fail("invalid update accepted", 0);
    uu_frame_temporal_filter_destroy(filter);
    uu_frame_temporal_filter_destroy(NULL);
}

static unsigned long parse_count(const char *value, const char *name)
{
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);

    if (!value[0] || !end || *end || !parsed) {
        fprintf(stderr, "invalid %s: %s\n", name, value);
        exit(2);
    }
    return parsed;
}

int main(int argc, char **argv)
{
    unsigned long iterations = argc > 1 ? parse_count(argv[1], "iterations") : 2000u;
    unsigned long full_frames = argc > 2 ? parse_count(argv[2], "full frames") : 4u;
    unsigned long thread_count = argc > 3 ? parse_count(argv[3], "threads") : 4u;
    pthread_t *threads;
    struct worker *workers;
    uint64_t full_frame_state = UINT64_C(0x915f77dca21b39e3);
    unsigned long index;

    if (argc > 4 || thread_count > 64u) {
        fprintf(stderr, "usage: %s [ITERATIONS [FULL_FRAMES [THREADS]]]\n", argv[0]);
        return 2;
    }
    run_invalid_cases();
    threads = calloc(thread_count, sizeof(*threads));
    workers = calloc(thread_count, sizeof(*workers));
    if (!threads || !workers)
        fail("thread allocation failed", 0);
    for (index = 0; index < thread_count; ++index) {
        workers[index].seed =
            UINT64_C(0x6a09e667f3bcc909) ^ (UINT64_C(0x9e3779b97f4a7c15) * (index + 1u));
        workers[index].iterations = iterations / thread_count +
            (index < iterations % thread_count);
        workers[index].identifier = (unsigned int)index;
        if (pthread_create(&threads[index], NULL, run_worker, &workers[index]))
            fail("pthread_create failed", index);
    }
    for (index = 0; index < thread_count; ++index)
        if (pthread_join(threads[index], NULL))
            fail("pthread_join failed", index);
    run_full_frame_case(2560u, 1440u, full_frames, &full_frame_state);
    run_full_frame_case(1440u, 2560u, full_frames, &full_frame_state);
    free(workers);
    free(threads);
    printf(
        "PASS frame helper sanitized stress iterations=%lu full_frames=%lu threads=%lu\n",
        iterations, full_frames * 2u, thread_count
    );
    return 0;
}
