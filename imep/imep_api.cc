/* 
   imep_api.cc
   $Id: imep_api.cc,v 1.1 1999/08/03 04:12:36 yaxu Exp $
   */

#include <imep/imep.h>

#define CURRENT_TIME	Scheduler::instance().clock()

static const int verbose = 0;

// ======================================================================
// ======================================================================
// The IMEP API

void
imepAgent::imepRegister(rtAgent *rt)
{
	rtagent_ = rt;
	assert(rtagent_);
}

void
imepAgent::imepGetLinkStatus(nsaddr_t index, u_int32_t &status)
{
	imepLink *l = findLink(index);

	if(l == 0)
	  {
	    status = LINK_DOWN;
	  }
	else
	  {
	    status = l->status();
	  }
}

void
imepAgent::imepSetLinkInStatus(nsaddr_t index)
{
  imepLink *l = findLink(index);

  if(l == 0) 
    {
      l = new imepLink(index);
      LIST_INSERT_HEAD(&imepLinkHead, l, link);
      l->status() = LINK_DOWN;
    }
	
  if (LINK_DOWN == l->status()) 
    { // the link is an in adjacency
      if (verbose) trace("T %.9f _%d_ new link to %d",
			 CURRENT_TIME, ipaddr, index);
      l->lastSeq() = 0; // expect 1 next  (not needed XXX)
      l->lastSeqValid() = 0;
      l->out_expire() = -1.0;

      stats.new_in_adjacency++;
      // using the imep-spec-01 logic (sec 3.4.2)
      // send a hello immeadiately after learning of a new
      // adjacency.
      sendHello(index);
      // the draft sez ``immeadiate'' --- I'll allow the
      // time for aggregation.  we could cancel the controlTimer
      // here and launch the packet immeadiately if we wanted. -dam
    }

  u_int ostatus = l->status();
  l->status() |= LINK_IN;
  if (ostatus != l->status() && l->status() == LINK_BI)
    {
      rtagent_->rtNotifyLinkUP(index);
      stats.new_neighbor++;
    }    

  l->in_expire() = CURRENT_TIME + MAX_BEACON_TIME;
}

void
imepAgent::imepSetLinkOutStatus(nsaddr_t index)
{
	imepLink *l = findLink(index);

	// how could we know that someone hears us w/o us
	// first receiving a packet from them (which would create
	// in status and create a link record for them)? -dam
	assert(l);

	u_int ostatus = l->status();
	l->status() |= LINK_OUT;
	if (ostatus != l->status() && l->status() == LINK_BI)
	  {
	    rtagent_->rtNotifyLinkUP(index);
	    stats.new_neighbor++;
	  }

	l->out_expire() = CURRENT_TIME + MAX_BEACON_TIME;
}

void
imepAgent::imepSetLinkBiStatus(nsaddr_t index)
{
  imepSetLinkInStatus(index);
  imepSetLinkOutStatus(index);
}

void
imepAgent::imepSetLinkDownStatus(nsaddr_t index)
{
	imepLink *l = findLink(index);

	if(l == 0) {
		return;
	}
	l->status() = LINK_DOWN;
	l->in_expire() = -1.0;
	l->out_expire() = -1.0;

	// clear the resequencing queue for the neighbor
	incomingQ.deleteDst(index);

	// clean this node off the response list of any packets 
	// we're expecting them to ack
	purgeReXmitQ(index);

	// tell the routing layer the link is gone
	rtagent_->rtNotifyLinkDN(index);

	stats.delete_neighbor1++;

	if (verbose) trace("T %.9f _%d_ down link to %d",
			   CURRENT_TIME, ipaddr, index);
}

void
imepAgent::imepPacketUndeliverable(Packet *p)
{
	struct hdr_cmn *cmh = HDR_CMN(p);
	struct hdr_ip *ip = HDR_IP(p);

	if (AF_INET == cmh->addr_type())
	  imepSetLinkDownStatus(cmh->next_hop());

	if (verbose) trace("T %.9f _%d_ undeliverable pkt to %d",
			   CURRENT_TIME, ipaddr, ip->dst());

	rtagent_->rtRoutePacket(p);
}

void
imepAgent::purgeLink()
{
  imepLink *l, *nl;

  for(l = imepLinkHead.lh_first ; l; l = nl) 
    { 
      nl = l->link.le_next;
      
      // Is this a bug?  should save old status now, and then
      // notify rtagent if ostatus == LINK_BI and new status doesn't
      // -dam 8/26/98
      // I don't think it's a problem, since any packet that
      // sets link_out expire time also sets link_in expire time,
      // so a LINK_OUT && !LINK_IN state should never be possible -dam
      
      int ostatus = l->status();
      if (l->in_expire() < CURRENT_TIME) l->status() &= ~LINK_IN;
      if (l->out_expire() < CURRENT_TIME) l->status() &= ~LINK_OUT;
      if (LINK_BI == ostatus && LINK_BI != l->status())
	{
	  imepSetLinkDownStatus(l->index());
	}

      if (LINK_DOWN == l->status())
	{
	  stats.delete_neighbor2++;

	  LIST_REMOVE(l, link);
	  delete l;
	}
    }
}

void
imepAgent::imepGetBiLinks(int*& nblist, int& nbcnt)
{
	imepLink *l;
	int cnt = 0;

	for(l = imepLinkHead.lh_first; l; l = l->link.le_next) {
		if(l->status() == LINK_BI) cnt++;
	}
	nbcnt = cnt;

	if(cnt == 0) return;
	// no neighbors

	nblist = new int[cnt];
	cnt = 0;
	for(l = imepLinkHead.lh_first; l; l = l->link.le_next) {
		if(l->status() == LINK_BI) {
			nblist[cnt] = l->index();
			cnt++;
		}
	}
}
