#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TRUE 1
#define FALSE 0
#define BOOL int
#define NV_ENC_SUCCESS 0
#define UU_NVENC_ENCODER_MAGIC 0x55554e56454e4331ULL

typedef int NVENCSTATUS;

struct uu_nvenc_resource
{
    int identifier;
    struct uu_nvenc_resource *next;
};

struct uu_nvenc_encoder
{
    uint64_t magic;
    void *native_encoder;
    pthread_mutex_t mutex;
    BOOL destroying;
    struct uu_nvenc_resource *resources;
    struct uu_nvenc_encoder *next;
};

static pthread_mutex_t uu_bridge_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct uu_nvenc_encoder *uu_encoders;
static int freed_resources;

static void fail(const char *message);
static void uu_free_resource(struct uu_nvenc_resource *resource);

#include "../third_party/nvenc/uu-remote-nvenc-lifecycle.h"

static void uu_free_resource(struct uu_nvenc_resource *resource)
{
    if (!resource)
        fail("attempted to free a null resource");
    ++freed_resources;
}

static pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_condition = PTHREAD_COND_INITIALIZER;
static BOOL active_entered;
static BOOL release_active;
static BOOL destroy_started;
static BOOL destroy_acquired;
static BOOL contender_finished;
static BOOL contender_bridged;
static struct uu_nvenc_encoder *contender_encoder;
static void *const test_handle = (void *)(uintptr_t)0x1234u;

static void fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void wait_flag(BOOL *flag, const char *message)
{
    struct timespec deadline;

    if (clock_gettime(CLOCK_REALTIME, &deadline))
        fail("clock_gettime failed");
    deadline.tv_sec += 2;
    pthread_mutex_lock(&test_mutex);
    while (!*flag)
        if (pthread_cond_timedwait(&test_condition, &test_mutex, &deadline))
        {
            pthread_mutex_unlock(&test_mutex);
            fail(message);
        }
    pthread_mutex_unlock(&test_mutex);
}

static void *active_worker(void *unused)
{
    struct uu_nvenc_encoder *encoder;
    BOOL bridged;

    (void)unused;
    encoder = uu_lock_encoder(test_handle, &bridged);
    if (!encoder || !bridged)
        fail("active worker did not enter bridged encoder");
    pthread_mutex_lock(&test_mutex);
    active_entered = TRUE;
    pthread_cond_broadcast(&test_condition);
    while (!release_active)
        pthread_cond_wait(&test_condition, &test_mutex);
    pthread_mutex_unlock(&test_mutex);
    uu_unlock_encoder(encoder);
    return NULL;
}

static void *destroy_worker(void *unused)
{
    struct uu_nvenc_encoder *encoder;
    BOOL bridged;

    (void)unused;
    pthread_mutex_lock(&test_mutex);
    destroy_started = TRUE;
    pthread_cond_broadcast(&test_condition);
    pthread_mutex_unlock(&test_mutex);
    encoder = uu_begin_encoder_destroy(test_handle, &bridged);
    if (!encoder || !bridged)
        fail("destroy worker did not acquire bridged encoder");
    pthread_mutex_lock(&test_mutex);
    destroy_acquired = TRUE;
    pthread_cond_broadcast(&test_condition);
    pthread_mutex_unlock(&test_mutex);
    uu_retire_destroyed_encoder(encoder);
    uu_unlock_encoder(encoder);
    return NULL;
}

static void *contender_worker(void *unused)
{
    (void)unused;
    contender_encoder = uu_lock_encoder(test_handle, &contender_bridged);
    pthread_mutex_lock(&test_mutex);
    contender_finished = TRUE;
    pthread_cond_broadcast(&test_condition);
    pthread_mutex_unlock(&test_mutex);
    return NULL;
}

