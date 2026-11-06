#ifndef DATA_LOG_H
#define DATA_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/*
 * Steps and heart rate kept in flash so nothing is lost while no app is
 * connected. One record per period (5 min in Daily Mode, 1 min in Night Mode,
 * and a short one when the mode changes), in a ring of DATA_LOG_CAPACITY
 * records: the oldest is overwritten when it is full. Records are numbered by
 * a sequence that only grows, so the app asks for everything after the last
 * one it has (see ble_service.c and the README).
 */

#define DATA_LOG_CAPACITY    1024
#define DATA_LOG_DAILY_S     300
#define DATA_LOG_NIGHT_S     60

/* Bytes per record on the wire and in flash, all little endian:
 *  0  uint32 seq         record number, from 1, never reused
 *  4  uint32 time        UTC seconds at the start of the period if FLAG_UTC,
 *                        else seconds since that boot started
 *  8  uint16 steps       steps counted in the period (if FLAG_STEPS)
 * 10  uint16 boot        boot number the record was made in
 * 12  uint8  hr_avg      mean heart rate in bpm over the readings, 0 = none
 * 13  uint8  hr_max      highest heart rate in bpm, 0 = none
 * 14  uint8  seconds/10  length of the period in tens of seconds
 * 15  uint8  flags       DATA_LOG_FLAG_*
 */
#define DATA_LOG_RECORD_LEN 16

#define DATA_LOG_FLAG_UTC   BIT(0) /* time is UTC; otherwise seconds since boot */
#define DATA_LOG_FLAG_NIGHT BIT(1) /* the period was in Night Mode */
#define DATA_LOG_FLAG_STEPS BIT(2) /* steps is valid (the step counter worked) */

struct data_log_record {
	uint32_t seq;
	uint32_t time;
	uint16_t steps;
	uint16_t boot;
	uint8_t hr_avg;
	uint8_t hr_max;
	uint8_t tens_of_seconds;
	uint8_t flags;
};

/*
 * Count this boot and find the records already in flash. Call after
 * storage_init(). Takes a few tens of milliseconds.
 *
 * Returns 0 on success, negative errno if there is no storage (then nothing is
 * logged and the log reads as empty).
 */
int data_log_init(void);

/*
 * Feed one second of data from the main loop. heart_rate 0 means no reading.
 * Closes the period, and queues its record for writing, when it is full or the
 * mode changed.
 */
void data_log_add_second(int heart_rate, bool steps_valid, uint32_t steps, bool night);

/* Write the period in progress now and wait for it. Before powering off. */
void data_log_flush(void);

/*
 * The records in flash: sequence numbers oldest up to, not including, next.
 * Equal means empty. Safe from any thread.
 */
void data_log_range(uint32_t *oldest, uint32_t *next);

/*
 * Read record seq. A record made earlier in this boot, before the app set the
 * time, is returned with UTC time once the time is known. Safe from any thread.
 *
 * Returns 0, -ENOENT if it is not in flash (overwritten or never written), or
 * another negative errno.
 */
int data_log_read(uint32_t seq, struct data_log_record *rec);

/* Encode a record into DATA_LOG_RECORD_LEN bytes, the layout above. */
void data_log_encode(const struct data_log_record *rec, uint8_t *out);

/* This boot's number, as stored in records. */
uint16_t data_log_boot(void);

#endif /* DATA_LOG_H */
