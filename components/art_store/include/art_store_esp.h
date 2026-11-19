#pragma once
#include "art_store.h"
#include "esp_err.h"
#define ART_STORE_PARTITION_SUBTYPE 0x40

/* Binds only to a dedicated raw data partition, subtype 0x40, named label.
 * Refuses unrelated NVS/filesystem partitions. Returns ESP_ERR_NOT_FOUND
 * for a missing partition or ART_STORE_EMPTY (not an error) for blank storage.
 * Does not erase flash, format a filesystem, or change the partition table. */
esp_err_t art_store_open_partition(art_store_t *store, const char *label, art_store_result_t *result);
