/*
 * mstpd - MSTP daemon
 * Copyright (C) 2026
 * GPL-2.0
 */
#include <string.h>
#include <time.h>

#include <stdlib.h>

#include "ubus_config.h"

static struct bridge_config *bridge_cfg_head;

static uint32_t bridge_config_timestamp(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

struct bridge_config *bridge_config_get(const char *name, bool create)
{
    struct bridge_config *cfg = bridge_cfg_head;

    while(cfg)
    {
        if(!strcmp(cfg->name, name))
            goto out;
        cfg = cfg->next;
    }

    if(!create)
        return NULL;

    cfg = calloc(1, sizeof(*cfg));
    if(!cfg)
        return NULL;

    cfg->name = strdup(name);
    if(!cfg->name)
    {
        free(cfg);
        return NULL;
    }

    cfg->next = bridge_cfg_head;
    bridge_cfg_head = cfg;

out:
    cfg->timestamp = bridge_config_timestamp();
    return cfg;
}

void bridge_config_expire(void)
{
    struct bridge_config *cfg = bridge_cfg_head;
    struct bridge_config *prev = NULL;
    uint32_t ts = bridge_config_timestamp();

    while(cfg)
    {
        struct bridge_config *next = cfg->next;

        if(ts - cfg->timestamp < 60)
        {
            prev = cfg;
            cfg = next;
            continue;
        }

        if(prev)
            prev->next = next;
        else
            bridge_cfg_head = next;

        free(cfg->name);
        free(cfg);

        cfg = next;
    }
}
