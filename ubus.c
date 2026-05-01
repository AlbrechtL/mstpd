/*
 * mstpd - MSTP daemon
 * Copyright (C) 2026
 * GPL-2.0
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <libubus.h>
#include <libubox/uloop.h>

#include "ubus.h"
#include "ubus_ctl.h"

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

static bool ubus_set_bridge_config(struct blob_attr *attr)
{
    struct blob_attr *tb[__BRIDGE_CONFIG_MAX], *cur;
    const char *name;
    const char *proto = NULL;
    bool has_forward_delay = false;
    bool has_hello_time = false;
    bool has_max_age = false;
    bool has_ageing_time = false;
    uint32_t forward_delay = 0;
    uint32_t hello_time = 0;
    uint32_t max_age = 0;
    uint32_t ageing_time = 0;

    if(!attr)
        return false;

    blobmsg_parse(bridge_config_policy, __BRIDGE_CONFIG_MAX, tb,
                  blobmsg_data(attr), blobmsg_len(attr));

    cur = tb[BRIDGE_CONFIG_NAME];
    if(!cur)
        return false;
    name = blobmsg_get_string(cur);

    if((cur = tb[BRIDGE_CONFIG_PROTO]) != NULL)
        proto = blobmsg_get_string(cur);

    if((cur = tb[BRIDGE_CONFIG_FWD_DELAY]) != NULL)
    {
        has_forward_delay = true;
        forward_delay = blobmsg_get_u32(cur);
    }

    if((cur = tb[BRIDGE_CONFIG_HELLO_TIME]) != NULL)
    {
        has_hello_time = true;
        hello_time = blobmsg_get_u32(cur);
    }

    if((cur = tb[BRIDGE_CONFIG_AGEING_TIME]) != NULL)
    {
        has_ageing_time = true;
        ageing_time = blobmsg_get_u32(cur);
    }

    if((cur = tb[BRIDGE_CONFIG_MAX_AGE]) != NULL)
    {
        has_max_age = true;
        max_age = blobmsg_get_u32(cur);
    }

    return ubus_ctl_store_bridge_config(name, proto,
                                        has_forward_delay, forward_delay,
                                        has_hello_time, hello_time,
                                        has_max_age, max_age,
                                        has_ageing_time, ageing_time) == 0;
}

static const char *ubus_get_bridge_name(struct blob_attr *attr)
{
    static struct blob_attr *tb[__BRIDGE_CONFIG_MAX];

    if(!attr)
        return NULL;

    blobmsg_parse(bridge_config_policy, __BRIDGE_CONFIG_MAX, tb,
                  blobmsg_data(attr), blobmsg_len(attr));

    if(!tb[BRIDGE_CONFIG_NAME])
        return NULL;

    return blobmsg_get_string(tb[BRIDGE_CONFIG_NAME]);
}

static void ubus_send_ok(struct ubus_context *ctx,
                         struct ubus_request_data *req)
{
    blob_buf_init(&b, 0);
    blobmsg_add_u8(&b, "ok", 1);
    ubus_send_reply(ctx, req, b.head);
}

static int ubus_add_bridge(struct ubus_context *ctx, struct ubus_object *obj,
                           struct ubus_request_data *req, const char *method,
                           struct blob_attr *msg)
{
    if(!ubus_set_bridge_config(msg))
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

static int ubus_bridge_state(struct ubus_context *ctx, struct ubus_object *obj,
                             struct ubus_request_data *req,
                             const char *method, struct blob_attr *msg)
{
    struct blob_attr *tb[__BRIDGE_STATE_MAX];
    const char *bridge_name;

    if(!msg)
        return UBUS_STATUS_INVALID_ARGUMENT;

    blobmsg_parse(bridge_state_policy, __BRIDGE_STATE_MAX, tb,
                  blobmsg_data(msg), blobmsg_len(msg));

    if(!tb[BRIDGE_STATE_NAME] || !tb[BRIDGE_STATE_ENABLED])
        return UBUS_STATUS_INVALID_ARGUMENT;

    bridge_name = blobmsg_get_string(tb[BRIDGE_STATE_NAME]);

    if(ubus_ctl_set_bridge_state(bridge_name,
                                 blobmsg_get_bool(tb[BRIDGE_STATE_ENABLED])) != 0)
        return UBUS_STATUS_UNKNOWN_ERROR;

    ubus_send_ok(ctx, req);
    return 0;
}

enum bridge_status_attr {
    BRIDGE_STATUS_NAME,
    __BRIDGE_STATUS_MAX
};

static const struct blobmsg_policy bridge_status_policy[__BRIDGE_STATUS_MAX] = {
    [BRIDGE_STATUS_NAME] = { "name", BLOBMSG_TYPE_STRING },
};

static int ubus_bridge_status(struct ubus_context *ctx, struct ubus_object *obj,
                              struct ubus_request_data *req,
                              const char *method, struct blob_attr *msg)
{
    struct blob_attr *tb[__BRIDGE_STATUS_MAX];
    struct ubus_bridge_status s;

    if(!msg)
        return UBUS_STATUS_INVALID_ARGUMENT;

    blobmsg_parse(bridge_status_policy, __BRIDGE_STATUS_MAX, tb,
                  blobmsg_data(msg), blobmsg_len(msg));

    if(!tb[BRIDGE_STATUS_NAME])
        return UBUS_STATUS_INVALID_ARGUMENT;

    if(ubus_ctl_get_bridge_status(blobmsg_get_string(tb[BRIDGE_STATUS_NAME]),
                                  &s) != 0)
        return UBUS_STATUS_UNKNOWN_ERROR;

    blob_buf_init(&b, 0);
    blobmsg_add_u8(&b, "stp_enabled", s.stp_enabled);
    blobmsg_add_u8(&b, "enabled", s.enabled);
    blobmsg_add_string(&b, "bridge_id", s.bridge_id);
    blobmsg_add_string(&b, "designated_root", s.designated_root);
    blobmsg_add_string(&b, "regional_root", s.regional_root);
    blobmsg_add_string(&b, "root_port", s.root_port);
    blobmsg_add_u32(&b, "path_cost", s.path_cost);
    blobmsg_add_u32(&b, "internal_path_cost", s.internal_path_cost);
    blobmsg_add_u32(&b, "max_age", s.max_age);
    blobmsg_add_u32(&b, "bridge_max_age", s.bridge_max_age);
    blobmsg_add_u32(&b, "forward_delay", s.forward_delay);
    blobmsg_add_u32(&b, "bridge_forward_delay", s.bridge_forward_delay);
    blobmsg_add_u32(&b, "tx_hold_count", s.tx_hold_count);
    blobmsg_add_u32(&b, "max_hops", s.max_hops);
    blobmsg_add_u32(&b, "hello_time", s.hello_time);
    blobmsg_add_u32(&b, "ageing_time", s.ageing_time);
    blobmsg_add_string(&b, "protocol_version", s.protocol_version);
    blobmsg_add_u32(&b, "time_since_topology_change", s.time_since_topology_change);
    blobmsg_add_u32(&b, "topology_change_count", s.topology_change_count);
    blobmsg_add_u8(&b, "topology_change", s.topology_change);
    blobmsg_add_string(&b, "topology_change_port", s.topology_change_port);
    blobmsg_add_string(&b, "last_topology_change_port", s.last_topology_change_port);
    ubus_send_reply(ctx, req, b.head);

    return 0;
}

static const struct ubus_method ustp_methods[] = {
    UBUS_METHOD("add_bridge", ubus_add_bridge, bridge_config_policy),
    UBUS_METHOD("bridge_state", ubus_bridge_state, bridge_state_policy),
    UBUS_METHOD("bridge_status", ubus_bridge_status, bridge_status_policy),
};

static struct ubus_object_type ustp_object_type =
    UBUS_OBJECT_TYPE("ustp", ustp_methods);

static struct ubus_object ustp_object = {
    .name = "ustp",
    .type = &ustp_object_type,
    .methods = ustp_methods,
    .n_methods = ARRAY_SIZE(ustp_methods),
};

static int netifd_device_cb(struct ubus_context *ctx, struct ubus_object *obj,
                            struct ubus_request_data *req,
                            const char *method, struct blob_attr *msg)
{
    const char *bridge_name;

    if(strcmp(method, "stp_init") != 0)
        return 0;

    bridge_name = ubus_get_bridge_name(msg);
    if(!bridge_name)
        return 0;

    if(!ubus_set_bridge_config(msg))
        return 0;

    /*
     * Keep stp_init side-effect free (like ustp): only cache bridge config.
     * The actual enable/disable transition is handled by explicit
     * ustp.bridge_state calls from netifd.
     */
    return 0;
}

