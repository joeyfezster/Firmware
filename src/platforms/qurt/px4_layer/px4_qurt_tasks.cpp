/****************************************************************************
 *
 *   Copyright (C) 2015 Mark Charlebois. All rights reserved.
 *   Author: @author Mark Charlebois <charlebm#gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file px4_linux_tasks.c
 * Implementation of existing task API for Linux
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>
#include <sched.h>
#include <unistd.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <string>

#include <px4_tasks.h>

#define MAX_CMD_LEN 100

#define PX4_MAX_TASKS 100
struct task_entry
{
	pthread_t pid;
	std::string name;
	bool isused;
	task_entry() : isused(false) {}
};

static task_entry taskmap[PX4_MAX_TASKS];

typedef struct 
{
	px4_main_t entry;
	int argc;
	char *argv[];
	// strings are allocated after the 
} pthdata_t;

static void *entry_adapter ( void *ptr )
{
	pthdata_t *data;            
	data = (pthdata_t *) ptr;  

	data->entry(data->argc, data->argv);
	free(ptr);
	printf("Before px4_task_exit\n");
	px4_task_exit(0); 
	printf("After px4_task_exit\n");

	return NULL;
} 

void
px4_systemreset(bool to_bootloader)
{
	printf("Called px4_system_reset\n");
}

px4_task_t px4_task_spawn_cmd(const char *name, int scheduler, int priority, int stack_size, px4_main_t entry, char * const argv[])
{
	int rv;
	int argc = 0;
	int i;
	unsigned int len = 0;
	unsigned long offset;
	unsigned long structsize;
	char * p = (char *)argv;

        pthread_t task;
	pthread_attr_t attr;
	struct sched_param param;

	// Calculate argc
	while (p != (char *)0) {
		p = argv[argc];
		if (p == (char *)0)
			break;
		++argc;
		len += strlen(p)+1;
	}
        structsize = sizeof(pthdata_t)+(argc+1)*sizeof(char *);
	pthdata_t *taskdata;
    
	// not safe to pass stack data to the thread creation
	taskdata = (pthdata_t *)malloc(structsize+len);
	offset = ((unsigned long)taskdata)+structsize;

    	taskdata->entry = entry;
	taskdata->argc = argc;

	for (i=0; i<argc; i++) {
		printf("arg %d %s\n", i, argv[i]);
		taskdata->argv[i] = (char *)offset;
		strcpy((char *)offset, argv[i]);
		offset+=strlen(argv[i])+1;
	}
	// Must add NULL at end of argv
	taskdata->argv[argc] = (char *)0;

#if 0
	rv = pthread_attr_init(&attr);
	if (rv != 0) {
		printf("px4_task_spawn_cmd: failed to init thread attrs\n");
		return (rv < 0) ? rv : -rv;
	}
	rv = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	if (rv != 0) {
		printf("px4_task_spawn_cmd: failed to set inherit sched\n");
		return (rv < 0) ? rv : -rv;
	}
	rv = pthread_attr_setschedpolicy(&attr, scheduler);
	if (rv != 0) {
		printf("px4_task_spawn_cmd: failed to set sched policy\n");
		return (rv < 0) ? rv : -rv;
	}

	param.sched_priority = priority;

	rv = pthread_attr_setschedparam(&attr, &param);
	if (rv != 0) {
		printf("px4_task_spawn_cmd: failed to set sched param\n");
		return (rv < 0) ? rv : -rv;
	}
#endif

        //rv = pthread_create (&task, &attr, &entry_adapter, (void *) taskdata);
        rv = pthread_create (&task, NULL, &entry_adapter, (void *) taskdata);
	if (rv != 0) {

		if (rv == EPERM) {
			//printf("WARNING: NOT RUNING AS ROOT, UNABLE TO RUN REALTIME THREADS\n");
        		rv = pthread_create (&task, NULL, &entry_adapter, (void *) taskdata);
			if (rv != 0) {
				printf("px4_task_spawn_cmd: failed to create thread %d %d\n", rv, errno);
				return (rv < 0) ? rv : -rv;
			}
		}
		else {
			return (rv < 0) ? rv : -rv;
		}
	}

	for (i=0; i<PX4_MAX_TASKS; ++i) {
		if (taskmap[i].isused == false) {
			taskmap[i].pid = task;
			taskmap[i].name = name;
			taskmap[i].isused = true;
			break;
		}
	}
	if (i>=PX4_MAX_TASKS) {
		return -ENOSPC;
	}
        return i;
}

int px4_task_delete(px4_task_t id)
{
	int rv = 0;
	pthread_t pid;
	printf("Called px4_task_delete\n");

	if (id < PX4_MAX_TASKS && taskmap[id].isused)
		pid = taskmap[id].pid;
	else
		return -EINVAL;

	// If current thread then exit, otherwise cancel
        if (pthread_self() == pid) {
		taskmap[id].isused = false;
		pthread_exit(0);
	} else {
		rv = pthread_cancel(pid);
	}

	taskmap[id].isused = false;

	return rv;
}

void px4_task_exit(int ret)
{
	int i; 
	pthread_t pid = pthread_self();

	// Get pthread ID from the opaque ID
	for (i=0; i<PX4_MAX_TASKS; ++i) {
		if (taskmap[i].pid == pid) {
			taskmap[i].isused = false;
			break;
		}
	}
	if (i>=PX4_MAX_TASKS) 
		printf("px4_task_exit: self task not found!\n");
	else
		printf("px4_task_exit: %s\n", taskmap[i].name.c_str());

	pthread_exit((void *)(unsigned long)ret);
}

void px4_killall(void)
{
	//printf("Called px4_killall\n");
	for (int i=0; i<PX4_MAX_TASKS; ++i) {
		// FIXME - precludes pthread task to have an ID of 0
		if (taskmap[i].isused == true) {
			px4_task_delete(i);
		}
	}
}

int px4_task_kill(px4_task_t id, int sig)
{
	int rv = 0;
	pthread_t pid;
	//printf("Called px4_task_delete\n");

	if (id < PX4_MAX_TASKS && taskmap[id].pid != 0)
		pid = taskmap[id].pid;
	else
		return -EINVAL;

	// If current thread then exit, otherwise cancel
	rv = pthread_kill(pid, sig);

	return rv;
}

void px4_show_tasks()
{
	int idx;
	int count = 0;

	printf("Active Tasks:\n");
	for (idx=0; idx < PX4_MAX_TASKS; idx++)
	{
		if (taskmap[idx].isused) {
			printf("   %-10s %p\n", taskmap[idx].name.c_str(), taskmap[idx].pid);
			count++;
		}
	}
	if (count == 0)
		printf("   No running tasks\n");

}