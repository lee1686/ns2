/*
 * Copyright (c) 1997 The Regents of the University of California.
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
 */

/*
 * This code below was motivated in part by code contributed by
 * Kathie Nichols (nichols@com21.com).  The code below is based primarily
 * on the 4.4BSD TCP implementation. -KF [kfall@ee.lbl.gov]
 *
 * Some Warnings:
 *	this version of TCP will not work correctly if the sequence number
 *	goes above 2147483648 due to sequence number wrap
 *
 *	this version of TCP currently sends data on the 3rd segment of
 *	the initial 3-way handshake.  So, the typical sequence of events is
 *		A   ------> SYN ------> B
 *		A   <----- SYN+ACK ---- B
 *		A   ------> ACK+data -> B
 *	whereas many "real-world" TCPs don't send data until a 4th segment
 *
 *	there is no dynamic receiver's advertised window.   The advertised
 *	window is simulated by simply telling the sender a bound on the window
 *	size (wnd_).
 *
 *	in real TCP, a user process performing a read (via PRU_RCVD)
 *		calls tcp_output each time to (possibly) send a window
 *		update.  Here we don't have a user process, so we simulate
 *		a user process always ready to consume all the receive buffer
 *
 * Notes:
 *	wnd_, wnd_init_, cwnd_, ssthresh_ are in segment units
 *	sequence and ack numbers are in byte units
 *
 * Futures:
 *	there are different existing TCPs with respect to how
 *	ack's are handled on connection startup.  Some delay
 *	the ack for the first segment, which can cause connections
 *	to take longer to start up than if we be sure to ack it quickly.
 */

#ifndef lint
static const char rcsid[] =
    "@(#) $Header: /home/smtatapudi/Thesis/nsnam/nsnam/ns-2/Attic/tcp-full.cc,v 1.8 1997/08/10 07:49:59 mccanne Exp $ (LBL)";
#endif

#include "tclcl.h"
#include "ip.h"
#include "tcp-full.h"
#include "flags.h"
#include "random.h"
#include "template.h"

#define	TRUE 	1
#define	FALSE 	0

static class FullTcpClass : public TclClass { 
public:
    FullTcpClass() : TclClass("Agent/TCP/FullTcp") {}
    TclObject* create(int, const char*const*) { 
        return (new FullTcpAgent());
    }
} class_full;

/*
 * Tcl bound variables:
 *	segsperack: for delayed ACKs, how many to wait before ACKing
 *	segsize: segment size to use when sending
 */
FullTcpAgent::FullTcpAgent() : flags_(0),
	state_(TCPS_CLOSED), rq_(rcv_nxt_), last_ack_sent_(-1), delack_timer_(this),	delsnd_timer_(this), rtx_timer_(this)
{
	bind("segsperack_", &segs_per_ack_);
	bind("segsize_", &maxseg_);
	bind("tcprexmtthresh_", &tcprexmtthresh_);
	bind("iss_", &iss_);
	bind_bool("nodelay_", &nodelay_);
	bind_bool("data_on_syn_",&data_on_syn_);
	bind_bool("dupseg_fix_", &dupseg_fix_);
	bind_bool("dupack_reset_", &dupack_reset_);
	bind("interval_", &delack_interval_);

	reset();
}

/*
 * reset to starting point, don't set state_ here,
 * because our starting point might be LISTEN rather
 * than CLOSED if we're a passive opener
 */
void
FullTcpAgent::reset()
{
	last_ack_sent_ = -1;
	rcv_nxt_ = -1;
	flags_ = 0;
	TcpAgent::reset();
	t_seqno_ = iss_;
}

/*
 * headersize:
 *	how big is an IP+TCP header in bytes
 *	(for now, is the basic size, but may changes
 *	 in the future w/options; fix for sack)
 */
int
FullTcpAgent::headersize()
{
	return (TCPIP_BASE_PKTSIZE);
}

/*
 * cancel any pending timers
 * free up the reassembly queue if there's anything there
 */
FullTcpAgent::~FullTcpAgent()
{
	register i;
	for (i = 0; i < NTIMER; i++)
		if (pending_[i])
			cancel(i);
	rq_.clear();
}

/*
 * the 'advance' interface to the regular tcp is in packet
 * units.  Here we scale this to bytes for full tcp.
 *
 * 'advance' is normally called by an "application" (i.e. data source)
 * to signal that there is something to send
 *
 * 'curseq_' is the last byte number provided by the application
 */
