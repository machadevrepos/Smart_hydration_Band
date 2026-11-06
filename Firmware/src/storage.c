#include <errno.h>
#include <zephyr/sys/printk.h>

#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "storage.h"

LOG_MODULE_REGISTER(storage, LOG_LEVEL_INF);

#define KEY_PREFIX  "umbra/"
#define KEY_NAME_MAX 32

static struct nvs_fs *nvs;
static bool ready;

int storage_init(void)
{
	void *backend = NULL;
	int ret = settings_subsys_init();

	if (ret < 0) {
		LOG_ERR("Settings storage unavailable (%d)", ret);
		return ret;
	}

	ret = settings_storage_get(&backend);
	if (ret < 0 || backend == NULL) {
		LOG_ERR("Settings NVS unavailable (%d)", ret);
		return ret < 0 ? ret : -ENODEV;
	}

	nvs = backend;
	ready = true;
	return 0;
}

struct load_request {
	void *buf;
	size_t len;
	int result;
};

static int load_one(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
		    void *param)
{
	struct load_request *req = param;
	const char *next;

	/* Only the exact key, not keys below it. */
	if (settings_name_next(key, &next) != 0) {
		return 0;
	}

	if (len != req->len) {
		req->result = -ENOENT;
		return 0;
	}

	ssize_t got = read_cb(cb_arg, req->buf, req->len);

	req->result = got == (ssize_t)req->len ? 0 : -EIO;
	return 0;
}

int storage_load(const char *key, void *buf, size_t len)
{
	char name[KEY_NAME_MAX];
	struct load_request req = {.buf = buf, .len = len, .result = -ENOENT};
	int ret;

	if (!ready) {
		return -ENODEV;
	}

	snprintk(name, sizeof(name), KEY_PREFIX "%s", key);
	ret = settings_load_subtree_direct(name, load_one, &req);
	if (ret < 0) {
		return ret;
	}

	return req.result;
}

int storage_save(const char *key, const void *buf, size_t len)
{
	char name[KEY_NAME_MAX];

	if (!ready) {
		return -ENODEV;
	}

	snprintk(name, sizeof(name), KEY_PREFIX "%s", key);
	return settings_save_one(name, buf, len);
}

struct nvs_fs *storage_nvs(void)
{
	return ready ? nvs : NULL;
}
