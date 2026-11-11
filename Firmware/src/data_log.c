#include <errno.h>

#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "data_log.h"
#include "storage.h"
#include "wall_clock.h"

LOG_MODULE_REGISTER(data_log, LOG_LEVEL_INF);

/*
 * Records live in the settings NVS under their own IDs, one per ring slot:
 * LOG_ID_BASE + seq % DATA_LOG_CAPACITY. Settings uses IDs from 0x8000 up, so
 * these cannot collide. Rewriting a slot leaves the old entry as garbage that
 * NVS reclaims, which spreads the wear over the whole partition.
 */
#define LOG_ID_BASE 0x0100
BUILD_ASSERT(LOG_ID_BASE + DATA_LOG_CAPACITY <= 0x4000, "Log IDs must stay below settings IDs");

#define STORAGE_KEY_BOOT "boot"

/*
 * Writes go through a queue to their own low-priority thread: an NVS write
 * that has to erase a sector can take a few hundred milliseconds, longer than
 * the main loop may stop draining the sensor FIFO.
 */
#define WRITER_STACK_SIZE 1024
#define WRITER_PRIORITY   10
#define WRITE_QUEUE_LEN   8

K_MSGQ_DEFINE(write_queue, sizeof(struct data_log_record), WRITE_QUEUE_LEN, 4);
static K_SEM_DEFINE(flushed, 0, 1);

static struct nvs_fs *fs;
static uint16_t boot;

/* Guards oldest and next, which the BLE thread reads. */
static K_MUTEX_DEFINE(range_lock);
static uint32_t oldest = 1;
static uint32_t next = 1;
static bool write_failed;

/* The period being collected, main loop only. */
static struct {
	bool open;
	bool night;
	int64_t start_uptime_s;
	uint32_t seconds;
	uint32_t hr_sum;
	uint32_t hr_count;
	uint8_t hr_max;
	bool steps_valid;
	uint32_t steps_start;
	uint32_t steps_last;
	uint8_t spo2;
	uint8_t spo2_quality;
} period;

void data_log_encode(const struct data_log_record *rec, uint8_t *out)
{
	sys_put_le32(rec->seq, &out[0]);
	sys_put_le32(rec->time, &out[4]);
	sys_put_le16(rec->steps, &out[8]);
	sys_put_le16(rec->boot, &out[10]);
	out[12] = rec->hr_avg;
	out[13] = rec->hr_max;
	out[14] = rec->tens_of_seconds;
	out[15] = rec->flags;
	out[16] = rec->spo2;
	out[17] = rec->spo2_quality;
	sys_put_le16(0, &out[18]);
}

static void decode(const uint8_t *in, struct data_log_record *rec)
{
	rec->seq = sys_get_le32(&in[0]);
	rec->time = sys_get_le32(&in[4]);
	rec->steps = sys_get_le16(&in[8]);
	rec->boot = sys_get_le16(&in[10]);
	rec->hr_avg = in[12];
	rec->hr_max = in[13];
	rec->tens_of_seconds = in[14];
	rec->flags = in[15];
	rec->spo2 = in[16];
	rec->spo2_quality = in[17];
}

static uint16_t slot_id(uint32_t seq)
{
	return (uint16_t)(LOG_ID_BASE + seq % DATA_LOG_CAPACITY);
}

static void count_boot(void)
{
	uint32_t count = 0;

	(void)storage_load(STORAGE_KEY_BOOT, &count, sizeof(count));
	count++;
	if (storage_save(STORAGE_KEY_BOOT, &count, sizeof(count)) < 0) {
		LOG_WRN("Boot count not saved");
	}
	boot = (uint16_t)count;
}

/* Find the newest and oldest records still in flash. */
static void scan(void)
{
	uint32_t newest = 0;
	uint32_t first = 0;

	storage_lock();
	for (uint32_t slot = 0; slot < DATA_LOG_CAPACITY; slot++) {
		uint8_t raw[DATA_LOG_RECORD_LEN];
		struct data_log_record rec;

		if (nvs_read(fs, (uint16_t)(LOG_ID_BASE + slot), raw, sizeof(raw)) !=
		    sizeof(raw)) {
			continue;
		}
		decode(raw, &rec);
		if (rec.seq == 0 || rec.seq % DATA_LOG_CAPACITY != slot) {
			continue;
		}
		newest = MAX(newest, rec.seq);
		first = first == 0 ? rec.seq : MIN(first, rec.seq);
	}
	storage_unlock();

	k_mutex_lock(&range_lock, K_FOREVER);
	next = newest + 1;
	oldest = first != 0 ? MAX(first, next > DATA_LOG_CAPACITY ? next - DATA_LOG_CAPACITY : 1)
			    : next;
	k_mutex_unlock(&range_lock);
}

int data_log_init(void)
{
	fs = storage_nvs();
	if (fs == NULL) {
		return -ENODEV;
	}

	count_boot();
	scan();

	uint32_t first;
	uint32_t last;

	data_log_range(&first, &last);
	LOG_INF("Data log: boot %u, records %u-%u in flash", boot, first, last - 1);
	return 0;
}

uint16_t data_log_boot(void)
{
	return boot;
}

void data_log_range(uint32_t *first, uint32_t *after_last)
{
	k_mutex_lock(&range_lock, K_FOREVER);
	*first = oldest;
	*after_last = next;
	k_mutex_unlock(&range_lock);
}