void
FullTcpAgent::advance(int np)
{
	// XXX hack:
	//	because np is in packets and a data source
	//	may pass a *huge* number as a way to tell us
	//	to go forever, just look for the huge number
	//	and if it's there, pre-divide it
	if (np >= 0x10000000)
		np /= maxseg_;

	curseq_ += (np * maxseg_);

	//
	// state-specific operations:
	//	if CLOSED, do an active open/connect
	//	if ESTABLISHED, just try to send more
	//	if above ESTABLISHED, we are closing, so don't allow
	//	if anything else (establishing), do nothing here
	//
	if (state_ > TCPS_ESTABLISHED) {
		double now = Scheduler::instance().clock();
		fprintf(stderr,
		 "%f: FullTcpAgent::advance(%s): cannot advance while in state %d\n",
		 now, name(), state_);
		return;
	} else if (state_ == TCPS_CLOSED)
		connect();		// initiate new connection
	else if (state_ == TCPS_ESTABLISHED)
		send_much(0, REASON_NORMAL, 0);
	return;
}
/*
 * flags that are completely dependent on the tcp state
 * (in real TCP, see tcp_fsm.h, the "tcp_outflags" array)
 */
int FullTcpAgent::outflags()
{
	int flags = 0;
	if ((state_ != TCPS_LISTEN) && (state_ != TCPS_SYN_SENT))
		flags |= TH_ACK;

	if ((state_ == TCPS_SYN_SENT) || (state_ == TCPS_SYN_RECEIVED))
		flags |= TH_SYN;

	if ((state_ == TCPS_FIN_WAIT_1) || (state_ == TCPS_LAST_ACK))
		flags |= TH_FIN;

	return (flags);
}

/*
 * see if we should send a segment, and if so, send it
 * (may be ACK or data)
 *	'maxseq_' is called 'snd_max' in "real" TCP
 *	and is the largest seq number we've sent
 *
 * maxseg_, largest seq# we've sent (snd_max)
 * flags_, flags regarding our internal state (t_state)
 * pflags, a local used to build up the tcp header flags (flags)
 * curseq_, is the highest sequence number given to us by "application"
 * highest_ack_, the highest ACK we've seen for our data (snd_una)
 * seqno, the next seq# we're going to send (snd_nxt), this will
 *	update t_seqno_ (the last thing we sent)
 */
void FullTcpAgent::output(int seqno, int reason)
{
	int datalen = min((curseq_ + iss_) - seqno, maxseg_);
	int pflags = outflags();
	int emptying_buffer = ((seqno + datalen) >= (curseq_ + iss_));
	int is_retransmit = (seqno <= maxseq_);
	int idle = (highest_ack_ == maxseq_);

	/* turn off FIN if there's really more to send */
	if (!emptying_buffer)
		pflags &= ~TH_FIN;

	if ((seqno + datalen) > (highest_ack_ + (window() * maxseg_)))
		datalen = 0;

	/* sender SWS avoidance (Nagle) */

	if (datalen > 0) {
		// if full-sized segment, ok
		if (datalen == maxseg_)
			goto send;
		// if Nagle disabled and buffer clearing, ok
		if ((idle || nodelay_)  && emptying_buffer)
			goto send;
		// if a retransmission
		if (is_retransmit)
			goto send;
		// if big "enough", ok...
		//	(this is not a likely case, and would
		//	only happen for tiny windows)
		if (datalen >= ((wnd_ * maxseg_) / 2.0))
			goto send;
	}

	if (need_send())
		goto send;

	/*
	 * send now if a SYN or special flag "TF_ACKNOW" is set.
	 * TF_ACKNOW can be set during connection establishment and
	 * to generate acks for out-of-order data
	 */
	if ((flags_ & TF_ACKNOW) || (pflags & TH_SYN))
		goto send;

	/* if we owe a FIN or are retransmitting a FIN, ok */
	if ((pflags & TH_FIN) &&
	    (((flags_ & TF_SENTFIN) == 0) || is_retransmit))
		goto send;

	return;		// no reason to send now

send:
	Packet* p = allocpkt();
	double now = Scheduler::instance().clock();
    hdr_tcp *tcph = (hdr_tcp*)p->access(off_tcp_);
    hdr_cmn *th = (hdr_cmn*)p->access(off_cmn_);

	if (datalen > 0 && emptying_buffer)
		pflags |= TH_PUSH;

	/*
	 * fill in packet fields.  Agent::allocpkt()
	 * has already filled most of the network layer
	 * fields for us.   So fill in tcp hdr and adjust
	 * the packet size.
	 */
	tcph->seqno() = seqno;
	tcph->flags() = pflags;
	tcph->hlen() = headersize();
	tcph->ts() = now;
	tcph->ackno() = rcv_nxt_; // what we want next
	last_ack_sent_ = tcph->ackno();
    /* Open issue:  should tcph->reason map to pkt->flags_ as in ns-1?? */
	tcph->reason() |= reason;
	th->size() = datalen + headersize();

	send(p, 0); 	// send it
	flags_ &= ~(TF_ACKNOW|TF_DELACK);

	if (pflags & TH_FIN)
		flags_ |= TF_SENTFIN;
	t_seqno_ += datalen;		// update snd_nxt (t_seqno_)
	if (t_seqno_ > maxseq_) {
		maxseq_ = t_seqno_; 	// largest seq# we've sent
		/*
		 * Time this transmission if not a retransmission and
		 * not currently timing anything.
		 */
		if (rtt_active_ == FALSE) {
			rtt_active_ = TRUE;	// set timer
			rtt_seq_ = seqno; 	// timed seq #
		}
	}
	/*
	 * Set retransmit timer if not currently set,
	 * and not doing an ack or a keep-alive probe.
	 * Initial value for retransmit timer is smoothed
	 * round-trip time + 2 * round-trip time variance.
	 * Future values are rtt + 4 * rttvar.
	 */
	if (!(rtx_timer_.status() == TIMER_PENDING) && ((seqno + datalen) != highest_ack_)) {
		set_rtx_timer();  // no timer pending, schedule one
	}
	return;
}

