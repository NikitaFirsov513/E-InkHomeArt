#pragma once

#include <stdbool.h>

#define FRAME_PIPELINE_SLOTS 2
typedef enum { FRAME_FREE, FRAME_RENDERING, FRAME_READY, FRAME_DISPLAY } frame_slot_state_t;
typedef struct { frame_slot_state_t state[FRAME_PIPELINE_SLOTS]; } frame_pipeline_t;

/* One renderer and one display consumer. Call these short ownership operations
 * under the same lock. Render/transfer pixels outside the lock. Zero-init is
 * empty. A displayed buffer stays immutable until take_latest replaces it. */
int frame_pipeline_begin_render(frame_pipeline_t *pipeline);
bool frame_pipeline_publish(frame_pipeline_t *pipeline, int slot);
int frame_pipeline_take_latest(frame_pipeline_t *pipeline);
