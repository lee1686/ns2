// Copyright (c) 2000 by the University of Southern California
// All rights reserved.
//
// Permission to use, copy, modify, and distribute this software and its
// documentation in source and binary forms for non-commercial purposes
// and without fee is hereby granted, provided that the above copyright
// notice appear in all copies and that both the copyright notice and
// this permission notice appear in supporting documentation. and that
// any documentation, advertising materials, and other materials related
// to such distribution and use acknowledge that the software was
// developed by the University of Southern California, Information
// Sciences Institute.  The name of the University may not be used to
// endorse or promote products derived from this software without
// specific prior written permission.
//
// THE UNIVERSITY OF SOUTHERN CALIFORNIA makes no representations about
// the suitability of this software for any purpose.  THIS SOFTWARE IS
// PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
//
// Other copyrights might apply to parts of this software and are so
// noted when applicable.

// smac is designed and developed by Wei Ye (SCADDS/ISI)
// and is ported into ns by Padma Haldar, June'02.

// This module implements Sensor-MAC
//  See http://www.isi.edu/scadds/papers/smac_infocom.pdf for details
//
// It has the following functions.
//  1) Both virtual and physical carrier sense
//  2) RTS/CTS for hidden terminal problem
//  3) Backoff and retry
//  4) Broadcast packets are sent directly without using RTS/CTS/ACK.
//  5) A long unicast message is divided into multiple TOS_MSG (by upper
//     layer). The RTS/CTS reserves the medium for the entire message.
//     ACK is used for each TOS_MSG for immediate error recovery.
//  6) Node goes to sleep when its neighbor is communicating with another
//     node.
//  7) Each node follows a periodic listen/sleep schedule
//  8.1) At bootup time each node listens for a fixed SYNCPERIOD and then
//     tries to send out a sync packet. It suppresses sending out of sync pkt 
//     if it happens to receive a sync pkt from a neighbor and follows the 
//     neighbor's schedule. 
//  8.2) Or a node can choose its own schecule instead of following others, the
//       schedule start time is user configurable
//  9) Neighbor Discovery: in order to prevent that two neighbors can not
//     find each other due to following complete different schedules, each
//     node periodically listen for a whole period of the SYNCPERIOD
//  10) Duty cycle is user configurable

 

#ifndef NS_SMAC
#define NS_SMAC

#include "mac.h"
#include "mac-802_11.h"
#include "cmu-trace.h"
#include "random.h"
#include "timer-handler.h"

/* User-adjustable MAC parameters
 *--------------------------------
 * The default values can be overriden in Each application's Makefile
 * SMAC_MAX_NUM_NEIGHB: maximum number of neighbors.
 * SMAC_MAX_NUM_SCHED: maximum number of different schedules.
 * SMAC_DUTY_CYCLE: duty cycle in percentage. It controls the length of sleep 
 *   interval.
 * SMAC_RETRY_LIMIT: maximum number of RTS retries for sending a single message.
 * SMAC_EXTEND_LIMIT: maximum number of times to extend Tx time when ACK timeout
     happens.
 */

#ifndef SMAC_MAX_NUM_NEIGHBORS
#define SMAC_MAX_NUM_NEIGHBORS 20
#endif

#ifndef SMAC_MAX_NUM_SCHEDULES
#define SMAC_MAX_NUM_SCHEDULES 4
#endif

#ifndef SMAC_DUTY_CYCLE
#define SMAC_DUTY_CYCLE 10
#endif

#ifndef SMAC_RETRY_LIMIT
#define SMAC_RETRY_LIMIT 5
#endif

#ifndef SMAC_EXTEND_LIMIT
#define SMAC_EXTEND_LIMIT 5
#endif


/* Internal MAC parameters
 *--------------------------
 * Do NOT change them unless for tuning S-MAC
 * SYNC_CW: number of slots in the sync contention window, must be 2^n - 1 
 * DATA_CW: number of slots in the data contention window, must be 2^n - 1
 * SYNC_PERIOD: period to send a sync pkt, in cycles.
 * SRCH_CYCLES_LONG: # of SYNC periods during which a node performs a neighbor discovery
 * SRCH_CYCLES_SHORT: if there is no known neighbor, a node need to seach neighbor more aggressively
 */

#define SYNC_CW 31
#define DATA_CW 63
#define SYNCPERIOD 10
#define SYNCPKTTIME 3         // an adhoc value used for now later shld converge with durSyncPkt_

#define SRCH_CYCLES_SHORT 3
#define SRCH_CYCLES_LONG 22