/*
 * Try to send as much data as the window will allow.  The link layer will 
 * do the buffering; we ask the application layer for the size of the packets.
 */
void FullTcpAgent::send_much(int force, int reason, int maxburst)
{

	/*
	 * highest_ack is essentially "snd_una" in real TCP
	 *
	 * loop while we are in-window (seqno <= (highest_ack + win))
	 * and there is something to send (t_seqno_ < curseq_+iss_)
	 */
	int win = window() * maxseg_;	// window() in pkts
	int npackets = 0;
	int topwin = min(highest_ack_ + win, curseq_ + iss_);


	if (!force && (delsnd_timer_.status() == TIMER_PENDING))
        return;

	while (force || (t_seqno_ < topwin)) {
		if (overhead_ != 0 && !(delsnd_timer_.status() == TIMER_PENDING)) {
			delsnd_timer_.resched(Random::uniform(overhead_));
			return;
		}
		output(t_seqno_, reason);	// updates seqno for us
		force = 0;
		npackets++;

		if (maxburst && npackets == maxburst)
			break;
	}
	return;
}

void FullTcpAgent::cancel_rtx_timeout()
{
	if (rtx_timer_.status() == TIMER_PENDING) {
		rtx_timer_.cancel();
	}
}

/*
 * Process an ACK
 *	this version of the routine doesn't necessarily
 *	require the ack to be one which advances the ack number
 *
 * if this ACKs a rtt estimate
 *	indicate we are not timing
 *	reset the exponential timer backoff (gamma)
 * update rtt estimate
 * cancel retrans timer if everything is sent and ACK'd, else set it
 * advance the ack number if appropriate
 * update segment to send next if appropriate
 */
void FullTcpAgent::newack(Packet* pkt)
{
    hdr_tcp *tcph = (hdr_tcp*)pkt->access(off_tcp_);

	double now = Scheduler::instance().clock();
	register int ackno = tcph->ackno();

	// we were timing the segment and we
	// got an ACK for it
	if (rtt_active_ && ackno >= rtt_seq_) {
		/* got a rtt sample */
		rtt_active_ = FALSE;	// no longer timing
		t_backoff_ = 1;		// stop exp backoff
	}

	/* always with timestamp option */
	double tao = now - tcph->ts();
	rtt_update(tao);

	if (ackno == maxseq_)
		cancel_rtx_timeout();
	else {
		if (ackno > highest_ack_) {
			set_rtx_timer();
		}
	}

	// advance the ack number if this is for new data
	if (ackno > highest_ack_)
		highest_ack_ = ackno;
	// set up the next packet to send
	if (t_seqno_ < highest_ack_)
		t_seqno_ = highest_ack_;	// thing to send next
}

/*
 * this is the simulated form of the header prediction
 * predicate
 */
int FullTcpAgent::predict_ok(Packet* pkt)
{
    hdr_tcp *tcph = (hdr_tcp*)pkt->access(off_tcp_);

	int p1 = (state_ == TCPS_ESTABLISHED);		// ready
	int p2 = (tcph->flags() == TH_ACK);		// is an ACK
	int p3 = (tcph->seqno() == rcv_nxt_);		// in-order data
	int p4 = (t_seqno_ == maxseq_);			// not re-xmit
	int p5 = (((hdr_flags*)pkt->access(off_flags_))->ecn_ == 0);	// no ECN

	return (p1 && p2 && p3 && p4 && p5);
}

/*
 * fast_retransmit using the given seqno
 *	perform a fast retransmit
 *	kludge t_seqno_ (snd_nxt) so we do the
 *	retransmit then continue from where we were
 */

void FullTcpAgent::fast_retransmit(int seq)
{
	int onxt = t_seqno_;		// output() changes t_seqno_
	recover_ = maxseq_;		// keep a copy of highest sent
	recover_cause_ = REASON_DUPACK;	// why we started this recovery period
	output(seq, REASON_DUPACK);	// send one pkt
	t_seqno_ = onxt;
}