/* Writer thread: give the record its number and store it. */
static void store(struct data_log_record *rec)
{
	uint8_t raw[DATA_LOG_RECORD_LEN];

	k_mutex_lock(&range_lock, K_FOREVER);
	rec->seq = next;
	k_mutex_unlock(&range_lock);

	data_log_encode(rec, raw);

	storage_lock();
	ssize_t ret = nvs_write(fs, slot_id(rec->seq), raw, sizeof(raw));
	storage_unlock();

	if (ret < 0) {
		if (!write_failed) {
			LOG_ERR("Data log write failed (%d)", (int)ret);
		}
		write_failed = true;
		return;
	}
	write_failed = false;

	k_mutex_lock(&range_lock, K_FOREVER);
	next = rec->seq + 1;
	if (next - oldest > DATA_LOG_CAPACITY) {
		oldest = next - DATA_LOG_CAPACITY;
	}
	k_mutex_unlock(&range_lock);
}

/* A record with seq 0 is a flush marker: everything before it is stored. */
static void writer_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		struct data_log_record rec;

		(void)k_msgq_get(&write_queue, &rec, K_FOREVER);
		if (rec.time == 0 && rec.tens_of_seconds == 0) {
			k_sem_give(&flushed);
			continue;
		}
		store(&rec);
	}
}

K_THREAD_DEFINE(data_log_writer, WRITER_STACK_SIZE, writer_thread, NULL, NULL, NULL,
		WRITER_PRIORITY, 0, 0);

static void open_period(bool night, bool steps_valid, uint32_t steps)
{
	period.open = true;
	period.night = night;
	period.start_uptime_s = k_uptime_get() / 1000;
	period.seconds = 0;
	period.hr_sum = 0;
	period.hr_count = 0;
	period.hr_max = 0;
	/* spo2 is left alone: a reading noted between periods belongs to this one. */
	period.steps_valid = steps_valid;
	period.steps_start = steps;
	period.steps_last = steps;
}

static void close_period(void)
{
	struct data_log_record rec = {
		.boot = boot,
		.hr_max = period.hr_max,
		.spo2 = period.spo2,
		.spo2_quality = period.spo2_quality,
		.tens_of_seconds = (uint8_t)MIN((period.seconds + 5) / 10, UINT8_MAX),
		.flags = period.night ? DATA_LOG_FLAG_NIGHT : 0,
	};
	uint32_t boot_utc;

	period.open = false;
	period.spo2 = 0;
	period.spo2_quality = 0;
	if (fs == NULL || period.seconds == 0) {
		return;
	}

	if (period.hr_count > 0) {
		rec.hr_avg = (uint8_t)((period.hr_sum + period.hr_count / 2) / period.hr_count);
	}

	/* The step counter only counts up; anything else means it restarted. */
	if (period.steps_valid && period.steps_last >= period.steps_start) {
		rec.steps = (uint16_t)MIN(period.steps_last - period.steps_start, UINT16_MAX);
		rec.flags |= DATA_LOG_FLAG_STEPS;
	}

	if (wall_clock_boot_utc(&boot_utc)) {
		rec.time = boot_utc + (uint32_t)period.start_uptime_s;
		rec.flags |= DATA_LOG_FLAG_UTC;
	} else {
		rec.time = (uint32_t)period.start_uptime_s;
	}

	/* A record of time 0 and length 0 would read as a flush marker. */
	rec.tens_of_seconds = MAX(rec.tens_of_seconds, 1);

	if (k_msgq_put(&write_queue, &rec, K_NO_WAIT) < 0) {
		LOG_WRN("Data log queue full, record dropped");
	}
}

void data_log_note_spo2(uint8_t percent, uint8_t quality)
{
	period.spo2 = percent;
	period.spo2_quality = quality;
}

void data_log_add_second(int heart_rate, bool steps_valid, uint32_t steps, bool night)
{
	if (fs == NULL) {
		return;
	}

	if (period.open && night != period.night) {
		close_period();
	}
	if (!period.open) {
		open_period(night, steps_valid, steps);
	}

	period.seconds++;

	if (heart_rate > 0) {
		uint8_t bpm = (uint8_t)MIN(heart_rate, UINT8_MAX);

		period.hr_sum += bpm;
		period.hr_count++;
		period.hr_max = MAX(period.hr_max, bpm);
	}

	if (steps_valid) {
		if (!period.steps_valid) {
			/* Counter came back: count from here. */
			period.steps_valid = true;
			period.steps_start = steps;
		}
		period.steps_last = steps;
	}

	if (period.seconds >= (night ? DATA_LOG_NIGHT_S : DATA_LOG_DAILY_S)) {
		close_period();
	}
}

void data_log_flush(void)
{
	struct data_log_record marker = {0};

	if (fs == NULL) {
		return;
	}
	if (period.open) {
		close_period();
	}

	k_sem_reset(&flushed);
	if (k_msgq_put(&write_queue, &marker, K_MSEC(500)) == 0) {
		(void)k_sem_take(&flushed, K_SECONDS(3));
	}
}

int data_log_read(uint32_t seq, struct data_log_record *rec)
{
	uint8_t raw[DATA_LOG_RECORD_LEN];
	uint32_t first;
	uint32_t after_last;
	uint32_t boot_utc;

	if (fs == NULL) {
		return -ENODEV;
	}

	data_log_range(&first, &after_last);
	if (seq < first || seq >= after_last) {
		return -ENOENT;
	}

	storage_lock();
	ssize_t ret = nvs_read(fs, slot_id(seq), raw, sizeof(raw));
	storage_unlock();

	if (ret != sizeof(raw)) {
		return ret < 0 ? (int)ret : -ENOENT;
	}

	decode(raw, rec);
	if (rec->seq != seq) {
		return -ENOENT;
	}

	/* Made this boot before the time was set: it can be placed now. */
	if (!(rec->flags & DATA_LOG_FLAG_UTC) && rec->boot == boot &&
	    wall_clock_boot_utc(&boot_utc)) {
		rec->time += boot_utc;
		rec->flags |= DATA_LOG_FLAG_UTC;
	}

	return 0;
}
