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

#include "ble_service.h"
#include "haptic.h"

LOG_MODULE_REGISTER(ble_service, LOG_LEVEL_INF);

#define BLE_THREAD_STACK_SIZE 1536
#define BLE_THREAD_PRIORITY   7

/*
 * If advertising cannot restart (after a disconnect, or at start-up), try again
 * this often; otherwise the band could not be reached until power-cycled.
 */
#define ADV_RETRY_MS 5000

/* Umbra service: 1ab10000-fde9-42a3-ab5b-82416f9bca6e, characteristics count up. */
#define UMBRA_UUID_VAL(n) BT_UUID_128_ENCODE(0x1ab10000 + (n), 0xfde9, 0x42a3, 0xab5b, 0x82416f9bca6e)

#define UMBRA_MODE_DAILY  0
#define UMBRA_MODE_NIGHT  1
#define UMBRA_HAPTIC_DOUBLE_TAP 1

static struct bt_uuid_128 umbra_service_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(0));
static struct bt_uuid_128 umbra_steps_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(1));
static struct bt_uuid_128 umbra_mode_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(2));
static struct bt_uuid_128 umbra_haptic_uuid = BT_UUID_INIT_128(UMBRA_UUID_VAL(3));

/*
 * The Nordic UART JSON line is for the current app, which reads nothing else.
 * Remove it, with CONFIG_BT_NUS, once the app uses the services above.
 */
/* Longest: {"session_active":false,"heart_rate":200,"steps":4294967295,"battery":100}\n */
#define JSON_LINE_MAX 88
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
static atomic_t restart_advertising;
static atomic_t telemetry_pending;

/* Only the Bluetooth receive thread touches these. */
static char nus_command[NUS_COMMAND_BUFFER_LEN + 1];
static size_t nus_command_len;

static void fire_haptic_command(void)
{
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

BT_GATT_SERVICE_DEFINE(umbra_svc,
	BT_GATT_PRIMARY_SERVICE(&umbra_service_uuid),
	BT_GATT_CHARACTERISTIC(&umbra_steps_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_steps, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&umbra_mode_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_mode, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&umbra_haptic_uuid.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, write_haptic, NULL),
);

/* Positions of the value attributes in umbra_svc, for notifying. */
#define UMBRA_ATTR_STEPS 2
#define UMBRA_ATTR_MODE  5

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
	len += snprintk(&line[len], size - len, "}\n");

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

int ble_service_init(void)
{
	int err = bt_enable(NULL);

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
