/*
 * Copyright (c) 2023, Emna Rekik
 * Copyright (c) 2024, Nordic Semiconductor
 * Copyright (c) 2026, t0.technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <inttypes.h>

#include <zephyr/data/json.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/dns_sd.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/mdns_responder.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>

#include "psu.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(psu_control, LOG_LEVEL_DBG);

struct psu_command {
	bool output_state;
};

static const struct json_obj_descr psu_command_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct psu_command, output_state, JSON_TOK_TRUE),
};

static const struct device *leds_dev = DEVICE_DT_GET_ANY(gpio_leds);

static uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};

static uint8_t main_js_gz[] = {
#include "main.js.gz.inc"
};

static struct http_resource_detail_static index_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = index_html_gz,
	.static_data_len = sizeof(index_html_gz),
};

static struct http_resource_detail_static main_js_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/javascript",
		},
	.static_data = main_js_gz,
	.static_data_len = sizeof(main_js_gz),
};

static int uptime_handler(struct http_client_ctx *client, enum http_data_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx, void *user_data)
{
	int ret;
	static uint8_t uptime_buf[64];

	/* A payload is not expected with the GET request. Ignore any data and wait until
	 * final callback before sending response
	 */
	if (status == HTTP_SERVER_DATA_FINAL) {
		int64_t uptime_ms = k_uptime_get();
		int64_t total_seconds = uptime_ms / 1000;
		int hours = total_seconds / 3600;
		int minutes = (total_seconds % 3600) / 60;
		int seconds = total_seconds % 60;

		ret = snprintf(uptime_buf, sizeof(uptime_buf), "%d:%02d:%02d",
			       hours, minutes, seconds);
		if (ret < 0)
			return ret;

		response_ctx->body = uptime_buf;
		response_ctx->body_len = ret;
		response_ctx->final_chunk = true;
	}

	return 0;
}

static struct http_resource_detail_dynamic uptime_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = uptime_handler,
	.user_data = NULL,
};

static void parse_psu_post(uint8_t *buf, size_t len)
{
	int ret;
	struct psu_command cmd;
	const int expected_return_code = BIT_MASK(ARRAY_SIZE(psu_command_descr));

	ret = json_obj_parse(buf, len, psu_command_descr, ARRAY_SIZE(psu_command_descr), &cmd);
	if (ret != expected_return_code) {
		LOG_WRN("Failed to fully parse JSON payload, ret=%d", ret);
		return;
	}

	LOG_INF("POST request setting PSU output to %d", cmd.output_state);

	/* Control PSU output */
	ret = psu_set_output(cmd.output_state);
	if (ret < 0)
		LOG_ERR("Failed to set PSU output: %d", ret);

	/* Also control LED if available */
	if (leds_dev != NULL) {
		if (cmd.output_state)
			led_on(leds_dev, 0);
		else
			led_off(leds_dev, 0);
	}
}

static int psu_control_handler(struct http_client_ctx *client, enum http_data_status status,
		       const struct http_request_ctx *request_ctx,
		       struct http_response_ctx *response_ctx, void *user_data)
{
	static uint8_t post_payload_buf[32];
	static size_t cursor;

	if (status == HTTP_SERVER_DATA_ABORTED) {
		cursor = 0;
		return 0;
	}

	if (request_ctx->data_len + cursor > sizeof(post_payload_buf)) {
		cursor = 0;
		return -ENOMEM;
	}

	/* Copy payload to our buffer. Note that even for a small payload, it may arrive split into
	 * chunks (e.g. if the header size was such that the whole HTTP request exceeds the size of
	 * the client buffer).
	 */
	memcpy(post_payload_buf + cursor, request_ctx->data, request_ctx->data_len);
	cursor += request_ctx->data_len;

	if (status == HTTP_SERVER_DATA_FINAL) {
		parse_psu_post(post_payload_buf, cursor);
		cursor = 0;
	}

	return 0;
}

static struct http_resource_detail_dynamic psu_control_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = psu_control_handler,
	.user_data = NULL,
};

static int psu_telemetry_handler(struct http_client_ctx *client, enum http_data_status status,
				 const struct http_request_ctx *request_ctx,
				 struct http_response_ctx *response_ctx, void *user_data)
{
	static char json_buf[256];
	int ret;

	if (status == HTTP_SERVER_DATA_FINAL) {
		float vin = 0, vout = 0, iout = 0, temp = 0;
		int fan_rpm = 0;
		bool output_on = false;
		int ret_vin, ret_vout, ret_iout, ret_temp, ret_fan;

		/* Read all telemetry */
		ret_vin = psu_get_voltage_in(&vin);
		ret_vout = psu_get_voltage_out(&vout);
		ret_iout = psu_get_current_out(&iout);
		ret_temp = psu_get_temperature(&temp);
		ret_fan = psu_get_fan_speed(&fan_rpm);
		psu_get_output_status(&output_on);

		/* Format as JSON - use integer formatting to avoid float printf */
		ret = snprintf(json_buf, sizeof(json_buf),
			       "{\"vin\":%d.%02d,\"vout\":%d.%02d,\"iout\":%d.%03d,"
			       "\"temp\":%d.%d,\"fan_rpm\":%d,\"output_on\":%s}",
			       (int)vin, (int)(vin * 100) % 100,
			       (int)vout, (int)(vout * 100) % 100,
			       (int)iout, (int)(iout * 1000) % 1000,
			       (int)temp, (int)(temp * 10) % 10,
			       fan_rpm,
			       output_on ? "true" : "false");

		if (ret < 0 || ret >= sizeof(json_buf)) {
			LOG_ERR("Failed to format PSU JSON");
			return -ENOMEM;
		}

		response_ctx->body = json_buf;
		response_ctx->body_len = ret;
		response_ctx->final_chunk = true;
	}

