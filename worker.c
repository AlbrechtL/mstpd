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
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

#include "worker.h"
#include "bridge_ctl.h"
#include "bridge_track.h"
#include "packet.h"

static pthread_t w_thread;
static pthread_mutex_t w_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t w_cond = PTHREAD_COND_INITIALIZER;
static LIST_HEAD(w_queue);

struct worker_queued_event {
	struct list_head list;
	struct worker_event ev;
};

static struct worker_event *worker_next_event(void)
{
	struct worker_queued_event *ev;
	static struct worker_event ev_data;

	pthread_mutex_lock(&w_lock);
	while (list_empty(&w_queue))
		pthread_cond_wait(&w_cond, &w_lock);

	ev = list_entry(w_queue.next, struct worker_queued_event, list);
	list_del(&ev->list);
	pthread_mutex_unlock(&w_lock);

	memcpy(&ev_data, &ev->ev, sizeof(ev_data));
	free(ev);

	return &ev_data;
}

static void
handle_worker_event(struct worker_event *ev)
{
	switch (ev->type) {
	case WORKER_EV_ONE_SECOND:
		bridge_one_second();
		break;
	case WORKER_EV_BRIDGE_EVENT:
		bridge_event_handler();
		break;
	case WORKER_EV_RECV_PACKET:
		packet_rcv();
		break;
	case WORKER_EV_BRIDGE_ADD:
		bridge_create(ev->bridge_idx, &ev->bridge_config);
		break;
	case WORKER_EV_BRIDGE_REMOVE:
		bridge_delete(ev->bridge_idx);
		break;
	case WORKER_EV_PORT_CONFIG: {
		bridge_t *br = bridge_find(ev->bridge_idx);
		if (br) {
			port_t *prt = port_find(br, ev->port_idx);
			if (prt)
				MSTP_IN_set_cist_port_config(prt, &ev->port_config);
		}
		break;
	}
	default:
		return;
	}
}

static void *worker_thread_fn(void *arg)
{
	struct worker_event *ev;

	while (1) {
		ev = worker_next_event();
		if (ev->type == WORKER_EV_SHUTDOWN)
			break;

		handle_worker_event(ev);
	}

	return NULL;
}

int worker_init(void)
{
	return pthread_create(&w_thread, NULL, worker_thread_fn, NULL);
}

void worker_cleanup(void)
{
	struct worker_event ev = {
		.type = WORKER_EV_SHUTDOWN,
	};

	worker_queue_event(&ev);
	pthread_join(w_thread, NULL);
}

void worker_queue_event(struct worker_event *ev)
{
	struct worker_queued_event *evc;

	evc = malloc(sizeof(*evc));
	memcpy(&evc->ev, ev, sizeof(*ev));

	pthread_mutex_lock(&w_lock);
	list_add_tail(&evc->list, &w_queue);
	pthread_mutex_unlock(&w_lock);

	pthread_cond_signal(&w_cond);
}
