/* 
   mac-802_3.cc
   $Id: mac-802_3.cc,v 1.10 2000/12/20 10:13:57 alefiyah Exp $
   */
#include <packet.h>
#include <random.h>
#include <arp.h>
#include <ll.h> 
#include <mac-802_3.h>

//#define MAC_DEBUG

#ifndef MAC_DEBUG
#define FPRINTF(s, f, t, index, func) do {} while (0)
#else
   static double xtime= 0.0;
#  define FPRINTF(s, f, t, index, func) \
         do { fprintf(s, f, t, index, func); xtime= t; } while (0)
#endif MAC_DEBUG


inline void MacHandler::cancel() {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), mac->index_, __PRETTY_FUNCTION__);
	Scheduler& s = Scheduler::instance();
	assert(busy_);
	s.cancel(&intr);
	// No need to free the event intr since it's statically allocated.
	busy_ = 0;
}

inline void Mac8023HandlerSend::cancel() {
	assert(busy_);
	Scheduler &s= Scheduler::instance();
	s.cancel(&intr);
	busy_= 0;
	p_= 0;
}

inline void MacHandlerRecv::cancel() {
	Scheduler& s = Scheduler::instance();
	assert(busy_ && p_);
	s.cancel(&intr);
	busy_ = 0;
	Packet::free(p_);
	p_= 0;
}

inline void MacHandlerRetx::cancel() {
	Scheduler& s = Scheduler::instance();
	assert(busy_ && p_);
	s.cancel(&intr);
}

inline void MacHandlerIFS::cancel() {
	//fprintf (stderr, "cancelled dtime= %.15f\n", intr.time_- Scheduler::instance().clock());
	MacHandler::cancel();
}
static class Mac802_3Class : public TclClass {
public:
	Mac802_3Class() : TclClass("Mac/802_3") {}
	TclObject* create(int, const char*const*) {
		return (new Mac802_3);
	}
} class_mac802_3;

void Mac8023HandlerSend::handle(Event*) {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), mac->index_, __PRETTY_FUNCTION__);
	assert(p_);
	/* Transmission completed successfully */
	busy_ = 0;
	p_= 0;
	mac->mhRetx_.free();
	mac->mhRetx_.reset();
	mac->mhIFS_.schedule(mac->netif_->txtime(int(IEEE_8023_IFS_BITS/8.0)));
}

void Mac8023HandlerSend::schedule(const Packet *p, double t) {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), mac->index_, __PRETTY_FUNCTION__);
	Scheduler& s = Scheduler::instance();
	assert(!busy_);
	s.schedule(this, &intr, t);
	busy_ = 1;
	p_= p;
}

void MacHandlerRecv::handle(Event* ) {
	/* Reception Successful */
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), mac->index_, __PRETTY_FUNCTION__);
	busy_ = 0;
	mac->recv_complete(p_);
	p_= 0;
}

void MacHandlerRecv::schedule(Packet *p, double t) {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), mac->index_, __PRETTY_FUNCTION__);
	Scheduler& s = Scheduler::instance();
	assert(p && !busy_);
	s.schedule(this, &intr, t);
	busy_ = 1;
	p_ = p;
}

bool MacHandlerRetx::schedule(double delta) {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), mac->index_, __PRETTY_FUNCTION__);
	Scheduler& s = Scheduler::instance();
	assert(p_ && !busy_);
	int k, r;
	if(try_ < IEEE_8023_ALIMIT) {
		k = min(try_, IEEE_8023_BLIMIT);
		r = Random::integer(1 << k);
		s.schedule(this, &intr, r * mac->netif_->txtime(IEEE_8023_SLOT_BITS/8.0) + delta);
		busy_ = 1;
		return true;
	}
	fprintf(stderr,"try %d",try_);
	return false;
}

void MacHandlerRetx::handle(Event *) {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), mac->index_, __PRETTY_FUNCTION__);
	assert(p_);
	busy_= 0;
	++try_;
	mac->transmit(p_);
}

