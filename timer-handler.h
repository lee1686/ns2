
/*
 * timer-handler.h
 * Copyright (C) 1997 by USC/ISI
 * All rights reserved.                                            
 *                                                                
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation, advertising
 * materials, and other materials related to such distribution and use
 * acknowledge that the software was developed by the University of
 * Southern California, Information Sciences Institute.  The name of the
 * University may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * @(#) $Header: /home/smtatapudi/Thesis/nsnam/nsnam/ns-2/Attic/timer-handler.h,v 1.3 1997/08/10 07:50:02 mccanne Exp $ (USC/ISI)
 */

#ifndef timer_handler_h
#define timer_handler_h

#include "tclcl.h"
#include "scheduler.h"

/*
 * Abstract base class to deal with timer-style handlers.
 *
 *
 * To define a new timer, subclass this function and define handle:
 *
 * class MyTimer : public TimerHandler {
 * public:
 *	MyTimer(MyAgentClass *a) : AgentTimerHandler() { a_ = a; }
 *	virtual double expire(Event *e);
 * protected:
 *	MyAgentClass *a_;
 * };
 *
 * Then define expire:
 *
 * double
 * MyTimer::expire(Event *e)
 * {
 * 	// do the work
 *	// return TIMER_HANDLED;  // => do not reschedule timer
 *	// return delay;          // => reschedule timer after delay
 * }
 *
 * Often MyTimer will be a friend of MyAgentClass,
 * or expire() will only call a function of MyAgentClass.
 *
 * See tcp-rbp.{cc,h} for a real example.
 */
#define TIMER_HANDLED -1.0         // xxx: should be const double in class?
class TimerHandler : public Handler {
public:
	TimerHandler() : status_(TIMER_IDLE) { }

	void sched(double delay);    // cannot be pending
	void resched(double delay);  // may or may not be pending
				     // if you don't know the pending status,
				     // call resched()
	void cancel();               // must be pending
	enum TimerStatus { TIMER_IDLE, TIMER_PENDING, TIMER_HANDLING };
	int status() { return status_; };

protected:
	virtual void expire(Event *e) = 0;  // must be filled in by client
	// Should call resched() if it wants to reschedule the interface.

	virtual void handle(Event *e);
	int status_;
	Event event_;

private:
	inline void _sched(double delay) {
		(void)Scheduler::instance().schedule(this, &event_, delay);
	}
	inline void _cancel() {
		(void)Scheduler::instance().cancel(&event_);
	}
};

// Local Variables:
// mode:c++
// End:

#endif /* timer_handler_h */