	return 0;
}

static struct http_resource_detail_dynamic psu_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = psu_telemetry_handler,
	.user_data = NULL,
};

static uint16_t psu_http_service_port = 80;
HTTP_SERVICE_DEFINE(psu_http_service, NULL, &psu_http_service_port,
		    CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL);

/* Dynamic DNS-SD service registration with unique instance name */
static char psu_service_instance[DNS_SD_INSTANCE_MAX_SIZE + 1];
static uint16_t psu_http_port_be;
static struct dns_sd_rec psu_service_record;
static bool service_registered;
static int network_connected_count = 0;

HTTP_RESOURCE_DEFINE(index_html_gz_resource, psu_http_service, "/",
		     &index_html_gz_resource_detail);

HTTP_RESOURCE_DEFINE(main_js_gz_resource, psu_http_service, "/main.js",
		     &main_js_gz_resource_detail);

HTTP_RESOURCE_DEFINE(uptime_resource, psu_http_service, "/uptime", &uptime_resource_detail);

HTTP_RESOURCE_DEFINE(psu_control_resource, psu_http_service, "/psu-control", &psu_control_resource_detail);

HTTP_RESOURCE_DEFINE(psu_resource, psu_http_service, "/psu", &psu_resource_detail);

/* Register DNS-SD service with unique instance name based on MAC address */
static void register_dns_sd_service(struct net_if *iface)
{
	struct net_linkaddr *lladdr;
	int pos;

	if (service_registered)
		return;

	lladdr = net_if_get_link_addr(iface);
	if (lladdr == NULL || lladdr->len == 0) {
		LOG_ERR("Failed to get link address for DNS-SD service registration");
		return;
	}

	/* Create unique service instance name: "t0-psu-<MAC>" */
	pos = snprintk(psu_service_instance, sizeof(psu_service_instance), "t0-psu-");
	for (int i = 0; i < lladdr->len && pos < sizeof(psu_service_instance) - 2; i++) {
		pos += snprintk(&psu_service_instance[pos],
				sizeof(psu_service_instance) - pos,
				"%02x", lladdr->addr[i]);
	}

	/* Setup service record */
	psu_http_port_be = sys_cpu_to_be16(psu_http_service_port);
	psu_service_record.instance = psu_service_instance;
	psu_service_record.service = "_t0-psu";
	psu_service_record.proto = "_tcp";
	psu_service_record.domain = "local";
	psu_service_record.text = dns_sd_empty_txt;
	psu_service_record.text_size = sizeof(dns_sd_empty_txt);
	psu_service_record.port = &psu_http_port_be;

	/* Register with mDNS responder */
	if (mdns_responder_set_ext_records(&psu_service_record, 1) == 0)
		service_registered = true;
}

/* Set unique hostname based on MAC address */
static void set_unique_hostname(struct net_if *iface)
{
	struct net_linkaddr *lladdr;

	lladdr = net_if_get_link_addr(iface);
	if (lladdr == NULL || lladdr->len == 0) {
		LOG_ERR("Failed to get link address for hostname");
		return;
	}

	/* Set hostname postfix using MAC address (will be hex-encoded) */
	net_hostname_set_postfix(lladdr->addr, lladdr->len);
}

/* Handle network connected/disconnected events */
static void network_event_handler(struct net_mgmt_event_callback *cb,
				  uint64_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		char addr_str[NET_IPV4_ADDR_LEN];
		struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

		network_connected_count++;

		LOG_INF("=== Network Connected Event #%d (uptime=%" PRId64 "ms) ===",
			network_connected_count, k_uptime_get());
		LOG_INF("  Interface: %d (%p)", net_if_get_by_iface(iface), iface);

		if (ipv4 && ipv4->unicast[0].ipv4.addr_state == NET_ADDR_PREFERRED) {
			net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr,
				      addr_str, sizeof(addr_str));
			LOG_INF("  IP Address: %s", addr_str);
		}

		/* Set unique hostname before announcing */
		set_unique_hostname(iface);

		/* Register DNS-SD service - no manual IF_UP trigger needed */
		register_dns_sd_service(iface);
	} else if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		LOG_WRN("=== Network Disconnected Event (uptime=%" PRId64 "ms) ===",
			k_uptime_get());
		service_registered = false;
	}
}

static struct net_mgmt_event_callback network_cb;

static int network_setup(void)
{
	net_mgmt_init_event_callback(&network_cb, network_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&network_cb);
	return 0;
}

SYS_INIT(network_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

int main(void)
{
	LOG_INF("=== PSU Controller Starting ===");
	LOG_INF("  Build: %s %s", __DATE__, __TIME__);
	LOG_INF("  HTTP Port: %d", psu_http_service_port);

	psu_test();
	http_server_start();

	LOG_INF("HTTP server started");
	return 0;
}