inline void MacHandlerIFS::schedule(double t) {
		FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), mac->index_, __PRETTY_FUNCTION__);
		assert(!busy_);
		Scheduler &s= Scheduler::instance();
		s.schedule(this, &intr, t);
		busy_= 1;
}
inline void MacHandlerIFS::handle(Event*) { 
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), mac->index_, __PRETTY_FUNCTION__);
	busy_= 0; 
	mac->resume(); 
}


Mac802_3::Mac802_3() : Mac(), 
	mhRecv_(this), mhRetx_(this), mhIFS_(this), mhSend_(this),trace_(0) {
        // Bind mac trace variable 
        bind_bool("trace_",&trace_);
}

void Mac802_3::sendUp(Packet *p, Handler *) {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), index_, __PRETTY_FUNCTION__);
	/* just received the 1st bit of a packet */
	if (state_ != MAC_IDLE && mhIFS_.busy()) {
		// this must mean either that 
		//   a. mhIFS_ is about to expire now - concurrent events ordering - or
		//   b. (txtime + ifs + propdelay) < (propdelay + txtime + ifs) for the prev. packet
		// so we assume that IFS is over and resume
#ifdef MAC_DEBUG
#define EPS 1.0e-15
		if (mhIFS_.expire() - Scheduler::instance().clock() > EPS) {
			fprintf(stderr, "mhIFS_: %.20f, time= %.20f, diff= %e\n",  
				mhIFS_.expire(), Scheduler::instance().clock(),
				mhIFS_.expire() - Scheduler::instance().clock());
			assert(0);
		}
#undef EPS
#endif
		mhIFS_.cancel();
		resume();
	} 
	if(state_ == MAC_IDLE) {
		state_ = MAC_RECV;
		assert(!mhRecv_.busy());
		/* the last bit will arrive in txtime seconds */
		mhRecv_.schedule(p, netif_->txtime(p));
	} else {
		collision(p); //received packet while sending or receiving
	}
}

void Mac802_3::sendDown(Packet *p, Handler *h) {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), index_, __PRETTY_FUNCTION__);
	assert(initialized());
	assert(h);
	assert(netif_->txtime(IEEE_8023_MINFRAME) > 
	       2*netif_->channel()->maxdelay()); /* max prop. delay is limited by specs: 
						    about 25us for 10Mbps
						    and   2.5us for 100Mbps
						 */

	int size= (HDR_CMN(p)->size() += ETHER_HDR_LEN); //XXX also preamble?
	hdr_mac *mh= HDR_MAC(p);
	mh->padding_= 0;
	if (size > IEEE_8023_MAXFRAME) {
		static bool warnedMAX= false;
		if (!warnedMAX) {
			fprintf(stderr, "Mac802_3: frame is too big: %d\n", size);
			warnedMAX= true;
		}
	} else if (size < IEEE_8023_MINFRAME) {
		// pad it to fit MINFRAME
		mh->padding_= IEEE_8023_MINFRAME - size;
		HDR_CMN(p)->size() += mh->padding_;
	}
	callback_ = h;
	mhRetx_.packet(p); //packet's buffered by mhRetx in case of retransmissions

	transmit(p);
}


void Mac802_3::transmit(Packet *p) {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), index_, __PRETTY_FUNCTION__);
	assert(callback_);
	if(mhSend_.packet()) {
		fprintf(stderr, "index: %d\n", index_);
		fprintf(stderr, "Retx Timer: %d\n", mhRetx_.busy());
		fprintf(stderr, "IFS  Timer: %d\n", mhIFS_.busy());
		fprintf(stderr, "Recv Timer: %d\n", mhRecv_.busy());
		fprintf(stderr, "Send Timer: %d\n", mhSend_.busy());
		exit(1);
	}

	/* Perform carrier sense  - if we were sending before, never mind state_ */
	if (mhIFS_.busy() || (state_ != MAC_IDLE)) {
		/* we'll try again when IDLE. It'll happen either when
                   reception completes, or if collision.  Either way,
                   we call resume() */
		return;
	}
	HDR_CMN(p)->direction()= hdr_cmn::DOWN; //down

	double txtime = netif_->txtime(p);
	/* Schedule transmission of the packet's last bit */
	mhSend_.schedule(p, txtime);

	// pass the packet to the PHY: need to send a copy, 
	// because there may be collision and it may be freed
	downtarget_->recv(p->copy()); 

	state_= MAC_SEND;
}