/*
 * real tcp determines if the remote
 * side should receive a window update/ACK from us, and often
 * results in sending an update every 2 segments, thereby
 * giving the familiar 2-packets-per-ack behavior of TCP.
 * Here, we don't advertise any windows, so we just see if
 * there's at least 'segs_per_ack_' pkts not yet acked
 */

int FullTcpAgent::need_send()
{
	return ((rcv_nxt_ - last_ack_sent_) >= (segs_per_ack_ * maxseg_));
}

/*
 * deal with timers going off.
 * 2 types for now:
 *	retransmission timer (TCP_TIMER_RTX)
 *	delayed send (randomization) timer (TCP_TIMER_DELSND)
 *
 * real TCP initializes the RTO as 6 sec
 *	(A + 2D, where A=0, D=3), [Stevens p. 305]
 * and thereafter uses
 *	(A + 4D, where A and D are dynamic estimates)
 *
 * note that in the simulator t_srtt_, t_rttvar_ and t_rtt_
 * are all measured in 'tcp_tick_'-second units
 */
void FullTcpAgent::timeout(int tno)
{

        /* retransmit timer */
        if (tno == TCP_TIMER_RTX) {
		recover_ = maxseq_;
		recover_cause_ = REASON_TIMEOUT;
                closecwnd(0);
                reset_rtx_timer(1);
		t_seqno_ = highest_ack_;
		dupacks_ = 0;
                send_much(0, PF_TIMEOUT);
        } else if (tno == TCP_TIMER_DELSND) {
                /*
                 * delayed-send timer, with random overhead
                 * to avoid phase effects
                 */
                send_much(1, PF_TIMEOUT);
        } else if (tno == TCP_TIMER_DELACK) {
		if (flags_ & TF_DELACK) {
			flags_ &= ~TF_DELACK;
			flags_ |= TF_ACKNOW;
			send_much(1, REASON_NORMAL, 0);
		}
		delack_timer_.resched(delack_interval_);
	} else {
		double now = Scheduler::instance().clock();
		fprintf(stderr, "%f: (%s) UNKNOWN TIMEOUT %d\n",
			now, name(), tno);
	}
}

/*
 * main reception path - 
 * called from the agent that handles the data path below in its muxing mode
 * advance() is called when connection is established with size sent from
 * user/application agent
 */