/* Physical layer parameters
 *---------------------------
 * Based on the parameters from PHY_RADIO and RADIO_CONTROL
 * CLOCK_RES: clock resolution in ms. 
 * BANDWIDTH: bandwidth (bit rate) in kbps. Not directly used.
 * PRE_PKT_BYTES: number of extra bytes transmitted before each pkt. It equals
 *   preamble + start symbol + sync bytes.
 * ENCODE_RATIO: output/input ratio of the number of bytes of the encoding
 *  scheme. In Manchester encoding, 1-byte input generates 2-byte output.
 * PROC_DELAY: processing delay of each packet in physical and MAC layer, in ms
 */

#define CLOCKRES 1       // clock resolution is 1ms
#define BANDWIDTH 20      // kbps =>CHANGE BYTE_TX_TIME WHENEVER BANDWIDTH CHANGES
//#define BYTE_TX_TIME 4/10 // 0.4 ms to tx one byte => changes when bandwidth does
#define PRE_PKT_BYTES 5
#define ENCODE_RATIO 2   /* Manchester encoding has 2x overhead */
#define PROC_DELAY 1



// Note everything is in clockticks (CLOCKRES in ms) for tinyOS
// so we need to convert that to sec for ns
#define CLKTICK2SEC(x)  ((x) * (CLOCKRES / 1.0e3))
#define SEC2CLKTICK(x)  ((x) / (CLOCKRES / 1.0e3))


// MAC states
#define SLEEP 0         // radio is turned off, can't Tx or Rx
#define IDLE 1          // radio in Rx mode, and can start Tx
//#define CHOOSE_SCHED 2  // node in boot-up phase, needs to choose a schedule
#define CR_SENSE 2      // medium is free, do it before initiate a Tx
//#define BACKOFF 3       // medium is busy, and cannot Tx
#define WAIT_CTS 3      // sent RTS, waiting for CTS
#define WAIT_DATA 4     // sent CTS, waiting for DATA
#define WAIT_ACK 5      // sent DATA, waiting for ACK
#define WAIT_NEXTFRAG 6 // send one fragment, waiting for next from upper layer


// how to send the pkt: broadcast or unicast
#define BCASTSYNC 0
#define BCASTDATA 1
#define UNICAST 2

// Types of pkt
#define DATA_PKT 0
#define RTS_PKT 1
#define CTS_PKT 2
#define ACK_PKT 3
#define SYNC_PKT 4


// radio states for performance measurement
#define RADIO_SLP 0  // radio off
#define RADIO_IDLE 1 // radio idle
#define RADIO_RX 2   // recv'ing mode
#define RADIO_TX 3   // transmitting mode




/*  sizeof smac datapkt hdr and smac control and sync packets  */
/*  have been hardcoded here to mirror the values in TINY_OS implementation */
/*  The following is the pkt format definitions for tiny_os implementation */
/*  of smac : */

/*  typedef struct MAC_CTRLPKT_VALS{ */
/*  unsigned char length; */
/*  char type; */
/*  short addr; */
/*  unsigned char group; */
/*  short srcAddr; */
/*  unsigned char duration; */
/*  short crc; */
/*  }; */

/*  typedef struct MAC_SYNCPKT_VALS{ */
/*  unsigned char length; */
/*  char type; */
/*  short srcAddr; */
/*  short syncNode; */
/*  unsigned char sleepTime;  // my next sleep time from now */
/*  short crc; */
/*  };  */

/*  struct MSG_VALS{ */
/*  unsigned char length; */
/*  char type; */
/*  short addr; */
/*  unsigned char group; */
/*  short srcAddr; */
/*  unsigned char duration; */
/*  char data[DATA_LENGTH]; */
/*  short crc; */
/*  }; */

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#define SIZEOF_SMAC_DATAPKT 50  // hdr(10) + payload - fixed size pkts
#define SIZEOF_SMAC_CTRLPKT 10
#define SIZEOF_SMAC_SYNCPKT 9  


// Following are the ns definitions of the smac frames
//SYNC PKT 
struct smac_sync_frame { 
  int type; 
  int length; 
  int srcAddr;
  //int dstAddr;
  int syncNode; 
  double sleepTime;  // my next sleep time from now */
  int crc; 
}; 

// RTS, CTS, ACK
struct smac_control_frame {
  int type;
  int length;
  int dstAddr;
  int srcAddr;
  double duration;
  int crc;
};

// DATA 
struct hdr_smac {
  int type;
  int length;
  int dstAddr;
  int srcAddr;
  double duration;
  //char data[DATA_LENGTH];
  int crc;
};

// Used by smac when in sync mode
struct SchedTable { 
  int txSync;  // flag indicating need to send sync 
  int txData;  // flag indicating need to send data 
  int numPeriods; // count for number of periods 
}; 

struct NeighbList { 
  int nodeId; 
  int schedId; 
}; 

class SMAC;

