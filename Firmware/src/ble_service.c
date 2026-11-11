#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/hrs.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include <bluetooth/services/nus.h>

#include "band_mode.h"
#include "ble_service.h"
#include "haptic.h"
#include "data_log.h"
#include "spo2.h"
#include "wall_clock.h"

LOG_MODULE_REGISTER(ble_service, LOG_LEVEL_INF);

#define BLE_THREAD_STACK_SIZE 2048
#define BLE_THREAD_PRIORITY   7

/*
 * If advertising cannot restart (after a disconnect, or at start-up), try again
 * this often; otherwise the band could not be reached until power-cycled.
 */
#define ADV_RETRY_MS 5000

/* Haptic commands closer together than this are ignored. */
#define HAPTIC_COMMAND_GAP_MS 2000

/* Umbra service: 1ab10000-fde9-42a3-ab5b-82416f9bca6e, characteristics count up. */
#define UMBRA_UUID_VAL(n) BT_UUID_128_ENCODE(0x1ab10000 + (n), 0xfde9, 0x42a3, 0xab5b, 0x82416f9bca6e)

#define UMBRA_MODE_DAILY  0
#define UMBRA_MODE_NIGHT  1
#define UMBRA_HAPTIC_DOUBLE_TAP 1

static struct bt_uuid_128 umbra_service_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(0));
static struct bt_uuid_128 umbra_steps_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(1));
static struct bt_uuid_128 umbra_mode_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(2));
static struct bt_uuid_128 umbra_haptic_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(3));
static struct bt_uuid_128 umbra_intervals_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(4));

static struct bt_uuid_128 umbra_time_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(5));
static struct bt_uuid_128 umbra_quiet_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(6));
static struct bt_uuid_128 umbra_log_control_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(7));
static struct bt_uuid_128 umbra_log_data_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(8));
static struct bt_uuid_128 umbra_spo2_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(9));

/*
 * SpO2 value: status uint8 (enum spo2_status), last valid SpO2 % uint8 (0 =
 * none), its quality uint8, reserved uint8, UTC seconds of that reading uint32
 * (0 if the time was not known), latest attempt's R x 1000 uint16. Write 1 to
 * measure now.
 */
#define SPO2_VALUE_LEN   10
#define SPO2_MEASURE_NOW 1

#define LOG_CONTROL_LEN    12
#define LOG_FORMAT_VERSION DATA_LOG_FORMAT_VERSION
/* With a 247-byte MTU, 12 records (240 bytes) go in one notification. */
#define LOG_RECORDS_PER_NOTIFY_MAX 12
/*
 * During a log transfer ask for a 15-30 ms connection interval, then the usual
 * 200-400 ms (prj.conf). The phone may refuse either; the transfer still works.
 */
#define LOG_FAST_PARAM BT_LE_CONN_PARAM(12, 24, 0, 400)
#define LOG_SLOW_PARAM                                                                             \
	BT_LE_CONN_PARAM(CONFIG_BT_PERIPHERAL_PREF_MIN_INT, CONFIG_BT_PERIPHERAL_PREF_MAX_INT,     \
			 CONFIG_BT_PERIPHERAL_PREF_LATENCY, CONFIG_BT_PERIPHERAL_PREF_TIMEOUT)

/* Intervals value: Daily then Night minutes, each uint16 little endian. */
#define INTERVALS_LEN 4
/* Time value: UTC seconds uint32 LE, then local offset in minutes int16 LE. */
#define TIME_LEN 6
/* Quiet hours value: start then end, local minutes after midnight, uint16 LE. */
#define QUIET_LEN 4

/*
 * The Nordic UART JSON line is for the current app, which reads nothing else.
 * Remove it, with CONFIG_BT_NUS, once the app uses the services above.
 */
/*
 * Longest:
 * {"session_active":false,"heart_rate":200,"steps":4294967295,"battery":100,"spo2":100}\n
 */
#define JSON_LINE_MAX 100
#define NUS_COMMAND_BUFFER_LEN 64
#define NUS_COMMAND_TRIGGER_HAPTIC "trigger_haptic"

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, UMBRA_UUID_VAL(0)),
};