void FullTcpAgent::recv(Packet *pkt, Handler*)
{
    hdr_tcp *tcph = (hdr_tcp*)pkt->access(off_tcp_);
    hdr_cmn *th = (hdr_cmn*)pkt->access(off_cmn_);

	int needoutput = 0;
	int ourfinisacked = 0;
	int todrop = 0;
	int dupseg = FALSE;

#ifdef notdef
	if (trace_)
		plot();
#endif

	//
	// if no delayed-ACK timer is set, set one
	// they are set to fire every 'interval_' secs, starting
	// at time t0 = (0.0 + k * interval_) for some k such
	// that t0 > now
	//
	if (!(delack_timer_.status() == TIMER_PENDING)) {
		double now = Scheduler::instance().clock();
		int last = int(now / delack_interval_);
		delack_timer_.resched(delack_interval_ * (last + 1.0) - now);
	}

	int datalen = th->size() - tcph->hlen();
	int ackno = tcph->ackno();	// ack # from packet

	// note predict_ok() false if PUSH bit on
	if (predict_ok(pkt)) {
		if (datalen == 0) {
			// check for a received pure ACK and that
			// our own cwnd doesn't limit us
			if (ackno > highest_ack_ && ackno <= maxseq_
				&& cwnd_ >= wnd_) {
				newack(pkt);
				opencwnd();
				if ((curseq_ + iss_) > highest_ack_) {
					/* more to send */
					send_much(0, REASON_NORMAL, 0);
				}
				Packet::free(pkt);
				return;
			}
		} else if (ackno == highest_ack_ && rq_.empty()) {
			// check for pure incoming segment
			// the next data segment we're awaiting, and
			// that there's nothing sitting in the reassem-
			// bly queue
			// 	give to "application" here
			//	note: DELACK is inspected only by
			//	tcp_fasttimo() in real tcp.  Every 200 ms
			//	this routine scans all tcpcb's looking for
			//	DELACK segments and when it finds them
			//	changes DELACK to ACKNOW and calls tcp_output()
			rcv_nxt_ += datalen;
			flags_ |= TF_DELACK;
			//
			// special code here to simulate the operation
			// of a receiver who always consumes data,
			// resulting in a call to tcp_output
			Packet::free(pkt);
			if (need_send())
				send_much(1, REASON_NORMAL, 0);
			return;
		}
	}
	int tiflags = tcph->flags() ; // tcp flags from packet

	switch (state_) {
	case TCPS_LISTEN:	/* awaiting peer's SYN */
		if (tiflags & TH_ACK) {
			double now = Scheduler::instance().clock();
			// ACK shouldn't be on here
			fprintf(stderr,
			    "%f: FullTcpAgent::recv(%s): got ACK(%d) while in LISTEN\n",
				now, name(), ackno);
			goto drop;
		}
		if ((tiflags & TH_SYN) == 0) {
			double now = Scheduler::instance().clock();
			// we're looking for a SYN in return
			fprintf(stderr,
			    "%f: FullTcpAgent::recv(%s): got a non-SYN while in LISTEN\n",
				now, name());
			goto drop;
		}
		flags_ |= TF_ACKNOW;
		state_ = TCPS_SYN_RECEIVED;
		rcv_nxt_ = tcph->seqno();
		t_seqno_ = iss_;
		goto step6;
	case TCPS_SYN_SENT:	/* we sent SYN, expecting SYN+ACK */
		if ((tiflags & TH_ACK) && (ackno > maxseq_)) {
			double now = Scheduler::instance().clock();
			// not an ACK for our SYN, discard
			fprintf(stderr,
			    "%f: FullTcpAgent::recv(%s): bad ACK (%d) for our SYN(%d)\n",
			        now, name(), int(ackno), int(maxseq_));
			goto drop;
		}
		if ((tiflags & TH_SYN) == 0) {
			double now = Scheduler::instance().clock();
			// we're looking for a SYN in return
			fprintf(stderr,
			    "%f: FullTcpAgent::recv(%s): no SYN for our SYN(%d)\n",
			        now, name(), int(maxseq_));
			goto drop;
		}
		rcv_nxt_ = tcph->seqno();	// initial expected seq#
		cancel_rtx_timeout();	// cancel timer on our 1st SYN
		flags_ |= TF_ACKNOW;	// ACK peer's SYN
		if (tiflags & TH_ACK) {
			// got SYN+ACK (what we're expecting)
			// set up to ACK peer's SYN+ACK
			newack(pkt);
			state_ = TCPS_ESTABLISHED;
		} else {
			// simultaneous active opens
			state_ = TCPS_SYN_RECEIVED;
		}
		goto step6;
	}

	// check for redundant data at head/tail of segment
	//	note that the 4.4bsd [Net/3] code has
	//	a bug here which can cause us to ignore the
	//	perfectly good ACKs on duplicate segments.  The
	//	fix is described in (Stevens, Vol2, p. 959-960).
	//	This code is based on that correction.
	//
	// In addition, it has a modification so that duplicate segments
	// with dup acks don't trigger a fast retransmit when dupseg_fix_
	// is enabled.
	//
	todrop = rcv_nxt_ - tcph->seqno();  // how much overlap?
	if (todrop > 0) {
		// segment is something we've seen (perhaps partially)
		if (tiflags & TH_SYN)
			tiflags &= ~TH_SYN;
		if (todrop > datalen ||
		    (todrop == datalen && ((tiflags & TH_FIN) == 0))) {
			/*
			 * Any valid FIN must be to the left of the window.
			 * At this point the FIN must be a duplicate or out
			 * of sequence; drop it.
			 */

			tiflags &= ~TH_FIN;

			/*
			 * Send an ACK to resynchronize and drop any data.
			 * But keep on processing for RST or ACK.
			 */

			flags_ |= TF_ACKNOW;
			todrop = datalen;
			dupseg = TRUE;
		}
		tcph->seqno() += todrop;
		datalen -= todrop;
	}

	if (tiflags & TH_SYN) {
		double now = Scheduler::instance().clock();
		fprintf(stderr,
		    "%f: FullTcpAgent::recv(%s) received unexpected SYN (state:%d)\n",
		        now, name(), state_);
		goto drop;
	}

	if ((tiflags & TH_ACK) == 0) {
		double now = Scheduler::instance().clock();
		fprintf(stderr, "%f: FullTcpAgent::recv(%s) got packet lacking ACK (seq %d)\n",
			now, name(), tcph->seqno());
		goto drop;
	}

	/*
	 * ACK processing
	 */

	switch (state_) {
	case TCPS_SYN_RECEIVED:	/* got ACK for our SYN+ACK */
		if (ackno < highest_ack_ || ackno > maxseq_) {
			// not in useful range
			goto drop;
		}
		state_ = TCPS_ESTABLISHED;
		/* fall into ... */

        /*
         * In ESTABLISHED state: drop duplicate ACKs; ACK out of range
         * ACKs.  If the ack is in the range
         *      tp->snd_una < ti->ti_ack <= tp->snd_max
         * then advance tp->snd_una to ti->ti_ack and drop
         * data from the retransmission queue.
	 *
	 * note that states CLOSE_WAIT and TIME_WAIT aren't used
	 * in the simulator
         */

        case TCPS_ESTABLISHED:
        case TCPS_FIN_WAIT_1:
        case TCPS_FIN_WAIT_2:
        case TCPS_CLOSING:
        case TCPS_LAST_ACK:

		// look for dup ACKs (dup ack numbers, no data)
		//
		// do fast retransmit/recovery if at/past thresh
		if (ackno <= highest_ack_) {
			// an ACK which doesn't advance highest_ack_
			if (datalen == 0 && (!dupseg_fix_ || !dupseg)) {
                                /*
                                 * If we have outstanding data
                                 * this is a completely
                                 * duplicate ack,
                                 * the ack is the biggest we've
                                 * seen and we've seen exactly our rexmt
                                 * threshhold of them, assume a packet
                                 * has been dropped and retransmit it.
                                 *
                                 * We know we're losing at the current
                                 * window size so do congestion avoidance.
                                 *
                                 * Dup acks mean that packets have left the
                                 * network (they're now cached at the receiver)
                                 * so bump cwnd by the amount in the receiver
                                 * to keep a constant cwnd packets in the
                                 * network.
                                 */

				if (!(rtx_timer_.status() == TIMER_PENDING) ||
				    ackno != highest_ack_) {
					// not timed, or re-ordered ACK
					dupacks_ = 0;
				} else if (++dupacks_ == tcprexmtthresh_) {
					// possibly trigger fast retransmit
					if (!bug_fix_ ||
					   (highest_ack_ > recover_) ||
					   (recover_cause_ != REASON_TIMEOUT)) {
						closecwnd(1);
						cancel_rtx_timeout();
						rtt_active_ = FALSE;
						fast_retransmit(ackno);
						// we measure cwnd in packets,
						// so don't scale by maxseg_
						// as real TCP does
						cwnd_ = ssthresh_ + dupacks_;
					}
					goto drop;
				} else if (dupacks_ > tcprexmtthresh_) {
					// we just measure cwnd in packets,
					// so don't scale by maxset_ as real
					// tcp does
					cwnd_++;	// fast recovery
					send_much(0, REASON_NORMAL, 0);
					goto drop;
				}
			} else {
				// non-zero length segment
				// (or window changed in real TCP).
				if (dupack_reset_)
					dupacks_ = 0;
			}
			break;	/* take us to "step6" */
		}

		/*
		 * we've finished the fast retransmit/recovery period
		 * (i.e. received an ACK which advances highest_ack_)
		 */

                /*
                 * If the congestion window was inflated to account
                 * for the other side's cached packets, retract it.
                 */

		if (dupacks_ > tcprexmtthresh_ && cwnd_ > ssthresh_) {
			// this is where we can get frozen due
			// to multiple drops in the same window of
			// data
			cwnd_ = ssthresh_;
		}
		dupacks_ = 0;
		if (ackno > maxseq_) {
			double now = Scheduler::instance().clock();
			// ack more than we sent(!?)
			fprintf(stderr,
			    "%f: FullTcpAgent::recv(%s) too-big ACK (ack: %d, maxseq:%d)\n",
				now, name(), int(ackno), int(maxseq_));
			goto dropafterack;
		}

                /*
                 * If we have a timestamp reply, update smoothed
                 * round trip time.  If no timestamp is present but
                 * transmit timer is running and timed sequence
                 * number was acked, update smoothed round trip time.
                 * Since we now have an rtt measurement, cancel the
                 * timer backoff (cf., Phil Karn's retransmit alg.).
                 * Recompute the initial retransmit timer.
		 *
                 * If all outstanding data is acked, stop retransmit
                 * If there is more data to be acked, restart retransmit
                 * timer, using current (possibly backed-off) value.
                 */
		newack(pkt);
		if (ackno == maxseq_)
			needoutput = 1;
		opencwnd();
		if (ackno >= (curseq_ + iss_))
			ourfinisacked = 1;
		else
			ourfinisacked = 0;
		// additional processing when we're in special states

		switch (state_) {
                /*
                 * In FIN_WAIT_1 STATE in addition to the processing
                 * for the ESTABLISHED state if our FIN is now acknowledged
                 * then enter FIN_WAIT_2.
                 */
		case TCPS_FIN_WAIT_1:	/* doing active close */
			if (ourfinisacked)
				state_ = TCPS_FIN_WAIT_2;
			break;

                /*
                 * In CLOSING STATE in addition to the processing for
                 * the ESTABLISHED state if the ACK acknowledges our FIN
                 * then enter the TIME-WAIT state, otherwise ignore
                 * the segment.
                 */
		case TCPS_CLOSING:	/* simultaneous active close */;
			if (ourfinisacked)
				state_ = TCPS_CLOSED;
			break;
                /*
                 * In LAST_ACK, we may still be waiting for data to drain
                 * and/or to be acked, as well as for the ack of our FIN.
                 * If our FIN is now acknowledged,
                 * enter the closed state and return.
                 */
		case TCPS_LAST_ACK:	/* passive close */
			if (ourfinisacked)
				goto drop;

		/* no case for TIME_WAIT in simulator */
		} // inner switch
	} // outer switch

step6:
	/* real TCP handles window updates and URG data here */
/* dodata: this label is in the "real" code.. here only for reference */
	/*
	 * DATA processing
	 */

	if (datalen > 0 || (tiflags & TH_FIN)) {
		// see the "TCP_REASS" macro for this code
		if (tcph->seqno() == rcv_nxt_ && rq_.empty()) {
			// got the in-order packet we were looking
			// for, nobody is in the reassembly queue,
			// so this is the common case...
			// note: in "real" TCP we must also be in
			// ESTABLISHED state to come here, because
			// data arriving before ESTABLISHED is
			// queued in the reassembly queue.  Since we
			// don't really have a process anyhow, just
			// accept the data here as-is (i.e. don't
			// require being in ESTABLISHED state)
			tiflags &= TH_FIN;
			flags_ |= TF_DELACK;
			rcv_nxt_ += datalen;
			// give to "application" here
			if (need_send())
				send_much(1, REASON_NORMAL, 0);
		} else {
			// not the one we want next (or it
			// is but there's stuff on the reass queue);
			// do whatever we need to do for out-of-order
			// segments or hole-fills.  Also,
			// send an ACK to the other side right now.
			rq_.add(pkt);
			flags_ |= TF_ACKNOW;
		}
	} else {
		// pure ack, not a FIN
		tiflags &= ~TH_FIN;
	}

	/*
	 * if FIN is received, ACK the FIN
	 * (let user know if we could do so)
	 */

	if (tiflags & TH_FIN) {
		flags_ |= TF_ACKNOW;
		rq_.clear();	// other side shutting down
		switch (state_) {
                /*
                 * In SYN_RECEIVED and ESTABLISHED STATES
                 * enter the CLOSE_WAIT state.
		 * (in the simulator, go to LAST_ACK)
		 * (passive close)
                 */
                case TCPS_SYN_RECEIVED:
                case TCPS_ESTABLISHED:
                        state_ = TCPS_LAST_ACK;
                        break;

                /*
                 * If still in FIN_WAIT_1 STATE FIN has not been acked so
                 * enter the CLOSING state.
		 * (simultaneous close)
                 */
                case TCPS_FIN_WAIT_1:
                        state_ = TCPS_CLOSING;
                        break;
                /*
                 * In FIN_WAIT_2 state enter the TIME_WAIT state,
                 * starting the time-wait timer, turning off the other
                 * standard timers.
		 * (in the simulator, just go to CLOSED)
		 * (active close)
                 */
                case TCPS_FIN_WAIT_2:
                        state_ = TCPS_CLOSED;
			cancel_rtx_timeout();
                        break;
		}
	}

	if (needoutput || (flags_ & TF_ACKNOW))
		send_much(1, REASON_NORMAL, 0);
	else if ((curseq_ + iss_) > highest_ack_)
		send_much(0, REASON_NORMAL, 0);
	Packet::free(pkt);
	return;

dropafterack:
	flags_ |= TF_ACKNOW;
	send_much(1, REASON_NORMAL, 0);
drop:
   	Packet::free(pkt);
	return;
}