// Timers used in smac
class SmacTimer : public TimerHandler {
 public:
  SmacTimer(SMAC *a) : TimerHandler() {a_ = a; }
  virtual void expire(Event *e) = 0 ;
  int busy() ;
 protected:
  SMAC *a_;
};

// Generic timer used for sync, CTS and ACK timeouts
class SmacGeneTimer : public SmacTimer {
 public:
  SmacGeneTimer(SMAC *a) : SmacTimer(a) {}
  void expire(Event *e);
};

// Receive timer for receiving pkts
class SmacRecvTimer : public SmacTimer {
 public:
  SmacRecvTimer(SMAC *a) : SmacTimer(a) { stime_ = rtime_ = 0; }
  void sched(double duration);
  void expire(Event *e);
  double timeToExpire();
 protected:
  double stime_;
  double rtime_;
};

// Send timer
class SmacSendTimer : public SmacTimer {
 public:
  SmacSendTimer(SMAC *a) : SmacTimer(a) {}
  void expire(Event *e);
};

// Nav- indicating if medium is busy or not
class SmacNavTimer : public SmacTimer {
 public:
  SmacNavTimer(SMAC *a) : SmacTimer(a) {}
  void expire(Event *e);
};

// Neighbor nav - if neighbor is busy or not
// used for data timeout
class SmacNeighNavTimer : public SmacTimer {
 public:
  SmacNeighNavTimer(SMAC *a) : SmacTimer(a) { stime_ = rtime_ = 0; }
  void sched(double duration);
  void expire(Event *e);
  double timeToExpire();
 protected:
  double stime_;
  double rtime_;
};

// carrier sense timer
class SmacCsTimer : public SmacTimer {
 public:
  SmacCsTimer(SMAC *a) : SmacTimer(a) {}
  void expire(Event *e);
  void checkToCancel();
};

// synchronisation timer, regulates the sleep/wakeup cycles
class SmacCounterTimer : public SmacTimer { 
 public:  
  friend class SMAC;
  SmacCounterTimer(SMAC *a, int i) : SmacTimer(a) {index_ = i;}
  void sched(double t);
  void expire(Event *e); 
  double timeToSleep();
 protected:
  int index_;
  int value_;
  int syncTime_;
  int dataTime_;
  int listenTime_;
  int sleepTime_;
  int cycleTime_;
  double tts_;
  double stime_;
}; 


// The smac class
class SMAC : public Mac {
  
  friend class SmacGeneTimer;
  friend class SmacRecvTimer;
  friend class SmacSendTimer;
  friend class SmacNavTimer;
  friend class SmacNeighNavTimer;
  friend class SmacCsTimer; 
  friend class SmacCounterTimer;

 public:
  SMAC(void);
  ~SMAC() { 
    for (int i=0; i< SMAC_MAX_NUM_SCHEDULES; i++) {
      delete mhCounter_[i];
    }
  }
  void recv(Packet *p, Handler *h);

 protected:
  
  // functions for handling timers
  void handleGeneTimer();
  void handleRecvTimer();
  void handleSendTimer();
  void handleNavTimer();
  void handleNeighNavTimer();
  void handleCsTimer();
  //void handleChkSendTimer();
  void handleCounterTimer(int i);

  // Internal MAC parameters
  double slotTime_;
  double slotTime_sec_;
  double difs_;
  double sifs_;
  double eifs_;
  double guardTime_;
  double byte_tx_time_;
  double dutyCycle_;
 
 private:
  // functions for node schedule folowing sleep-wakeup cycles
  void setMySched(Packet *syncpkt);
  void sleep();
  void wakeup();

  // functions for handling incoming packets
  
  void rxMsgDone(Packet* p);
  //void rxFragDone(Packet *p);  no frag for now

  void handleRTS(Packet *p);
  void handleCTS(Packet *p);
  void handleDATA(Packet *p);
  void handleACK(Packet *p);
  void handleSYNC(Packet *p);

  // functions for handling outgoing packets
  
  // check for pending data pkt to be tx'ed
  // when smac is not following SYNC (sleep-wakeup) cycles.
  int checkToSend();               // check if can send, start cs 

  bool chkRadio();         // checks radiostate
  void transmit(Packet *p);         // actually transmits packet

  bool sendMsg(Packet *p, Handler *h);
  bool bcastMsg(Packet *p);
  bool unicastMsg(int n, Packet *p);
  //int sendMoreFrag(Packet *p);
  
  void txMsgDone();
  // void txFragDone();

  int startBcast();
  int startUcast();
  
  bool sendRTS();
  bool sendCTS(double duration);
  bool sendDATA();
  bool sendACK(double duration);
  bool sendSYNC();

  void sentRTS(Packet *p);
  void sentCTS(Packet *p);
  void sentDATA(Packet *p);
  void sentACK(Packet *p);
  void sentSYNC(Packet *p);
  
