#pragma once
#include <cstdlib>
struct TestMutex { bool locked = false; };
using SemaphoreHandle_t = TestMutex *;
inline bool fail_mutex_allocation = false;
inline bool force_mutex_timeout = false;
inline int live_mutexes = 0;
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    if (fail_mutex_allocation) return nullptr;
    ++live_mutexes;
    return new TestMutex{};
}
inline int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned) {
    if (!mutex || mutex->locked || force_mutex_timeout) return pdFALSE;
    mutex->locked = true;
    return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t mutex) {
    if (!mutex || !mutex->locked) std::abort();
    mutex->locked = false;
    return pdTRUE;
}
inline void vSemaphoreDelete(SemaphoreHandle_t mutex) {
    if (mutex->locked) std::abort();
    --live_mutexes;
    delete mutex;
}