void Mac802_3::collision(Packet *p) {

  // Variables added for trace support.
  // records the last time the packet was seen as well as the UID
        static int last_collision=INVALID_UID;
	static double last_collision_time=INVALID_TIME;

	
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), index_, __PRETTY_FUNCTION__);
	
	// Added for trace support
        // Hack to print only once for the lan ; not for each node in Lan.
	if(trace_){
	  hdr_cmn *th = hdr_cmn::access(p);

	  if (last_collision_time != Scheduler::instance().clock())
	    last_collision = INVALID_UID;

	  if(last_collision != th->uid()){
	    last_collision = th->uid();
	    HDR_CMN(p)->size() -= (ETHER_HDR_LEN + HDR_MAC(p)->padding_);
	    drop_->recv(p);
	    last_collision_time = Scheduler::instance().clock();
	  }
	}
	
	Packet::free(p);
	if (mhIFS_.busy()) mhIFS_.cancel();

	double ifstime= netif_->txtime(int((IEEE_8023_JAMSIZE+IEEE_8023_IFS_BITS)/8.0)); //jam time + ifs
	mhIFS_.schedule(ifstime);

	switch(state_) {
	case MAC_SEND:
		if (mhSend_.busy()) mhSend_.cancel();
		if (!mhRetx_.busy()) {
			/* schedule retransmissions */
			if (!mhRetx_.schedule(ifstime)) {
				p= mhRetx_.packet();
				hdr_cmn *th = hdr_cmn::access(p);
				HDR_CMN(p)->size() -= (ETHER_HDR_LEN + HDR_MAC(p)->padding_);
				fprintf(stderr,"\nBinary Exponential Backoff exceeded backoff limit\nDropping packet %d",th->uid());
                                fflush(stderr);
				// drop(p); // drop if backed off far enough
				Packet::free(p);
				mhRetx_.reset();
			}
		}
		break;
	case MAC_RECV:
		// more than 2 packets collisions possible
		if (mhRecv_.busy()) mhRecv_.cancel();
		break;
	default:
		assert("SHOULD NEVER HAPPEN" == 0);
	}
}

void Mac802_3::recv_complete(Packet *p) {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), index_, __PRETTY_FUNCTION__);
	assert(!mhRecv_.busy());
	assert(!mhSend_.busy());
	hdr_cmn *ch= HDR_CMN(p);
	/* Address Filtering */
	hdr_mac *mh= HDR_MAC(p);
	int dst= mh->macDA();

	if ((dst != BCAST_ADDR) && (dst != index_)) {
		Packet::free(p);
		goto done;
	}
	/* Strip off the mac header and padding if any */
	ch->size() -= (ETHER_HDR_LEN + mh->padding_);

	/* xxx FEC here */
	if( ch->error() ) {
                fprintf(stderr,"\nChecksum error\nDropping packet");
                fflush(stderr);
		// drop(p);
                Packet::free(p);
		goto done;
	}


	/* we could schedule an event to account for mac-delay */
	uptarget_->recv(p, (Handler*) 0);

 done:
	mhIFS_.schedule(netif_->txtime(int(IEEE_8023_IFS_BITS/8.0)));// wait for one IFS, then resume
}

/* we call resume() in these cases:
   - successful transmission
   - whole packet's received
   - collision and backoffLimit's exceeded
   - collision while receiving */
void Mac802_3::resume() {
	FPRINTF(stderr, "%.15f : %d : %s\n", Scheduler::instance().clock(), index_, __PRETTY_FUNCTION__);
	assert(!mhRecv_.busy());
	assert(!mhSend_.busy());
	assert(!mhIFS_.busy());

	state_= MAC_IDLE;

	if (mhRetx_.packet()) {
		if (!mhRetx_.busy()) {
			// we're not backing off and not sensing carrier right now: send
			transmit(mhRetx_.packet());
		}
	} else {
		if (callback_ && !mhRetx_.busy()) {
			//WARNING: calling callback_->handle may change the value of callback_
			Handler* h= callback_; 
			callback_= 0;
			h->handle(0);
		}
	}
}