int main(void)
{
    struct uu_nvenc_encoder encoder = {0};
    struct uu_nvenc_encoder replacement = {0};
    struct uu_nvenc_resource first_resource = {1, NULL};
    struct uu_nvenc_resource second_resource = {2, NULL};
    struct uu_nvenc_encoder *destroying_encoder;
    struct uu_nvenc_encoder churn[UU_NVENC_RETIRED_ENCODER_LIMIT + 5u] = {{0}};
    unsigned int index;
    BOOL bridged;
    pthread_t active;
    pthread_t destroy;
    pthread_t contender;

    encoder.magic = UU_NVENC_ENCODER_MAGIC;
    encoder.native_encoder = test_handle;
    if (pthread_mutex_init(&encoder.mutex, NULL))
        fail("encoder mutex init failed");
    uu_encoders = &encoder;

    if (pthread_create(&active, NULL, active_worker, NULL))
        fail("active worker creation failed");
    wait_flag(&active_entered, "active worker timed out");
    if (pthread_create(&destroy, NULL, destroy_worker, NULL))
        fail("destroy worker creation failed");
    wait_flag(&destroy_started, "destroy worker did not start");

    for (;;)
    {
        BOOL tombstone;

        pthread_mutex_lock(&uu_bridge_mutex);
        tombstone = encoder.destroying;
        pthread_mutex_unlock(&uu_bridge_mutex);
        if (tombstone)
            break;
    }
    if (pthread_create(&contender, NULL, contender_worker, NULL))
        fail("contender worker creation failed");
    wait_flag(&contender_finished,
              "contender blocked behind an encoder being destroyed");
    if (contender_encoder || !contender_bridged)
        fail("destroying encoder was allowed to fall through as native");

    pthread_mutex_lock(&test_mutex);
    release_active = TRUE;
    pthread_cond_broadcast(&test_condition);
    pthread_mutex_unlock(&test_mutex);
    wait_flag(&destroy_acquired, "destroy did not wait for active worker");

    pthread_join(contender, NULL);
    pthread_join(active, NULL);
    pthread_join(destroy, NULL);
    if (uu_encoders)
        fail("destroyed encoder remained registered");
    if (!uu_find_retired_encoder(test_handle) ||
        uu_retired_encoder_count != 1u)
        fail("destroyed encoder did not remain as a tombstone");
    contender_encoder = uu_lock_encoder(test_handle, &contender_bridged);
    if (contender_encoder || !contender_bridged)
        fail("late call fell through after encoder destruction completed");
    pthread_mutex_destroy(&encoder.mutex);

    replacement.magic = UU_NVENC_ENCODER_MAGIC;
    replacement.native_encoder = test_handle;
    if (pthread_mutex_init(&replacement.mutex, NULL))
        fail("replacement encoder mutex init failed");
    uu_publish_encoder(&replacement);
    if (uu_encoders != &replacement ||
        uu_find_retired_encoder(test_handle) ||
        uu_retired_encoder_count != 0u)
        fail("reused native handle did not replace its tombstone atomically");

    first_resource.next = &second_resource;
    replacement.resources = &first_resource;
    if (uu_complete_resource_unregister(
            &replacement, &first_resource, 20) != 20 ||
        replacement.resources != &first_resource || freed_resources != 0)
        fail("failed unregister released or detached its resource");
    if (uu_complete_resource_unregister(
            &replacement, &first_resource, NV_ENC_SUCCESS) != NV_ENC_SUCCESS)
        fail("successful unregister changed the native status");
    if (replacement.resources != &second_resource || freed_resources != 1)
        fail("successfully unregistered resource was not released exactly once");
    if (second_resource.identifier != 2)
        fail("releasing one resource damaged the remaining resource");

    destroying_encoder = uu_begin_encoder_destroy(test_handle, &bridged);
    if (destroying_encoder != &replacement || !bridged ||
        !replacement.destroying)
        fail("destroy retry test did not publish the destroying state");
    uu_cancel_encoder_destroy(destroying_encoder);
    if (replacement.destroying)
        fail("failed native destroy left the encoder permanently destroying");
    destroying_encoder = uu_lock_encoder(test_handle, &bridged);
    if (destroying_encoder != &replacement || !bridged)
        fail("encoder was not reusable after native destroy failure");
    uu_unlock_encoder(destroying_encoder);

    destroying_encoder = uu_begin_encoder_destroy(test_handle, &bridged);
    if (destroying_encoder != &replacement || !bridged)
        fail("replacement encoder could not be retired for churn test");
    uu_retire_destroyed_encoder(destroying_encoder);
    uu_unlock_encoder(destroying_encoder);
    pthread_mutex_destroy(&replacement.mutex);

    for (index = 0; index < UU_NVENC_RETIRED_ENCODER_LIMIT + 5u; ++index)
    {
        void *handle = (void *)(uintptr_t)(0x2000u + index);

        churn[index].magic = UU_NVENC_ENCODER_MAGIC;
        churn[index].native_encoder = handle;
        if (pthread_mutex_init(&churn[index].mutex, NULL))
            fail("churn encoder mutex init failed");
        uu_publish_encoder(&churn[index]);
        destroying_encoder = uu_begin_encoder_destroy(handle, &bridged);
        if (destroying_encoder != &churn[index] || !bridged)
            fail("churn encoder could not enter destruction");
        uu_retire_destroyed_encoder(destroying_encoder);
        uu_unlock_encoder(destroying_encoder);
        pthread_mutex_destroy(&churn[index].mutex);
    }
    if (uu_retired_encoder_count != UU_NVENC_RETIRED_ENCODER_LIMIT)
        fail("retired encoder handle table exceeded its fixed bound");
    if (uu_find_retired_encoder((void *)(uintptr_t)0x2000u))
        fail("oldest retired encoder handle was not evicted");
    if (!uu_find_retired_encoder(
            (void *)(uintptr_t)(
                0x2000u + UU_NVENC_RETIRED_ENCODER_LIMIT + 4u
            )
        ))
        fail("newest retired encoder handle was not retained");
    puts("PASS NVENC encoder lifecycle concurrency");
    return 0;
}