/* Guards current_conn and telemetry, which several threads touch. */
static K_MUTEX_DEFINE(state_lock);
static struct bt_conn *current_conn;
static struct ble_telemetry telemetry;
static bool started;

/* Wakes the BLE thread; these say why. */
static K_SEM_DEFINE(ble_wake, 0, 1);
/* A log transfer asked for by the app, and where it starts. */
static atomic_t log_request_pending;
static atomic_t log_request_from;
static atomic_t restart_advertising;
static atomic_t telemetry_pending;

/* Only the Bluetooth receive thread touches these. */
static char nus_command[NUS_COMMAND_BUFFER_LEN + 1];
static size_t nus_command_len;

/*
 * Both command paths (GATT write and NUS text) run on the Bluetooth receive
 * context, so this needs no lock. With no pairing anyone nearby can send them;
 * the gap stops a stuck or hostile app from buzzing the band nonstop.
 */
static void fire_haptic_command(void)
{
	static int64_t last_fired_ms;
	static bool fired_any;
	int64_t now = k_uptime_get();

	if (fired_any && now - last_fired_ms < HAPTIC_COMMAND_GAP_MS) {
		printk("  BLE command: double tap ignored, last one under %d s ago\n",
		       HAPTIC_COMMAND_GAP_MS / 1000);
		return;
	}

	fired_any = true;
	last_fired_ms = now;
	printk("  BLE command: double tap\n");
	haptic_double_tap();
}

static ssize_t read_steps(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	uint8_t value[sizeof(uint32_t)];

	k_mutex_lock(&state_lock, K_FOREVER);
	sys_put_le32(telemetry.steps, value);
	k_mutex_unlock(&state_lock);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

static ssize_t read_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	uint8_t value;

	k_mutex_lock(&state_lock, K_FOREVER);
	value = telemetry.night_mode ? UMBRA_MODE_NIGHT : UMBRA_MODE_DAILY;
	k_mutex_unlock(&state_lock);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

/* The main loop applies it within ~40 ms and notifies the new mode within 1 s. */
static ssize_t write_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	uint8_t value = *(const uint8_t *)buf;

	if (value != UMBRA_MODE_DAILY && value != UMBRA_MODE_NIGHT) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	printk("  BLE command: %s Mode\n", value == UMBRA_MODE_NIGHT ? "Night" : "Daily");
	band_mode_request(value == UMBRA_MODE_NIGHT ? BAND_MODE_NIGHT : BAND_MODE_DAILY);

	return len;
}

