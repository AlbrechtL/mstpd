/*
 * ustp - OpenWrt STP/RSTP/MSTP daemon
 * Copyright (C) 2021 Felix Fietkau <nbd@nbd.name>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <stdbool.h>

#include <libubus.h>
#include <net/if.h>

#include "epoll_loop.h"
#include "ubus_config.h"
#include "mstp.h"
#include "ubus.h"
#include "bridge_track.h"
#include "worker.h"

struct blob_buf b;

enum bridge_config_attr {
	BRIDGE_CONFIG_NAME,
	BRIDGE_CONFIG_PROTO,
	BRIDGE_CONFIG_FWD_DELAY,
	BRIDGE_CONFIG_HELLO_TIME,
	BRIDGE_CONFIG_MAX_AGE,
	BRIDGE_CONFIG_AGEING_TIME,
	__BRIDGE_CONFIG_MAX
};

static const struct blobmsg_policy bridge_config_policy[__BRIDGE_CONFIG_MAX] = {
	[BRIDGE_CONFIG_NAME] = { "name", BLOBMSG_TYPE_STRING },
	[BRIDGE_CONFIG_PROTO] = { "proto", BLOBMSG_TYPE_STRING },
	[BRIDGE_CONFIG_FWD_DELAY] = { "forward_delay", BLOBMSG_TYPE_INT32 },
	[BRIDGE_CONFIG_HELLO_TIME] = { "hello_time", BLOBMSG_TYPE_INT32 },
	[BRIDGE_CONFIG_MAX_AGE] = { "max_age", BLOBMSG_TYPE_INT32 },
	[BRIDGE_CONFIG_AGEING_TIME] = { "ageing_time", BLOBMSG_TYPE_INT32 },
};

static bool
ubus_set_bridge_config(struct blob_attr *attr)
{
	struct blob_attr *tb[__BRIDGE_CONFIG_MAX], *cur;
	struct bridge_config *cfg;
	CIST_BridgeConfig *bc;

	blobmsg_parse(bridge_config_policy, __BRIDGE_CONFIG_MAX, tb,
		      blobmsg_data(attr), blobmsg_len(attr));

	cur = tb[BRIDGE_CONFIG_NAME];
	if (!cur)
		return false;

	cfg = bridge_config_get(blobmsg_get_string(cur), true);

	bc = &cfg->config;
	bc->protocol_version = protoRSTP;
	bc->set_protocol_version = true;

	if ((cur = tb[BRIDGE_CONFIG_PROTO]) != NULL) {
		const char *proto = blobmsg_get_string(cur);

		if (!strcmp(proto, "mstp"))
			bc->protocol_version = protoMSTP;
		else if (!strcmp(proto, "stp"))
			bc->protocol_version = protoSTP;
	}

	if ((cur = tb[BRIDGE_CONFIG_FWD_DELAY]) != NULL) {
		bc->bridge_forward_delay = blobmsg_get_u32(cur);
		bc->set_bridge_forward_delay = true;
	}

	if ((cur = tb[BRIDGE_CONFIG_HELLO_TIME]) != NULL) {
		bc->bridge_hello_time = blobmsg_get_u32(cur);
		bc->set_bridge_hello_time = true;
	}

	if ((cur = tb[BRIDGE_CONFIG_AGEING_TIME]) != NULL) {
		bc->bridge_ageing_time = blobmsg_get_u32(cur);
		bc->set_bridge_ageing_time = true;
	}

	if ((cur = tb[BRIDGE_CONFIG_MAX_AGE]) != NULL) {
		bc->bridge_max_age = blobmsg_get_u32(cur);
		bc->set_bridge_max_age = true;
	}

	return true;
}

static void
ubus_send_ok(struct ubus_context *ctx, struct ubus_request_data *req)
{
	blob_buf_init(&b, 0);
	blobmsg_add_u8(&b, "ok", 1);
	ubus_send_reply(ctx, req, b.head);
}

static int
ubus_add_bridge(struct ubus_context *ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *method,
		struct blob_attr *msg)
{
	if (!ubus_set_bridge_config(msg))
		return UBUS_STATUS_INVALID_ARGUMENT;

	ubus_send_ok(ctx, req);

	return 0;
}

enum bridge_state_attr {
	BRIDGE_STATE_NAME,
	BRIDGE_STATE_ENABLED,
	__BRIDGE_STATE_MAX
};

static const struct blobmsg_policy bridge_state_policy[__BRIDGE_STATE_MAX] = {
	[BRIDGE_STATE_NAME] = { "name", BLOBMSG_TYPE_STRING },
	[BRIDGE_STATE_ENABLED] = { "enabled", BLOBMSG_TYPE_BOOL },
};

static int
ubus_bridge_state(struct ubus_context *ctx, struct ubus_object *obj,
		  struct ubus_request_data *req, const char *method,
		  struct blob_attr *msg)
{
	struct blob_attr *tb[__BRIDGE_STATE_MAX];
	struct bridge_config *cfg;
	const char *bridge_name;
	int bridge_idx;

	blobmsg_parse(bridge_state_policy, __BRIDGE_STATE_MAX, tb,
		      blobmsg_data(msg), blobmsg_len(msg));

	if (!tb[BRIDGE_STATE_NAME] || !tb[BRIDGE_STATE_ENABLED])
		return UBUS_STATUS_INVALID_ARGUMENT;

	bridge_name = blobmsg_get_string(tb[BRIDGE_STATE_NAME]);
	bridge_idx = if_nametoindex(bridge_name);
	if (!bridge_idx)
		return UBUS_STATUS_NOT_FOUND;

	if (blobmsg_get_bool(tb[BRIDGE_STATE_ENABLED])) {
		cfg = bridge_config_get(bridge_name, false);
		if (!cfg)
			return UBUS_STATUS_NOT_FOUND;

		if (bridge_create(bridge_idx, &cfg->config) != 0)
			return UBUS_STATUS_INVALID_ARGUMENT;
	} else {
		bridge_delete(bridge_idx);
	}

	ubus_send_ok(ctx, req);

	return 0;
}

enum port_config_attr {
	PORT_CONFIG_BRIDGE,
	PORT_CONFIG_PORT,
	PORT_CONFIG_ADMIN_EDGE,
	PORT_CONFIG_AUTO_EDGE,
	__PORT_CONFIG_MAX
};

static const struct blobmsg_policy port_config_policy[__PORT_CONFIG_MAX] = {
	[PORT_CONFIG_BRIDGE] = { "bridge", BLOBMSG_TYPE_STRING },
	[PORT_CONFIG_PORT] = { "port", BLOBMSG_TYPE_STRING },
	[PORT_CONFIG_ADMIN_EDGE] = { "admin_edge_port", BLOBMSG_TYPE_BOOL },
	[PORT_CONFIG_AUTO_EDGE] = { "auto_edge_port", BLOBMSG_TYPE_BOOL },
};

static int
ubus_set_port_config(struct ubus_context *ctx, struct ubus_object *obj,
			     struct ubus_request_data *req, const char *method,
			     struct blob_attr *msg)
{
	struct blob_attr *tb[__PORT_CONFIG_MAX];
	struct worker_event ev = {};

	blobmsg_parse(port_config_policy, __PORT_CONFIG_MAX, tb,
		      blobmsg_data(msg), blobmsg_len(msg));

	if (!tb[PORT_CONFIG_BRIDGE] || !tb[PORT_CONFIG_PORT])
		return UBUS_STATUS_INVALID_ARGUMENT;

	ev.bridge_idx = if_nametoindex(blobmsg_get_string(tb[PORT_CONFIG_BRIDGE]));
	ev.port_idx = if_nametoindex(blobmsg_get_string(tb[PORT_CONFIG_PORT]));
	if (!ev.bridge_idx || !ev.port_idx)
		return UBUS_STATUS_NOT_FOUND;

	if (tb[PORT_CONFIG_ADMIN_EDGE]) {
		ev.port_config.admin_edge_port =
			blobmsg_get_bool(tb[PORT_CONFIG_ADMIN_EDGE]);
		ev.port_config.set_admin_edge_port = true;
	}

	if (tb[PORT_CONFIG_AUTO_EDGE]) {
		ev.port_config.auto_edge_port =
			blobmsg_get_bool(tb[PORT_CONFIG_AUTO_EDGE]);
		ev.port_config.set_auto_edge_port = true;
	}

	if (!ev.port_config.set_admin_edge_port &&
	    !ev.port_config.set_auto_edge_port)
		return UBUS_STATUS_INVALID_ARGUMENT;

	ev.type = WORKER_EV_PORT_CONFIG;
	worker_queue_event(&ev);
	ubus_send_ok(ctx, req);

	return 0;
}

enum port_status_attr {
	PORT_STATUS_BRIDGE,
	PORT_STATUS_PORT,
	__PORT_STATUS_MAX
};

static const struct blobmsg_policy port_status_policy[__PORT_STATUS_MAX] = {
	[PORT_STATUS_BRIDGE] = { "bridge", BLOBMSG_TYPE_STRING },
	[PORT_STATUS_PORT] = { "port", BLOBMSG_TYPE_STRING },
};

static int
ubus_get_port_status(struct ubus_context *ctx, struct ubus_object *obj,
			     struct ubus_request_data *req, const char *method,
			     struct blob_attr *msg)
{
	struct blob_attr *tb[__PORT_STATUS_MAX];
	bridge_t *br;
	port_t *prt;
	CIST_PortStatus status;
	int bridge_idx;
	int port_idx;

	blobmsg_parse(port_status_policy, __PORT_STATUS_MAX, tb,
		      blobmsg_data(msg), blobmsg_len(msg));

	if (!tb[PORT_STATUS_BRIDGE] || !tb[PORT_STATUS_PORT])
		return UBUS_STATUS_INVALID_ARGUMENT;

	bridge_idx = if_nametoindex(blobmsg_get_string(tb[PORT_STATUS_BRIDGE]));
	port_idx = if_nametoindex(blobmsg_get_string(tb[PORT_STATUS_PORT]));
	if (!bridge_idx || !port_idx)
		return UBUS_STATUS_NOT_FOUND;

	br = bridge_find(bridge_idx);
	if (!br)
		return UBUS_STATUS_NOT_FOUND;

	prt = port_find(br, port_idx);
	if (!prt)
		return UBUS_STATUS_NOT_FOUND;

	MSTP_IN_get_cist_port_status(prt, &status);

	blob_buf_init(&b, 0);
	blobmsg_add_u8(&b, "admin_edge_port", status.admin_edge_port);
	blobmsg_add_u8(&b, "auto_edge_port", status.auto_edge_port);
	blobmsg_add_u8(&b, "oper_edge_port", status.oper_edge_port);
	ubus_send_reply(ctx, req, b.head);

	return 0;
}

static const struct ubus_method ustp_methods[] = {
	UBUS_METHOD("add_bridge", ubus_add_bridge, bridge_config_policy),
	UBUS_METHOD("bridge_state", ubus_bridge_state, bridge_state_policy),
	UBUS_METHOD("set_port_config", ubus_set_port_config, port_config_policy),
	UBUS_METHOD("get_port_status", ubus_get_port_status, port_status_policy),
};

static struct ubus_object_type ustp_object_type =
	UBUS_OBJECT_TYPE("ustp", ustp_methods);

static struct ubus_object ustp_object = {
	.name = "ustp",
	.type = &ustp_object_type,
	.methods = ustp_methods,
	.n_methods = ARRAY_SIZE(ustp_methods),
};

static int
netifd_device_cb(struct ubus_context *ctx, struct ubus_object *obj,
		 struct ubus_request_data *req, const char *method,
		 struct blob_attr *msg)
{
	if (strcmp(method, "stp_init") != 0)
		return 0;

	ubus_set_bridge_config(msg);

	return 0;
}

static struct ubus_subscriber netifd_sub = {
	.cb = netifd_device_cb,
	.remove_cb = NULL,
};

/* ubus context and event handler for epoll integration */
static struct ubus_context *ubus_ctx;
static struct epoll_event_handler ubus_handler;