  // Misc functions
  void collision(Packet *p);
  void capture(Packet *p);
  double txtime(Packet *p);
  
  void updateNav(double duration);
  void updateNeighNav(double duration);

  void mac_log(Packet *p) {
    logtarget_->recv(p, (Handler*) 0);
  }
  
  void discard(Packet *p, const char* why);
  int drop_RTS(Packet *p, const char* why);
  int drop_CTS(Packet *p, const char* why);
  int drop_DATA(Packet *p, const char* why);
  int drop_SYNC(Packet *p, const char* why);

  // smac methods to set dst, src and hdr_type in pkt hdrs
  inline int hdr_dst(char* hdr, int dst = -2) {
    struct hdr_smac *sh = (struct hdr_smac *) hdr;
    if (dst > -2)
      sh->dstAddr = dst;
    return sh->dstAddr;
  }
  inline int hdr_src(char* hdr, int src = -2) {
    struct hdr_smac *sh = (struct hdr_smac *) hdr;
    if (src > -2)
      sh->srcAddr = src;
    return sh->srcAddr;
  }
  inline int hdr_type(char *hdr, u_int16_t type = 0) {
    struct hdr_smac *sh = (struct hdr_smac *) hdr;
    if (type)
      sh->type = type;
    return sh->type;
  }
  
  // SMAC internal variables
  
  NsObject*       logtarget_;
  
  // Internal states
  int  state_;                   // MAC state
  int  radioState_;              // state of radio, rx, tx or sleep
  int tx_active_;                
  int mac_collision_;            
  
  int sendAddr_;		// node to send data to
  int recvAddr_;		// node to receive data from
  
  double  nav_;	        // network allocation vector. nav>0 -> medium busy
  double  neighNav_;      // track neighbors' NAV while I'm sending/receiving
  
  // SMAC Timers
  SmacNavTimer	        mhNav_;		// NAV timer medium is free or not
  SmacNeighNavTimer     mhNeighNav_;    // neighbor NAV timer for data timeout
  SmacSendTimer		mhSend_;	// incoming packets
  SmacRecvTimer         mhRecv_;        // outgoing packets
  SmacGeneTimer         mhGene_;        // generic timer used sync/CTS/ACK timeout
  SmacCsTimer           mhCS_;          // carrier sense timer
  
  // array of countertimer, one for each schedule
  // counter tracking node's sleep/awake cycle
  SmacCounterTimer      *mhCounter_[SMAC_MAX_NUM_SCHEDULES];  


  int numRetry_;	// number of tries for a data pkt
  int numExtend_;      // number of extensions on Tx time when frags are lost
  //int numFrags_;       // number of fragments in this transmission
  //int succFrags_;      // number of successfully transmitted fragments
  int lastRxFrag_;     // keep track of last data fragment recvd to prevent duplicate data

  int howToSend_;		// broadcast or unicast
  
  double durSyncPkt_;     // duration of sync packet
  double durDataPkt_;     // duration of data packet XXX caveat fixed packet size
  double durCtrlPkt_;     // duration of control packet
  double timeWaitCtrl_;   // set timer to wait for a control packet
  
  struct SchedTable schedTab_[SMAC_MAX_NUM_SCHEDULES];   // schedule table
  struct NeighbList neighbList_[SMAC_MAX_NUM_NEIGHBORS]; // neighbor list

  int mySyncNode_;                                 // nodeid of my synchronizer
  
  int currSched_;      // current schedule I'm talking to
  int numSched_;       // number of different schedules
  int numNeighb_;      // number of known neighbors
  int numBcast_;       // number of times needed to broadcast a packet
  
  Packet *dataPkt_;		// outgoing data packet
  Packet *pktRx_;               // buffer for incoming pkt
  Packet *pktTx_;               // buffer for outgoing pkt

  // flag to check pending data pkt for tx
  // when smac is not following SYNC (sleep-wakeup) cycles.
  int txData_ ;

  int syncFlag_;  // is set to 1 when SMAC uses sleep-wakeup cycle
  int selfConfigFlag_;  // is set to 0 when SMAC uses user configurable schedule start time
  double startTime_;  // schedule start time (schedule starts from SYNC period)

  // sleep-wakeup cycle times
  int syncTime_;
  int dataTime_;
  int listenTime_;
  int sleepTime_;
  int cycleTime_;

  // neighbor discovery

  int searchNeighb_;  // flag indicating if node is in neighbot discovery period
  int schedListen_;  // flag indicating if node is in scheduled listen period
  int numSync_;  // used to set/clear searchNeighb flag
  

 protected:
  int command(int argc, const char*const* argv);
  virtual int initialized() { 
    return (netif_ && uptarget_ && downtarget_); 
  }
};


#endif //NS_SMAC
