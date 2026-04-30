/*
 * mstpd - MSTP daemon
 * Copyright (C) 2026
 * GPL-2.0
 */
#ifndef __UBUS_CTL_H
#define __UBUS_CTL_H

#include <stdbool.h>
#include <stdint.h>

struct ubus_bridge_status {
    bool stp_enabled;
    bool enabled;
    char bridge_id[64];
    char designated_root[64];
    char regional_root[64];
    char root_port[48];
    uint32_t path_cost;
    uint32_t internal_path_cost;
    uint32_t max_age;
    uint32_t bridge_max_age;
    uint32_t forward_delay;
    uint32_t bridge_forward_delay;
    uint32_t tx_hold_count;
    uint32_t max_hops;
    uint32_t hello_time;
    uint32_t ageing_time;
    char protocol_version[8];
    uint32_t time_since_topology_change;
    uint32_t topology_change_count;
    bool topology_change;
    char topology_change_port[32];
    char last_topology_change_port[32];
};

int ubus_ctl_store_bridge_config(const char *name,
                                 const char *proto,
                                 bool has_forward_delay,
                                 uint32_t forward_delay,
                                 bool has_hello_time,
                                 uint32_t hello_time,
                                 bool has_max_age,
                                 uint32_t max_age,
                                 bool has_ageing_time,
                                 uint32_t ageing_time);

int ubus_ctl_set_bridge_state(const char *name, bool enabled);
int ubus_ctl_get_bridge_status(const char *name, struct ubus_bridge_status *out);
void ubus_ctl_expire_configs(void);

#endif