/*
 * Set retransmit timer.  By calling resched(), we handle the 
 * case in which the timer is already running.
 */
void FullTcpAgent::set_rtx_timer()
{
    rtx_timer_.resched(rtt_timeout());
}

void FullTcpAgent::reset_rtx_timer(int /* mild */)
{
	// cancel old timer, set a new one
        set_rtx_timer();	// set new timer
        rtt_backoff();		// double current timeout
        rtt_active_ = FALSE;
}


/*
 * do an active open
 * (in real TCP, see tcp_usrreq, case PRU_CONNECT)
 */
void FullTcpAgent::connect()
{
	state_ = TCPS_SYN_SENT;	// sending a SYN now

	if (!data_on_syn_) {
		// force no data in this segment
		int cur = curseq_;
		curseq_ = iss_;
		output(iss_, REASON_NORMAL);
		curseq_ = cur;
		return;
	}
	output(iss_, REASON_NORMAL);
	return;
}

/*
 * be a passive opener
 * (in real TCP, see tcp_usrreq, case PRU_LISTEN)
 * (for simulation, make this peer's ptype ACKs)
 */
void FullTcpAgent::listen()
{
	state_ = TCPS_LISTEN;
	type_ = PT_ACK;	// instead of PT_TCP
}

/*
 * called when user/application performs 'close'
 */

