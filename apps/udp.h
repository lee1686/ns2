/*
 * Copyright (c) Xerox Corporation 1997. All rights reserved.
 *  
 * License is granted to copy, to use, and to make and to use derivative
 * works for research and evaluation purposes, provided that Xerox is
 * acknowledged in all documentation pertaining to any such copy or derivative
 * work. Xerox grants no other licenses expressed or implied. The Xerox trade
 * name should not be used in any advertising without its written permission.
 *  
 * XEROX CORPORATION MAKES NO REPRESENTATIONS CONCERNING EITHER THE
 * MERCHANTABILITY OF THIS SOFTWARE OR THE SUITABILITY OF THIS SOFTWARE
 * FOR ANY PARTICULAR PURPOSE.  The software is provided "as is" without
 * express or implied warranty of any kind.
 *  
 * These notices must be retained in any copies of any part of this software.
 */

#ifndef ns_udp_h
#define ns_udp_h

#include "cbr.h"
#include "trafgen.h"

class TrafficGenerator;

/* This class is very similar to the CBR_Agent class, with 2 differences.
 * Instead of generating inter-packet times based on private state and
 * using fixed size packets, it invokes a method on a TrafficGenerator
 * object to determine the size of the next packet and the inter-packet
 * time.  The intention is to give the flexibility to associate
 * agents with different traffic generation processes.
 */

class UDP_Agent : public CBR_Agent {
 public:
        UDP_Agent();
	int command(int, const char*const*);
 protected:
	virtual void timeout(int);
	void start();
	void stop();
	TrafficGenerator *trafgen_;
	virtual void sendpkt();
};


#endif
