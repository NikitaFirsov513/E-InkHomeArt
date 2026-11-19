#include "art_store_esp.h"
#include "esp_partition.h"

static bool read_bytes(void *p, uint32_t offset, void *bytes, size_t size)
{
    return esp_partition_read(p, offset, bytes, size) == ESP_OK;
}
static bool write_bytes(void *p, uint32_t offset, const void *bytes, size_t size)
{
    return esp_partition_write(p, offset, bytes, size) == ESP_OK;
}
static bool erase_bytes(void *p, uint32_t offset, size_t size)
{
    return esp_partition_erase_range(p, offset, size) == ESP_OK;
}

esp_err_t art_store_open_partition(art_store_t *store, const char *label, art_store_result_t *result)
{
    if (store)
        store->ready = false;
    if (!store || !label || !result)
        return ESP_ERR_INVALID_ARG;
    *result = ART_STORE_ARGUMENT;
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ART_STORE_PARTITION_SUBTYPE, label);
    if (!p)
        return ESP_ERR_NOT_FOUND;
    if (p->size < ART_STORE_MIN_BYTES)
        return ESP_ERR_INVALID_SIZE;
    const art_store_io_t io = {.context = (void *)p,
                               .size = p->size,
                               .read = read_bytes,
                               .write = write_bytes,
                               .erase = erase_bytes};
    *result = art_store_open(store, &io);
    return *result == ART_STORE_OK || *result == ART_STORE_EMPTY ? ESP_OK : ESP_FAIL;
}