static struct ubus_auto_conn conn;
static struct ubus_subscriber netifd_sub;

static void netifd_sub_cb(struct uloop_timeout *t)
{
    uint32_t id;

    if(ubus_lookup_id(&conn.ctx, "network.device", &id) != 0 ||
       ubus_subscribe(&conn.ctx, &netifd_sub, id) != 0)
    {
        uloop_timeout_set(t, 1000);
        return;
    }

    blob_buf_init(&b, 0);
    ubus_invoke(&conn.ctx, id, "stp_init", b.head, NULL, NULL, 1000);
}

static struct uloop_timeout netifd_sub_timer = {
    .cb = netifd_sub_cb,
};

static void netifd_device_remove_cb(struct ubus_context *ctx,
                                    struct ubus_subscriber *obj, uint32_t id)
{
    uloop_timeout_set(&netifd_sub_timer, 1000);
}

static struct ubus_subscriber netifd_sub = {
    .cb = netifd_device_cb,
    .remove_cb = netifd_device_remove_cb,
};

static void bridge_cfg_expire_cb(struct uloop_timeout *t)
{
    ubus_ctl_expire_configs();
    uloop_timeout_set(t, 60000);
}

static struct uloop_timeout bridge_cfg_expire_timer = {
    .cb = bridge_cfg_expire_cb,
};

static void ubus_connect_handler(struct ubus_context *ctx)
{
    ubus_add_object(ctx, &ustp_object);
    ubus_register_subscriber(ctx, &netifd_sub);
    uloop_timeout_set(&netifd_sub_timer, 1);
}

int ustp_ubus_init(void)
{
    if(uloop_init() != 0)
        return -1;

    conn.cb = ubus_connect_handler;
    ubus_auto_connect(&conn);

    uloop_timeout_set(&bridge_cfg_expire_timer, 60000);
    return 0;
}

int ustp_ubus_run(void)
{
    return uloop_run();
}

void ustp_ubus_exit(void)
{
    uloop_timeout_cancel(&netifd_sub_timer);
    uloop_timeout_cancel(&bridge_cfg_expire_timer);

    ubus_remove_object(&conn.ctx, &ustp_object);
    ubus_unregister_subscriber(&conn.ctx, &netifd_sub);
    ubus_auto_shutdown(&conn);
    uloop_done();
}
