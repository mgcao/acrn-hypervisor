#ifndef EVENT_H
#define EVENT_H
#include <spinlock.h>

struct sched_event {
	spinlock_t lock;
	bool set;
	struct thread_object* waiting_thread;

	uint16_t type;
	uint16_t vm_id;
	uint16_t vcpu_id;
};

void init_event(struct sched_event *event);
void reset_event(struct sched_event *event);
void wait_event(struct sched_event *event);
void signal_event(struct sched_event *event);

#endif /* EVENT_H */
