/*
 * mstpd - MSTP daemon
 * Copyright (C) 2026
 * GPL-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <asm/byteorder.h>
#include <net/if.h>

#include "ctl_functions.h"
#include "log.h"
#include "mstp.h"
#include "ubus_ctl.h"

#define GET_NUM_FROM_PRIO(p) (__be16_to_cpu(p) & 0x0FFF)
#define BR_ID_FMT "%01hhX.%03hX.%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX"
#define BR_ID_ARGS(x) ((GET_PRIORITY_FROM_IDENTIFIER(x) >> 4) & 0x0F), \
    GET_NUM_FROM_PRIO((x).s.priority), \
    (x).s.mac_address[0], (x).s.mac_address[1], (x).s.mac_address[2], \
    (x).s.mac_address[3], (x).s.mac_address[4], (x).s.mac_address[5]

typedef struct bridge_cfg_entry {
    struct bridge_cfg_entry *next;
    char *name;
    uint32_t timestamp;
    CIST_BridgeConfig cfg;
} bridge_cfg_entry_t;

static bridge_cfg_entry_t *cfg_head;

static uint32_t monotonic_sec(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec;
}

static bridge_cfg_entry_t *cfg_find(const char *name)
{
    bridge_cfg_entry_t *p = cfg_head;

    while(p)
    {
        if(!strcmp(p->name, name))
            return p;
        p = p->next;
    }

    return NULL;
}

static bridge_cfg_entry_t *cfg_get_or_create(const char *name)
{
    bridge_cfg_entry_t *e = cfg_find(name);

    if(e)
        return e;

    e = calloc(1, sizeof(*e));
    if(!e)
        return NULL;

    e->name = strdup(name);
    if(!e->name)
    {
        free(e);
        return NULL;
    }

    e->next = cfg_head;
    cfg_head = e;

    return e;
}

int ubus_ctl_store_bridge_config(const char *name,
                                 const char *proto,
                                 bool has_forward_delay,
                                 uint32_t forward_delay,
                                 bool has_hello_time,
                                 uint32_t hello_time,
                                 bool has_max_age,
                                 uint32_t max_age,
                                 bool has_ageing_time,
                                 uint32_t ageing_time)
{
    bridge_cfg_entry_t *e;
    CIST_BridgeConfig *bc;

    if(!name)
        return -1;

    e = cfg_get_or_create(name);
    if(!e)
        return -1;

    bc = &e->cfg;
    memset(bc, 0, sizeof(*bc));

    bc->protocol_version = protoRSTP;
    bc->set_protocol_version = true;

    if(proto)
    {
        if(!strcmp(proto, "mstp"))
            bc->protocol_version = protoMSTP;
        else if(!strcmp(proto, "stp"))
            bc->protocol_version = protoSTP;
    }

    if(has_forward_delay)
    {
        bc->bridge_forward_delay = forward_delay;
        bc->set_bridge_forward_delay = true;
    }

    if(has_hello_time)
    {
        /* The current MSTP core only accepts bridge hello time == 2. */
        if(hello_time == 2)
        {
            bc->bridge_hello_time = hello_time;
            bc->set_bridge_hello_time = true;
        }
        else
        {
            LOG("Ignore unsupported hello_time=%u for bridge %s (must be 2)",
                hello_time, name);
        }
    }

    if(has_max_age)
    {
        bc->bridge_max_age = max_age;
        bc->set_bridge_max_age = true;
    }

    if(has_ageing_time)
    {
        bc->bridge_ageing_time = ageing_time;
        bc->set_bridge_ageing_time = true;
    }

    e->timestamp = monotonic_sec();
    return 0;
}

int ubus_ctl_set_bridge_state(const char *name, bool enabled)
{
    int br_array[2];
    int bridge_idx;

    if(!name)
        return -1;

    bridge_idx = if_nametoindex(name);
    if(!bridge_idx)
        return -1;

    br_array[0] = 1;
    br_array[1] = bridge_idx;

    if(enabled)
    {
        bridge_cfg_entry_t *e = cfg_find(name);

        if(!e)
            return -1;

        if(CTL_set_cist_bridge_config(bridge_idx, &e->cfg) != 0)
            return -1;

        if(CTL_add_bridges(br_array) != 0)
            return -1;
    }
    else
    {
        if(CTL_del_bridges(br_array) != 0)
            return -1;
    }

    return 0;
}

int ubus_ctl_get_bridge_status(const char *name, struct ubus_bridge_status *out)
{
    CIST_BridgeStatus s;
    char root_port_name[IFNAMSIZ] = "";
    int br_index;
    unsigned int root_portno;

    if(!name || !out)
        return -1;

    br_index = if_nametoindex(name);
    if(!br_index)
        return -1;

    if(CTL_get_cist_bridge_status(br_index, &s, root_port_name) != 0)
        return -1;

    memset(out, 0, sizeof(*out));

    out->stp_enabled = s.stp_enabled;
    out->enabled = s.enabled;
    snprintf(out->bridge_id, sizeof(out->bridge_id), BR_ID_FMT,
             BR_ID_ARGS(s.bridge_id));
    snprintf(out->designated_root, sizeof(out->designated_root), BR_ID_FMT,
             BR_ID_ARGS(s.designated_root));
    snprintf(out->regional_root, sizeof(out->regional_root), BR_ID_FMT,
             BR_ID_ARGS(s.regional_root));

    root_portno = GET_NUM_FROM_PRIO(s.root_port_id);
    if(root_portno != 0)
        snprintf(out->root_port, sizeof(out->root_port), "%s (#%u)",
                 root_port_name, root_portno);
    else
        snprintf(out->root_port, sizeof(out->root_port), "None");

    out->path_cost = s.root_path_cost;
    out->internal_path_cost = s.internal_path_cost;
    out->max_age = s.root_max_age;
    out->bridge_max_age = s.bridge_max_age;
    out->forward_delay = s.root_forward_delay;
    out->bridge_forward_delay = s.bridge_forward_delay;
    out->tx_hold_count = s.tx_hold_count;
    out->max_hops = s.max_hops;
    out->hello_time = s.bridge_hello_time;
    out->ageing_time = s.Ageing_Time;
    snprintf(out->protocol_version, sizeof(out->protocol_version), "%s",
             (protoRSTP == s.protocol_version) ? "rstp" :
             ((protoMSTP <= s.protocol_version) ? "mstp" : "stp"));
    out->time_since_topology_change = s.time_since_topology_change;
    out->topology_change_count = s.topology_change_count;
    out->topology_change = s.topology_change;
    snprintf(out->topology_change_port, sizeof(out->topology_change_port),
             "%s", s.topology_change_port);
    snprintf(out->last_topology_change_port,
             sizeof(out->last_topology_change_port), "%s",
             s.last_topology_change_port);

    return 0;
}

void ubus_ctl_expire_configs(void)
{
    bridge_cfg_entry_t *p = cfg_head;
    bridge_cfg_entry_t *prev = NULL;
    uint32_t now = monotonic_sec();

    while(p)
    {
        bridge_cfg_entry_t *next = p->next;

        if(now - p->timestamp >= 60)
        {
            if(prev)
                prev->next = next;
            else
                cfg_head = next;

            free(p->name);
            free(p);
        }
        else
        {
            prev = p;
        }

        p = next;
    }
}
