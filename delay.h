/*
 * Copyright (c) 1996-1997 The Regents of the University of California.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * 	This product includes software developed by the Network Research
 * 	Group at Lawrence Berkeley National Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) $Header: /home/smtatapudi/Thesis/nsnam/nsnam/ns-2/Attic/delay.h,v 1.9 1997/07/24 04:45:08 gnguyen Exp $ (LBL)
 */

#ifndef ns_delay_h
#define ns_delay_h

#include <assert.h>

#include "packet.h"
#include "queue.h"
#include "ip.h"
#include "connector.h"

class LinkDelay : public Connector {
 public:
	LinkDelay();
	void recv(Packet* p, Handler*);
	void send(Packet* p, Handler*);
	void handle(Event* e);
	double delay() { return delay_; }
	inline double txtime(Packet* p) {
		hdr_cmn *hdr = (hdr_cmn*)p->access(off_cmn_);
		return (hdr->size() * 8. / bandwidth_);
	}
	double bandwidth() const { return bandwidth_; }

 protected:
	int command(int argc, const char*const* argv);
	void reset();
	double bandwidth_;	/* bandwidth of underlying link (bits/sec) */
	double delay_;		/* line latency */
	Event intr_;
	int dynamic_;		/* indicates whether or not link is ~ */
	Event inTransit_;
	PacketQueue* itq_;
	Packet* nextPacket_;

private:
	void schedule_next() {
		if (! nextPacket_) {
			Scheduler& s = Scheduler::instance();

			if ((nextPacket_ = itq_->deque())) { /* ASSIGN */
				Event* pe = (Packet*) nextPacket_;
				double delay = pe->time_ - s.clock();
				assert(delay > 0);

				s.schedule(this, &inTransit_, delay);
			}
		}
	}
};

#endif