void FullTcpAgent::usrclosed()
{

	switch (state_) {
	case TCPS_CLOSED:
	case TCPS_LISTEN:
	case TCPS_SYN_SENT:
		state_ = TCPS_CLOSED;
		break;
	case TCPS_SYN_RECEIVED:
	case TCPS_ESTABLISHED:
		state_ = TCPS_FIN_WAIT_1;
		send_much(1, REASON_NORMAL, 0);
		break;
	}
	return;
}

int FullTcpAgent::command(int argc, const char*const* argv)
{
	// would like to have some "connect" primitive
	// here, but the problem is that we get called before
	// the simulation is running and we want to send a SYN.
	// Because no routing exists yet, this fails.
	// Instead, see code in advance() above.
	//
	// listen can happen any time because it just changes state_
	//
	// close is designed to happen at some point after the
	// simulation is running (using an ns 'at' command)

	if (argc == 2) {
		if (strcmp(argv[1], "listen") == 0) {
			// just a state transition
			listen();
			return (TCL_OK);
		}
		if (strcmp(argv[1], "close") == 0) {
			usrclosed();
			return (TCL_OK);
		}
	}
	if (argc == 3) {
		if (strcmp(argv[1], "advance") == 0) {
			advance(atoi(argv[2]));
			return (TCL_OK);
		}
	}
	return (TcpAgent::command(argc, argv));
}
/*
 * clear out reassembly queue
 */
