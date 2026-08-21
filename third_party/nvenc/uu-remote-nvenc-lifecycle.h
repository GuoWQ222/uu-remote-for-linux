/* Internal encoder-lifecycle locking shared by the bridge and its unit test. */

#define UU_NVENC_RETIRED_ENCODER_LIMIT 64u

static void *uu_retired_encoder_handles[UU_NVENC_RETIRED_ENCODER_LIMIT];
static unsigned int uu_retired_encoder_count;

static struct uu_nvenc_encoder *uu_find_encoder(void *native_encoder)
{
    struct uu_nvenc_encoder *encoder;

    for (encoder = uu_encoders; encoder; encoder = encoder->next)
        if (encoder->magic == UU_NVENC_ENCODER_MAGIC &&
            encoder->native_encoder == native_encoder)
            return encoder;
    return NULL;
}

static BOOL uu_find_retired_encoder(void *native_encoder)
{
    unsigned int index;

    for (index = 0; index < uu_retired_encoder_count; ++index)
        if (uu_retired_encoder_handles[index] == native_encoder)
            return TRUE;
    return FALSE;
}

static void uu_forget_retired_encoder_locked(void *native_encoder)
{
    unsigned int index;

    for (index = 0; index < uu_retired_encoder_count; ++index)
        if (uu_retired_encoder_handles[index] == native_encoder)
        {
            for (; index + 1u < uu_retired_encoder_count; ++index)
                uu_retired_encoder_handles[index] =
                    uu_retired_encoder_handles[index + 1u];
            --uu_retired_encoder_count;
            return;
        }
}

static struct uu_nvenc_encoder *uu_lock_encoder(
    void *encoder_handle, BOOL *bridged)
{
    struct uu_nvenc_encoder *encoder;

    *bridged = FALSE;
    pthread_mutex_lock(&uu_bridge_mutex);
    encoder = uu_find_encoder(encoder_handle);
    if (encoder)
    {
        *bridged = TRUE;
        if (encoder->destroying)
            encoder = NULL;
        else
            pthread_mutex_lock(&encoder->mutex);
    }
    else if (uu_find_retired_encoder(encoder_handle))
        *bridged = TRUE;
    pthread_mutex_unlock(&uu_bridge_mutex);
    return encoder;
}

static void uu_unlock_encoder(struct uu_nvenc_encoder *encoder)
{
    pthread_mutex_unlock(&encoder->mutex);
}

static struct uu_nvenc_encoder *uu_begin_encoder_destroy(
    void *encoder_handle, BOOL *bridged)
{
    struct uu_nvenc_encoder *encoder;

    *bridged = FALSE;
    pthread_mutex_lock(&uu_bridge_mutex);
    encoder = uu_find_encoder(encoder_handle);
    if (encoder)
    {
        *bridged = TRUE;
        if (encoder->destroying)
            encoder = NULL;
        else
        {
            /* Publish the tombstone before waiting for active users. */
            encoder->destroying = TRUE;
        }
    }
    else if (uu_find_retired_encoder(encoder_handle))
        *bridged = TRUE;
    pthread_mutex_unlock(&uu_bridge_mutex);
    if (encoder)
        pthread_mutex_lock(&encoder->mutex);
    return encoder;
}

static void uu_cancel_encoder_destroy(struct uu_nvenc_encoder *encoder)
{
    pthread_mutex_lock(&uu_bridge_mutex);
    encoder->destroying = FALSE;
    pthread_mutex_unlock(&uu_bridge_mutex);
    pthread_mutex_unlock(&encoder->mutex);
}

static void uu_retire_destroyed_encoder(struct uu_nvenc_encoder *encoder)
{
    struct uu_nvenc_encoder **cursor;
    unsigned int index;

    pthread_mutex_lock(&uu_bridge_mutex);
    for (cursor = &uu_encoders; *cursor; cursor = &(*cursor)->next)
        if (*cursor == encoder)
        {
            *cursor = encoder->next;
            break;
        }
    uu_forget_retired_encoder_locked(encoder->native_encoder);
    if (uu_retired_encoder_count < UU_NVENC_RETIRED_ENCODER_LIMIT)
        ++uu_retired_encoder_count;
    for (index = uu_retired_encoder_count - 1u; index > 0; --index)
        uu_retired_encoder_handles[index] =
            uu_retired_encoder_handles[index - 1u];
    uu_retired_encoder_handles[0] = encoder->native_encoder;
    pthread_mutex_unlock(&uu_bridge_mutex);
}

static void uu_publish_encoder(struct uu_nvenc_encoder *encoder)
{
    pthread_mutex_lock(&uu_bridge_mutex);
    uu_forget_retired_encoder_locked(encoder->native_encoder);
    encoder->next = uu_encoders;
    uu_encoders = encoder;
    pthread_mutex_unlock(&uu_bridge_mutex);
}

static NVENCSTATUS uu_complete_resource_unregister(
    struct uu_nvenc_encoder *encoder,
    struct uu_nvenc_resource *resource,
    NVENCSTATUS status)
{
    struct uu_nvenc_resource **cursor;

    if (status != NV_ENC_SUCCESS)
        return status;
    for (cursor = &encoder->resources; *cursor; cursor = &(*cursor)->next)
        if (*cursor == resource)
        {
            *cursor = resource->next;
            uu_free_resource(resource);
            return status;
        }
    return status;
}