/* Reconnect and subscription retry state */
static int ubus_reconnect_retry;
static int netifd_subscribe_retry;

static void
ubus_on_readable(uint32_t events, struct epoll_event_handler *h)
{
	if (!ubus_ctx)
		return;

	ubus_handle_event(ubus_ctx);
}

static int
ubus_try_reconnect(void)
{
	if (!ubus_ctx)
		return -1;

	if (ubus_reconnect(ubus_ctx, NULL) != 0)
		return -1;

	/* Reconnected, re-register objects and subscribers */
	ubus_add_object(ubus_ctx, &ustp_object);
	ubus_register_subscriber(ubus_ctx, &netifd_sub);

	/* Reset netifd subscription retry to immediately attempt subscription */
	netifd_subscribe_retry = 1;

	return 0;
}

static int
ubus_try_subscribe_netifd(void)
{
	uint32_t id;

	if (!ubus_ctx || ubus_ctx->sock.fd < 0)
		return -1;

	if (ubus_lookup_id(ubus_ctx, "network.device", &id) != 0)
		return -1;

	if (ubus_subscribe(ubus_ctx, &netifd_sub, id) != 0)
		return -1;

	/* Success: invoke stp_init to get existing bridge configs */
	blob_buf_init(&b, 0);
	ubus_invoke(ubus_ctx, id, "stp_init", b.head, NULL, NULL, 1000);

	netifd_subscribe_retry = 0;

	return 0;
}