void ReassemblyQueue::clear()
{
	seginfo* p;
	for (p = head_; p != NULL; p = p->next_)
		delete p;
	head_ = tail_ = NULL;
	return;
}

/*
 * add a packet to the reassembly queue..
 * will update FullTcpAgent::rcv_nxt_ by way of the
 * ReassemblyQueue::rcv_nxt_ integer reference (an alias)
 */
void ReassemblyQueue::add(Packet* pkt)
{
    hdr_tcp *tcph = (hdr_tcp*)pkt->access(off_tcp_);
    hdr_cmn *th = (hdr_cmn*)pkt->access(off_cmn_);

	int start = tcph->seqno();
	int end = start + th->size() - tcph->hlen();
	seginfo *q, *p, *n;

	if (head_ == NULL) {
		// nobody there, just insert
		tail_ = head_ = new seginfo;
		head_->prev_ = NULL;
		head_->next_ = NULL;
		head_->startseq_ = start;
		head_->endseq_ = end;
	} else {
		p = NULL;
		n = new seginfo;
		n->startseq_ = start;
		n->endseq_ = end;
		if (tail_->endseq_ <= start) {
			// common case of end of reass queue
			p = tail_;
			goto endfast;
		}

		q = head_;
		// look for the segment after this one
		while (q != NULL && (end > q->startseq_))
			q = q->next_;
		// set p to the segment before this one
		if (q == NULL)
			p = tail_;
		else
			p = q->prev_;

		if (p == NULL) {
			// insert at head
			n->next_ = head_;
			n->prev_ = NULL;
			head_->prev_ = n;
			head_ = n;
		} else {
endfast:
			// insert in the middle or end
			n->next_ = p->next_;
			p->next_ = n;
			n->prev_ = p;
			if (p == tail_)
				tail_ = n;
		}
	}
	//
	// look for a sequence of in-order segments and
	// set rcv_nxt if we can
	//

	if (head_->startseq_ > rcv_nxt_)
		return;	// still awaiting a hole-fill

	p = head_;
	while (p != NULL) {
		// update rcv_nxt_ to highest in-seq thing
		// and delete the entry from the reass queue
		rcv_nxt_ = p->endseq_;
		q = p;
		if (q->prev_)
			q->prev_->next_ = q->next_;
		else
			head_ = q->next_;
		if (q->next_)
			q->next_->prev_ = q->prev_;
		else
			tail_ = q->prev_;
		if (q->next_ && (q->endseq_ < q->next_->startseq_)) {
			delete q;
			break;		// only the in-seq stuff
		}
		p = p->next_;
		delete q;
	}

	return;
}

void DelAckTimer::expire(Event *e) {
                if (a_->flags_ & TF_DELACK) {  
                        a_->flags_ &= ~TF_DELACK;
                        a_->flags_ |= TF_ACKNOW;
                        a_->send_much(1, REASON_NORMAL, 0);
                } 
                a_->delack_timer_.resched(a_->delack_interval_);
}

void DelSndTimer::expire(Event *e) {
                a_->send_much(1, PF_TIMEOUT);
}
void RtxTimer::expire(Event *e) {
                a_->recover_ = a_->maxseq_;
                a_->recover_cause_ = REASON_TIMEOUT;
                a_->closecwnd(0);
                a_->reset_rtx_timer(1);
                a_->t_seqno_ = a_->highest_ack_;
                a_->dupacks_ = 0;
                a_->send_much(0, PF_TIMEOUT);

}

