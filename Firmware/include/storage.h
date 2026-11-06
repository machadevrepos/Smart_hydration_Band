#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

struct nvs_fs;

/*
 * Flash storage that survives power-off: Zephyr settings on NVS, in the
 * settings_storage partition. Small values (intervals, quiet hours, boot
 * count) are named settings under "umbra/"; the data log writes its records
 * straight into the same NVS with its own IDs (see data_log.c).
 */

/*
 * Mount the storage. Call once at start-up, before anything loads a value.
 *
 * Returns 0 on success, negative errno on failure. Without storage the band
 * runs on its defaults and nothing is remembered across power-off.
 */
int storage_init(void);

/*
 * Read the value saved under key ("umbra/<key>") into buf.
 *
 * Returns 0 if a value of exactly len bytes was found, -ENOENT if there is none
 * (or it has a different size), or another negative errno.
 */
int storage_load(const char *key, void *buf, size_t len);

/*
 * Save a value under key ("umbra/<key>"). Writes flash, so it can take tens of
 * milliseconds; call it from the main loop.
 *
 * Returns 0 on success, negative errno on failure.
 */
int storage_save(const char *key, const void *buf, size_t len);

/* The underlying NVS file system, or NULL if storage_init() failed. */
struct nvs_fs *storage_nvs(void);

#endif /* STORAGE_H */