int ustp_ubus_init(void)
{
	/* Create ubus context */
	ubus_ctx = ubus_connect(NULL);
	if (!ubus_ctx)
		return -1;

	/* Register object and subscriber */
	ubus_add_object(ubus_ctx, &ustp_object);
	ubus_register_subscriber(ubus_ctx, &netifd_sub);

	/* Register ubus socket FD with epoll for readability */
	ubus_handler.fd = ubus_ctx->sock.fd;
	ubus_handler.handler = ubus_on_readable;
	if (add_epoll(&ubus_handler) != 0) {
		ubus_free(ubus_ctx);
		ubus_ctx = NULL;
		return -1;
	}

	/* Initialize retry state */
	ubus_reconnect_retry = 0;
	netifd_subscribe_retry = 1;

	/* Attempt initial netifd subscription */
	ubus_try_subscribe_netifd();

	return 0;
}

void ustp_ubus_exit(void)
{
	if (!ubus_ctx)
		return;

	/* Remove from epoll */
	remove_epoll(&ubus_handler);

	/* Clean up ubus context */
	ubus_shutdown(ubus_ctx);
	ubus_free(ubus_ctx);
	ubus_ctx = NULL;
	ubus_reconnect_retry = 0;
	netifd_subscribe_retry = 0;
}

void ustp_ubus_one_second(void)
{
	if (!ubus_ctx)
		return;

	/* Try reconnect if disconnected */
	if (ubus_ctx->sock.fd < 0) {
		ubus_reconnect_retry++;
		if (ubus_reconnect_retry >= 1) {
			if (ubus_try_reconnect() == 0)
				ubus_reconnect_retry = 0;
		}
		return;
	}

	/* Try netifd subscription if not yet subscribed */
	if (netifd_subscribe_retry) {
		netifd_subscribe_retry++;
		if (netifd_subscribe_retry >= 1) {
			ubus_try_subscribe_netifd();
		}
	}

	/* Expire old bridge config entries */
	bridge_config_expire();
}
