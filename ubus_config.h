/*
 * mstpd - MSTP daemon
 * Copyright (C) 2026
 * GPL-2.0
 */
#ifndef __UBUS_CONFIG_H
#define __UBUS_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#include "mstp.h"

struct bridge_config {
    struct bridge_config *next;
    char *name;
    uint32_t timestamp;
    CIST_BridgeConfig config;
};

struct bridge_config *bridge_config_get(const char *name, bool create);
void bridge_config_expire(void);

#endif
