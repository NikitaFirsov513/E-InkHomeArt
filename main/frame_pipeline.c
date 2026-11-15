#include "frame_pipeline.h"

int frame_pipeline_begin_render(frame_pipeline_t *pipeline)
{
    if (!pipeline) return -1;
    /* Never let a second render begin before the first is published. */
    for (unsigned i = 0; i < FRAME_PIPELINE_SLOTS; ++i)
        if (pipeline->state[i] == FRAME_RENDERING) return -1;
    for (unsigned i = 0; i < FRAME_PIPELINE_SLOTS; ++i)
        if (pipeline->state[i] == FRAME_FREE) {
            pipeline->state[i] = FRAME_RENDERING;
            return (int)i;
        }
    /* The consumer is busy: replace its waiting frame, never its active one. */
    for (unsigned i = 0; i < FRAME_PIPELINE_SLOTS; ++i)
        if (pipeline->state[i] == FRAME_READY) {
            pipeline->state[i] = FRAME_RENDERING;
            return (int)i;
        }
    return -1;
}

bool frame_pipeline_publish(frame_pipeline_t *pipeline, int slot)
{
    if (!pipeline || slot < 0 || slot >= FRAME_PIPELINE_SLOTS ||
        pipeline->state[slot] != FRAME_RENDERING) return false;
    for (unsigned i = 0; i < FRAME_PIPELINE_SLOTS; ++i)
        if (pipeline->state[i] == FRAME_READY) pipeline->state[i] = FRAME_FREE;
    pipeline->state[slot] = FRAME_READY;
    return true;
}

int frame_pipeline_take_latest(frame_pipeline_t *pipeline)
{
    if (!pipeline) return -1;
    for (unsigned i = 0; i < FRAME_PIPELINE_SLOTS; ++i)
        if (pipeline->state[i] == FRAME_READY) {
            for (unsigned j = 0; j < FRAME_PIPELINE_SLOTS; ++j)
                if (pipeline->state[j] == FRAME_DISPLAY) pipeline->state[j] = FRAME_FREE;
            pipeline->state[i] = FRAME_DISPLAY;
            return (int)i;
        }
    return -1;
}