static ssize_t read_intervals(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset)
{
	struct band_intervals iv;
	uint8_t value[INTERVALS_LEN];

	band_mode_get_intervals(&iv);
	sys_put_le16(iv.daily_min, &value[0]);
	sys_put_le16(iv.night_min, &value[2]);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

static ssize_t write_intervals(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != INTERVALS_LEN) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	struct band_intervals iv = {
		.daily_min = sys_get_le16(&((const uint8_t *)buf)[0]),
		.night_min = sys_get_le16(&((const uint8_t *)buf)[2]),
	};

	if (band_mode_request_intervals(&iv) < 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	printk("  BLE command: intervals Daily %u min, Night %u min (0 = default)\n",
	       iv.daily_min, iv.night_min);
	return len;
}

/* Reads 0 UTC seconds while the time is not set. */
static ssize_t read_time(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	uint32_t utc_s = 0;
	uint8_t value[TIME_LEN];

	(void)wall_clock_now(&utc_s);
	sys_put_le32(utc_s, &value[0]);
	sys_put_le16((uint16_t)wall_clock_tz_offset(), &value[4]);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

static ssize_t write_time(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != TIME_LEN) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	uint32_t utc_s = sys_get_le32(&((const uint8_t *)buf)[0]);
	int16_t tz = (int16_t)sys_get_le16(&((const uint8_t *)buf)[4]);

	if (wall_clock_set(utc_s, tz) < 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	uint16_t minute = 0;

	(void)wall_clock_local_minute(&minute);
	printk("  BLE command: time set, local %02u:%02u (UTC offset %d min)\n", minute / 60,
	       minute % 60, tz);
	return len;
}

static ssize_t read_quiet(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	struct band_quiet_hours q;
	uint8_t value[QUIET_LEN];

	band_mode_get_quiet_hours(&q);
	sys_put_le16(q.start_min, &value[0]);
	sys_put_le16(q.end_min, &value[2]);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

static ssize_t write_quiet(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			   uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != QUIET_LEN) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	struct band_quiet_hours q = {
		.start_min = sys_get_le16(&((const uint8_t *)buf)[0]),
		.end_min = sys_get_le16(&((const uint8_t *)buf)[2]),
	};

	if (band_mode_request_quiet_hours(&q) < 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	return len;
}

static ssize_t write_haptic(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			    uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (*(const uint8_t *)buf != UMBRA_HAPTIC_DOUBLE_TAP) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	fire_haptic_command();

	return len;
}

/*
 * Log control read: oldest seq uint32, next seq uint32 (records are oldest up
 * to next - 1), capacity uint16, record length uint8, format version uint8.
 */
static ssize_t read_log_control(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset)
{
	uint32_t first;
	uint32_t after_last;
	uint8_t value[LOG_CONTROL_LEN];

	data_log_range(&first, &after_last);
	sys_put_le32(first, &value[0]);
	sys_put_le32(after_last, &value[4]);
	sys_put_le16(DATA_LOG_CAPACITY, &value[8]);
	value[10] = DATA_LOG_RECORD_LEN;
	value[11] = LOG_FORMAT_VERSION;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

/* Log control write: uint32 seq to send from (0 = oldest). The BLE thread sends. */
static ssize_t write_log_control(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != sizeof(uint32_t)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	atomic_set(&log_request_from, (atomic_val_t)sys_get_le32(buf));
	atomic_set(&log_request_pending, 1);
	k_sem_give(&ble_wake);

	return len;
}

/* The SpO2 value, SPO2_VALUE_LEN bytes; see its definition above. */
static void encode_spo2(uint8_t *value)
{
	struct spo2_result res;
	uint32_t boot_utc;
	uint32_t utc_s = 0;

	spo2_get_result(&res);
	if (res.uptime_ms != 0 && wall_clock_boot_utc(&boot_utc)) {
		utc_s = boot_utc + (uint32_t)(res.uptime_ms / 1000);
	}

	value[0] = (uint8_t)res.status;
	value[1] = res.percent;
	value[2] = res.quality;
	value[3] = 0;
	sys_put_le32(utc_s, &value[4]);
	sys_put_le16(res.ratio_milli, &value[8]);
}

static ssize_t read_spo2(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	uint8_t value[SPO2_VALUE_LEN];

	encode_spo2(value);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

/* Write 1 to measure now; the result is notified when it is done (30-55 s). */
static ssize_t write_spo2(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (*(const uint8_t *)buf != SPO2_MEASURE_NOW) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	printk("  BLE command: measure SpO2\n");
	spo2_request();
	return len;
}

BT_GATT_SERVICE_DEFINE(umbra_svc,
	BT_GATT_PRIMARY_SERVICE(&umbra_service_uuid),
	BT_GATT_CHARACTERISTIC(&umbra_steps_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_steps, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&umbra_mode_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_mode, write_mode, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&umbra_haptic_uuid.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, write_haptic, NULL),
	BT_GATT_CHARACTERISTIC(&umbra_intervals_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_intervals,
			       write_intervals, NULL),
	BT_GATT_CHARACTERISTIC(&umbra_time_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_time, write_time, NULL),
	BT_GATT_CHARACTERISTIC(&umbra_quiet_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_quiet, write_quiet,
			       NULL),
	BT_GATT_CHARACTERISTIC(&umbra_log_control_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_log_control,
			       write_log_control, NULL),
	BT_GATT_CHARACTERISTIC(&umbra_log_data_uuid.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&umbra_spo2_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_spo2, write_spo2, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* Positions of the value attributes in umbra_svc, for notifying. */
#define UMBRA_ATTR_STEPS    2
#define UMBRA_ATTR_MODE     5
#define UMBRA_ATTR_LOG_DATA 18
#define UMBRA_ATTR_SPO2     21

/*
 * Send the log from seq from (0 = oldest) as notifications on Log data, as many
 * whole records per notification as the MTU allows, then a 4-byte end marker:
 * the seq after the last record sent. Asks for a fast connection interval
 * meanwhile, and the slow one back after. Stops if the app disconnects,
 * unsubscribes or asks for a new transfer.
 */
static void send_log(uint32_t from)
{
	const struct bt_gatt_attr *attr = &umbra_svc.attrs[UMBRA_ATTR_LOG_DATA];
	uint8_t chunk[LOG_RECORDS_PER_NOTIFY_MAX * DATA_LOG_RECORD_LEN];
	struct bt_conn *conn = NULL;
	uint32_t first;
	uint32_t after_last;
	uint32_t sent = 0;
	int err = 0;

	k_mutex_lock(&state_lock, K_FOREVER);
	if (current_conn != NULL) {
		conn = bt_conn_ref(current_conn);
	}
	k_mutex_unlock(&state_lock);

	if (conn == NULL) {
		return;
	}

	data_log_range(&first, &after_last);

	/*
	 * A number past the end cannot have come from this log: the app last
	 * synced another band, or this one's flash was erased. Send the whole log
	 * rather than nothing until the count catches up; the end marker then gives
	 * the app the right number. Equal to the end is the normal "nothing new".
	 */
	if (from > after_last) {
		printk("  BLE log: asked from %u, past the end (%u): sending all\n", from,
		       after_last);
		from = 0;
	}

	uint32_t seq = MAX(from, first);
	size_t per_notify = (bt_gatt_get_mtu(conn) - 3) / DATA_LOG_RECORD_LEN;

	per_notify = CLAMP(per_notify, 1, LOG_RECORDS_PER_NOTIFY_MAX);

	printk("  BLE log: sending %u records from %u\n", after_last > seq ? after_last - seq : 0,
	       seq);
	(void)bt_conn_le_param_update(conn, LOG_FAST_PARAM);

	while (seq < after_last && err == 0 && atomic_get(&log_request_pending) == 0) {
		size_t n = 0;

		while (n < per_notify && seq < after_last) {
			struct data_log_record rec;

			if (data_log_read(seq, &rec) == 0) {
				data_log_encode(&rec, &chunk[n * DATA_LOG_RECORD_LEN]);
				n++;
			}
			seq++;
		}

		if (n > 0) {
			err = bt_gatt_notify(conn, attr, chunk, n * DATA_LOG_RECORD_LEN);
			sent += n;
		}
	}

	if (err == 0 && atomic_get(&log_request_pending) == 0) {
		uint8_t end[sizeof(uint32_t)];

		sys_put_le32(seq, end);
		err = bt_gatt_notify(conn, attr, end, sizeof(end));
	}

	(void)bt_conn_le_param_update(conn, LOG_SLOW_PARAM);
	bt_conn_unref(conn);

	printk("  BLE log: %u records sent%s\n", sent, err ? ", stopped (not subscribed or link lost)"
							   : "");
}

/* Set while advertising should run but failed to start; the BLE thread retries. */
static bool advertising_failed;

/* Only the BLE thread calls this. Logs once per failure run. */
static int start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err == -EALREADY) {
		err = 0;
	}

	if (err) {
		if (!advertising_failed) {
			LOG_ERR("Advertising failed (%d), retrying every %d s", err,
				ADV_RETRY_MS / 1000);
		}
		advertising_failed = true;
		return err;
	}

	advertising_failed = false;
	printk("  BLE advertising as %s\n", CONFIG_BT_DEVICE_NAME);
	return 0;
}

static bool is_connected(void)
{
	bool connected;

	k_mutex_lock(&state_lock, K_FOREVER);
	connected = current_conn != NULL;
	k_mutex_unlock(&state_lock);

	return connected;
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (0x%02x)", err);
		return;
	}

	k_mutex_lock(&state_lock, K_FOREVER);
	current_conn = bt_conn_ref(conn);
	k_mutex_unlock(&state_lock);

	nus_command_len = 0;
	printk("  BLE connected\n");
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	if (current_conn != NULL) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
	k_mutex_unlock(&state_lock);

	printk("  BLE disconnected (0x%02x)\n", reason);
}

/* Advertising can restart only once the old connection object is free. */
static void on_recycled(void)
{
	atomic_set(&restart_advertising, 1);
	k_sem_give(&ble_wake);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.recycled = on_recycled,
};

/* Text is buffered so a command split across writes is still found. */
static void on_nus_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
	ARG_UNUSED(conn);

	size_t copy = MIN(len, NUS_COMMAND_BUFFER_LEN - nus_command_len);

	memcpy(&nus_command[nus_command_len], data, copy);
	nus_command_len += copy;
	nus_command[nus_command_len] = '\0';

	if (strstr(nus_command, NUS_COMMAND_TRIGGER_HAPTIC) != NULL) {
		fire_haptic_command();
		nus_command_len = 0;
	} else if (memchr(data, '\n', len) != NULL || nus_command_len == NUS_COMMAND_BUFFER_LEN) {
		nus_command_len = 0;
	}
}

static struct bt_nus_cb nus_callbacks = {
	.received = on_nus_received,
};

static int format_json_line(const struct ble_telemetry *t, char *line, size_t size)
{
	/*
	 * heart_rate is always sent, 0 meaning no reading (off the wrist, settling,
	 * sensor fault). Leaving it out made the app keep showing the last value.
	 */
	int len = snprintk(line, size, "{\"session_active\":%s,\"heart_rate\":%d",
			   t->night_mode ? "true" : "false", MAX(t->heart_rate, 0));

	if (t->steps_valid) {
		len += snprintk(&line[len], size - len, ",\"steps\":%u", t->steps);
	}
	if (t->battery_valid) {
		len += snprintk(&line[len], size - len, ",\"battery\":%u", t->battery_percent);
	}
	/* Like heart_rate, always sent: 0 means no recent reading. */
	len += snprintk(&line[len], size - len, ",\"spo2\":%u}\n", t->spo2);

	return len;
}

/* Sends nothing unless the app has subscribed; the first send then fails. */
static void send_json_line(const struct ble_telemetry *t)
{
	char line[JSON_LINE_MAX];
	struct bt_conn *conn = NULL;

	k_mutex_lock(&state_lock, K_FOREVER);
	if (current_conn != NULL) {
		conn = bt_conn_ref(current_conn);
	}
	k_mutex_unlock(&state_lock);

	if (conn == NULL) {
		return;
	}

	int len = format_json_line(t, line, sizeof(line));
	uint32_t chunk_max = bt_nus_get_mtu(conn);

	for (int offset = 0; offset < len; offset += chunk_max) {
		uint16_t chunk = MIN((uint32_t)(len - offset), chunk_max);

		if (bt_nus_send(conn, (const uint8_t *)&line[offset], chunk) != 0) {
			break;
		}
	}

	bt_conn_unref(conn);
}

/*
 * Heart rate goes out with each reading and the rest only on change. A newly
 * subscribed app should read steps, mode and battery once to get the current
 * values. A notification reaches only a subscribed app, and with none it does
 * nothing, so failures here are not errors.
 */
static void send_updates(const struct ble_telemetry *t)
{
	static bool sent_any;
	static struct ble_telemetry sent;

	if (t->heart_rate > 0) {
		(void)bt_hrs_notify(t->heart_rate);
	}

	if (t->steps_valid && (!sent_any || t->steps != sent.steps)) {
		uint8_t value[sizeof(uint32_t)];

		sys_put_le32(t->steps, value);
		(void)bt_gatt_notify(NULL, &umbra_svc.attrs[UMBRA_ATTR_STEPS], value,
				     sizeof(value));
	}

	if (!sent_any || t->night_mode != sent.night_mode) {
		uint8_t value = t->night_mode ? UMBRA_MODE_NIGHT : UMBRA_MODE_DAILY;

		(void)bt_gatt_notify(NULL, &umbra_svc.attrs[UMBRA_ATTR_MODE], &value,
				     sizeof(value));
	}

	if (t->battery_valid && (!sent_any || t->battery_percent != sent.battery_percent)) {
		(void)bt_bas_set_battery_level(t->battery_percent);
	}

	/* SpO2 changes rarely: notify when its status or reading changes. */
	{
		static uint8_t sent_spo2[SPO2_VALUE_LEN];
		uint8_t value[SPO2_VALUE_LEN];

		encode_spo2(value);
		if (!sent_any || memcmp(value, sent_spo2, sizeof(value)) != 0) {
			(void)bt_gatt_notify(NULL, &umbra_svc.attrs[UMBRA_ATTR_SPO2], value,
					     sizeof(value));
			memcpy(sent_spo2, value, sizeof(value));
		}
	}

	sent = *t;
	sent_any = true;

	send_json_line(t);
}

static void ble_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int64_t adv_attempt_ms = 0;

	for (;;) {
		/* Wake on its own to retry while advertising is down. */
		(void)k_sem_take(&ble_wake, advertising_failed ? K_MSEC(ADV_RETRY_MS) : K_FOREVER);

		bool restart = atomic_set(&restart_advertising, 0) != 0;

		if (advertising_failed && is_connected()) {
			/* Only one connection: advertising resumes after it ends. */
			advertising_failed = false;
		} else if (restart ||
			   (advertising_failed && k_uptime_get() - adv_attempt_ms >= ADV_RETRY_MS)) {
			adv_attempt_ms = k_uptime_get();
			(void)start_advertising();
		}
		if (atomic_set(&log_request_pending, 0) != 0) {
			send_log((uint32_t)atomic_get(&log_request_from));
		}
		if (atomic_set(&telemetry_pending, 0) != 0) {
			struct ble_telemetry snapshot;

			k_mutex_lock(&state_lock, K_FOREVER);
			snapshot = telemetry;
			k_mutex_unlock(&state_lock);

			send_updates(&snapshot);
		}
	}
}

K_THREAD_DEFINE(ble_thread_id, BLE_THREAD_STACK_SIZE, ble_thread, NULL, NULL, NULL,
		BLE_THREAD_PRIORITY, 0, 0);

/* The UMBRA_ATTR_* positions are counted by hand; check them against the table. */
static int check_attr_positions(void)
{
	if (bt_uuid_cmp(umbra_svc.attrs[UMBRA_ATTR_STEPS].uuid, &umbra_steps_uuid.uuid) != 0 ||
	    bt_uuid_cmp(umbra_svc.attrs[UMBRA_ATTR_MODE].uuid, &umbra_mode_uuid.uuid) != 0 ||
	    bt_uuid_cmp(umbra_svc.attrs[UMBRA_ATTR_LOG_DATA].uuid, &umbra_log_data_uuid.uuid) != 0 ||
	    bt_uuid_cmp(umbra_svc.attrs[UMBRA_ATTR_SPO2].uuid, &umbra_spo2_uuid.uuid) != 0) {
		LOG_ERR("Umbra service attribute positions are wrong; fix UMBRA_ATTR_*");
		return -EFAULT;
	}
	return 0;
}

int ble_service_init(void)
{
	int err = check_attr_positions();

	if (err) {
		return err;
	}

	err = bt_enable(NULL);

	if (err) {
		return err;
	}

	err = bt_nus_init(&nus_callbacks);
	if (err) {
		return err;
	}

	started = true;

	/* The BLE thread starts advertising, and keeps retrying if it fails. */
	atomic_set(&restart_advertising, 1);
	k_sem_give(&ble_wake);

	return 0;
}

void ble_service_publish(const struct ble_telemetry *new_telemetry)
{
	if (!started) {
		return;
	}

	k_mutex_lock(&state_lock, K_FOREVER);
	telemetry = *new_telemetry;
	k_mutex_unlock(&state_lock);

	atomic_set(&telemetry_pending, 1);
	k_sem_give(&ble_wake);
}
