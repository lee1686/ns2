#
# Copyright (c) 1999 Regents of the University of California.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgement:
#       This product includes software developed by the MASH Research
#       Group at the University of California Berkeley.
# 4. Neither the name of the University nor of the Research Group may be
#    used to endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# Contributed by Tom Henderson, UCB Daedalus Research Group, June 1999

# @(#) $Header: /home/smtatapudi/Thesis/nsnam/nsnam/ns-2/tcl/lib/ns-sat.tcl,v 1.5 1999/08/29 01:29:14 tomh Exp $


# ======================================================================
#
# The Node/SatNode class (a pure virtual class)
#
# ======================================================================


Node/SatNode instproc init args {
	eval $self next $args		;# parent class constructor

	$self instvar nifs_ 
	$self instvar phy_tx_ phy_rx_ mac_ ifq_ ll_ pos_ hm_

	set nifs_	0		;# number of network interfaces
	# Create a drop trace to log packets for which no route exists
	set ns_ [Simulator instance]
	set dropT_ [$ns_ create-trace Sat/Drop [$ns_ set traceAllFile_] \
	  $self $self ""]
	$self set_trace $dropT_


}

Node/SatNode instproc reset {} {
        eval $self next 
	$self instvar hm_ instvar nifs_ phy_tx_ phy_rx_ mac_ ifq_ ll_
	set ns [Simulator instance]
	set now_ [$ns now]
        for {set i 0} {$i < $nifs_} {incr i} {
		$phy_tx_($i) reset
		$phy_rx_($i) reset
		if {[info exists mac_($i)]} {
			$mac_($i) reset
		}
		if {[info exists ll_($i)]} {
			$ll_($i) reset
		}
		if {[info exists ifq_($i)]} {
			$ifq_($i) reset
		}
	}
	if {$now_ == 0} {
		if {[info exists hm_]} {
			$ns at 0.0 "$self start_handoff"
		}
	}
}

Node/SatNode instproc set_next {node_} {
	$self instvar pos_
	$pos_ set_next [$node_ set pos_]
}

#
# Attach an agent to a node.  Pick a port and
# bind the agent to the port number.
# if portnumber is 255, default target is set to the routing agent
#
Node/SatNode instproc add-target {agent port } {

	$self instvar dmux_ 
	
	if { $port == 255 } {			
		# Set the default target at C++ level.  
		[$self set classifier_] defaulttarget $agent
		$dmux_ install $port $agent
	} else {
		#
		# Send Target
		#
		$agent target [$self entry]

		#
		# Recv Target
		#
		$dmux_ install $port $agent
	}
}

# ======================================================================
#
# methods for creating SatNodes
#
# ======================================================================

# Wrapper method for making a polar satellite node that first creates a 
# satnode plus two link interfaces (uplink and downlink) plus two 
# satellite channels (uplink and downlink)
# Additional ISLs can be added later
Simulator instproc satnode-polar {alt inc lon alpha plane linkargs chan} {
        set tmp [$self satnode polar $alt $inc $lon $alpha $plane]
	$self add-first-links $tmp gsl $linkargs $chan
        return $tmp
}

# Wrapper method for making a geo satellite node that first creates a 
# satnode plus two link interfaces (uplink and downlink) plus two 
# satellite channels (uplink and downlink)
Simulator instproc satnode-geo {lon linkargs chan} {
        set tmp [$self satnode geo $lon]
	$self add-first-links $tmp gsl $linkargs $chan
	return $tmp
}

# Wrapper method for making a geo satellite repeater node that first creates a 
# satnode plus two link interfaces (uplink and downlink) plus two 
# satellite channels (uplink and downlink)
Simulator instproc satnode-geo-repeater {lon chan} {
        set tmp [$self satnode geo $lon]
	$self add-first-links $tmp gsl-repeater "" $chan
	return $tmp
}

# Wrapper method that does nothing really but makes the API consistent 
Simulator instproc satnode-terminal {lat lon} {
	$self satnode terminal $lat $lon
}

Simulator instproc satnode args {
	$self instvar Node_
	set node [new Node/SatNode]
	if {[lindex $args 0] == "polar" || [lindex $args 0] == "Polar"} {
		set args [lreplace $args 0 0]
		# Set up position object
		$node set pos_ [new Position/Sat/Polar $args]
		$node cmd set_position [$node set pos_]
		[$node set pos_] setnode $node
		# Set up handoff manager
		$node set hm_ [new HandoffManager/Sat]
		$node cmd set_handoff_mgr [$node set hm_]
		[$node set hm_] setnode $node
		$node create-ragent 
	} elseif {[lindex $args 0] == "geo" || [lindex $args 0] == "Geo"} {  
		set args [lreplace $args 0 0]
		$node set pos_ [new Position/Sat/Geo $args]
		$node cmd set_position [$node set pos_]
		[$node set pos_] setnode $node
		$node create-ragent
	} elseif {[lindex $args 0] == "geo-repeater" || [lindex $args 0] == "Geo-repeater"} {  
		set args [lreplace $args 0 0]
		$node set pos_ [new Position/Sat/Geo $args]
		$node cmd set_position [$node set pos_]
		[$node set pos_] setnode $node
	} elseif {[lindex $args 0] == "terminal" || [lindex $args 0] == "Terminal"} {  
		set args [lreplace $args 0 0]
		$node set pos_ [new Position/Sat/Term $args]
		$node cmd set_position [$node set pos_]
		[$node set pos_] setnode $node
		$node set hm_ [new HandoffManager/Term]
		$node cmd set_handoff_mgr [$node set hm_]
		[$node set hm_] setnode $node
		$node create-ragent
	} else {
		puts "Otcl error; satnode specified incorrectly: $args"
	}
	set Node_([$node id]) $node
	$node set ns_ $self
        if [$self multicast?] {
                $node enable-mcast $self
        }
	$self check-node-num
	return $node
}

# ======================================================================
#
# methods for creating satellite links, channels, and error models
#
# ======================================================================

# Helper method for creating uplinks and downlinks for a satellite node
Simulator instproc add-first-links {node_ linktype linkargs chan} {
	$node_ set_downlink $chan
        $node_ set_uplink $chan
        # Add the interface for these channels and then attach the channels
	if {$linktype == "gsl-repeater"} {
		$node_ add-repeater $chan
	} else {
        	eval $node_ add-interface $linktype $linkargs
	}
        $node_ attach-to-outlink [$node_ set downlink_]
        $node_ attach-to-inlink [$node_ set uplink_]
}

#  Add network stack and attach to the channels 
Node/SatNode instproc add-gsl {ltype opt_ll opt_ifq opt_qlim opt_mac \
opt_bw opt_phy opt_inlink opt_outlink} {
	$self add-interface $ltype $opt_ll $opt_ifq $opt_qlim $opt_mac $opt_bw \
		$opt_phy 
	$self attach-to-inlink $opt_inlink
	$self attach-to-outlink $opt_outlink
}

# Add network stacks for ISLs, and create channels for them
Simulator instproc add-isl {ltype node1 node2 bw qtype qlim} {
	set opt_ll LL/Sat
	set opt_mac Mac/Sat
	set opt_phy Phy/Sat
	set opt_chan Channel/Sat
	set chan1 [new $opt_chan]
	set chan2 [new $opt_chan]
	$node1 add-interface $ltype $opt_ll $qtype $qlim $opt_mac $bw $opt_phy $chan1 $chan2
	$node2 add-interface $ltype $opt_ll $qtype $qlim $opt_mac $bw $opt_phy $chan2 $chan1
	if {$ltype == "crossseam"} {
		# Add another spare interface between node 1 and node 2
		$node1 add-interface $ltype $opt_ll $qtype $qlim $opt_mac $bw $opt_phy 
		$node2 add-interface $ltype $opt_ll $qtype $qlim $opt_mac $bw $opt_phy 
		
	}
}

Node/SatNode instproc add-repeater chan { 
	$self instvar nifs_ phy_tx_ phy_rx_ linkhead_ 

	set t $nifs_
	incr nifs_

	# linkhead_($t) is a simple connector that provides an API for
	# accessing elements of the network interface stack
	set linkhead_($t) [new Connector/LinkHead/Sat]
	set phy_tx_($t)	[new Phy/Repeater]		;# interface
	set phy_rx_($t)	[new Phy/Repeater]

	# linkhead maintains a collection of pointers
	$linkhead_($t) setnode $self
	$linkhead_($t) setphytx $phy_tx_($t)
	$linkhead_($t) setphyrx $phy_rx_($t)
	$linkhead_($t) set_type "gsl-repeater"
	$linkhead_($t) set type_ "gsl-repeater"

	$phy_rx_($t) up-target $phy_tx_($t)
	$phy_tx_($t) linkhead $linkhead_($t)
	$phy_rx_($t) linkhead $linkhead_($t)
	$phy_tx_($t) node $self		;# Bind node <---> interface
	$phy_rx_($t) node $self		;# Bind node <---> interface
}

#
#  The following sets up link layer, mac layer, interface queue 
#  and physical layer structures for the satellite nodes.
#  Propagation model is an optional argument
#
Node/SatNode instproc add-interface args { 

	$self instvar nifs_ phy_tx_ phy_rx_ mac_ ifq_ ll_ drophead_ linkhead_

	global ns_ MacTrace opt

	set t $nifs_
	incr nifs_

	# linkhead_($t) is a simple connector that provides an API for
	# accessing elements of the network interface stack
	set linkhead_($t) [new Connector/LinkHead/Sat]

	set linktype 	[lindex $args 0]
	set ll_($t)	[new [lindex $args 1]]		;# link layer
	set ifq_($t)	[new [lindex $args 2]]		;# interface queue
	set qlen	[lindex $args 3]
	set mac_($t)	[new [lindex $args 4]]		;# mac layer
	set mac_bw	[lindex $args 5]
	set phy_tx_($t)	[new [lindex $args 6]]		;# interface
	set phy_rx_($t)	[new [lindex $args 6]]		;# interface
	set inchan 	[lindex $args 7]
	set outchan 	[lindex $args 8]
	set drophead_($t) [new Connector]	;# drop target for queue
	set iif_($t) [new NetworkInterface]
	

	#
	# Local Variables
	#
	#set nullAgent_ [$ns_ set nullAgent_]
	set linkhead $linkhead_($t)
	set phy_tx $phy_tx_($t)
	set phy_rx $phy_rx_($t)
	set mac $mac_($t)
	set ifq $ifq_($t)
	set ll $ll_($t)
	set drophead $drophead_($t)
	set iif $iif_($t)

	# linkhead maintains a collection of pointers
	$linkhead setnode $self
	$linkhead setll $ll
	$linkhead setmac $mac
	$linkhead setqueue $ifq
	$linkhead setphytx $phy_tx
	$linkhead setphyrx $phy_rx
	$linkhead setnetinf $iif
	$self addlinkhead $linkhead; # Add NetworkInterface to node's list
	$linkhead target $ll; 
	$linkhead set_type $linktype
	$linkhead set type_ $linktype

	# 
	# NetworkInterface
	#
	$iif target [$self entry]

	#
	# Link Layer
	#
	$ll mac $mac; # XXX is this needed?
	$ll up-target $iif
	$ll down-target $ifq
	$ll set delay_ 0ms; # processing delay between ll and ifq

	#
	# Interface Queue
	#
	$ifq target $mac
	$ifq set qlim_ $qlen
        $drophead target [[Simulator instance] set nullAgent_]
        $ifq drop-target $drophead


	#
	# Mac Layer
	#
	$mac netif $phy_tx; # Not used by satellite code at this time
	$mac up-target $ll
	$mac down-target $phy_tx
	$mac set bandwidth_ $mac_bw; 
	#$mac nodes $opt(nn)
	
	#
	# Physical Layer
	#
	$phy_rx up-target $mac
	$phy_tx linkhead $linkhead
	$phy_rx linkhead $linkhead
	$phy_tx node $self		;# Bind node <---> interface
	$phy_rx node $self		;# Bind node <---> interface

	# If we have a channel to attach this link to (for an point-to-point
	# ISL), do so here
	if {$outchan != "" && $inchan != ""} {
		$phy_tx channel $outchan
		$phy_rx channel $inchan
		# Add to list of Phys receiving on the channel
		$inchan addif $phy_rx
	}
	return $t
}

Node/SatNode instproc set_uplink {chan} {
	$self instvar uplink_
	set uplink_ [new $chan]
	$self cmd set_uplink $uplink_
}

Node/SatNode instproc set_downlink {chan} {
	$self instvar downlink_
	set downlink_ [new $chan]
	$self cmd set_downlink $downlink_
}

#  Attaches channel to the phy indexed by "index" (by default, the first one)
Node/SatNode instproc attach-to-outlink {chan {index 0} } {
	$self instvar phy_tx_ mac_
	$phy_tx_($index) channel $chan
	#$mac_($index) channel $chan; # For mac addr resolution (rather than ARP)
}

#  Attaches channel to the phy indexed by "index" (by default, the first one)
Node/SatNode instproc attach-to-inlink { chan {index 0}} {
	$self instvar phy_rx_ 
	$phy_rx_($index) channel $chan
	$chan addif $phy_rx_($index)
}

#  Attaches error model to interface "index" (by default, the first one)
Node/SatNode instproc interface-errormodel { em { index 0 } } {
	$self instvar mac_ ll_ em_ linkhead_
	$mac_($index) up-target $em
	$em target $ll_($index)
	$em drop-target [new Agent/Null]; # otherwise, packet is only marked
	set em_($index) $em
	$linkhead_($index) seterrmodel $em
} 

# ======================================================================
#
# methods for routing 
#
# ======================================================================

# Create a network interface (for one uplink and downlink), add routing agent
# At least one interface must be created before routing agent is added
Node/SatNode instproc create-ragent {} {
	set ragent [new Agent/SatRoute]
        $self attach $ragent 255; # attaches to default target of classifier  
        $ragent set myaddr_ [$self set id_]
	$self set_ragent $ragent; # sets pointer at C++ level
	$ragent set_node $self; # sets back pointer in ragent to node
}

# When running all routing in C++, we want a dummy OTcl routing
#
Class Agent/rtProto/Dummy -superclass Agent/rtProto

Agent/rtProto/Dummy proc init-all args {
	# Nothing
}

# ======================================================================
#
# methods for tracing
#
# ======================================================================

Simulator instproc trace-all-satlinks {f} {
	$self instvar Node_
	foreach nn [array names Node_] {
		if {![$Node_($nn) info class Node/SatNode]} {
			continue; # Not a SatNode
		}
		$Node_($nn) trace-all-satlinks $f
	}
}

# All satlinks should have an interface indexed by nifs_
Node/SatNode instproc trace-all-satlinks {f} {
	$self instvar nifs_ enqT_ rcvT_ linkhead_
	for {set i 0} {$i < $nifs_} {incr i} {
		if {[$linkhead_($i) set type_] == "gsl-repeater"} {
			continue;
		}
		if {[info exists enqT_($i)]} {
			puts "Tracing already exists on node [$self id]"
		} else {
			$self trace-outlink-queue $f $i
		}
		if {[info exists rcvT_($i)]} {
			puts "Tracing already exists on node [$self id]"
		} else {
			$self trace-inlink-queue $f $i
		}
	}
}

# Set up trace objects around first output queue for packets destined to node
Node/SatNode instproc trace-outlink-queue {f {index_ 0} } {
	$self instvar id_ enqT_ deqT_ drpT_ mac_ ll_ ifq_ drophead_ 
	
	set ns [Simulator instance]
	set fromNode_ $id_
	set toNode_ -1

	set enqT_($index_) [$ns create-trace Sat/Enque $f $fromNode_ $toNode_]
	$enqT_($index_) target $ifq_($index_)
	$ll_($index_) down-target $enqT_($index_)

	set deqT_($index_) [$ns create-trace Sat/Deque $f $fromNode_ $toNode_]
	$deqT_($index_) target $mac_($index_)
	$ifq_($index_) target $deqT_($index_)

	set drpT_($index_) [$ns create-trace Sat/Drop $f $fromNode_ $toNode_]
	$drpT_($index_) target [$drophead_($index_) target]
	$drophead_($index_) target $drpT_($index_)
	$ifq_($index_) drop-target $drpT_($index_)
}

# Trace element between mac and ll tracing packets between node and node
Node/SatNode instproc trace-inlink-queue {f {index_ 0} } {
	$self instvar id_ rcvT_ mac_ ll_ phy_rx_ em_ errT_    

	set ns [Simulator instance]
	set toNode_ $id_
	set fromNode_ -1

	if {[info exists em_($index_)]} {
		# if error model, then chain mac -> em -> rcvT -> ll
		# First, set up an error trace on the ErrorModule
		set errT_($index_) [$ns create-trace Sat/Error $f $fromNode_ $toNode_]
		$errT_($index_) target [$em_($index_) drop-target]
		$em_($index_) drop-target $errT_($index_)
		set rcvT_($index_) [$ns create-trace Sat/Recv $f $fromNode_ $toNode_]
		$rcvT_($index_) target [$em_($index_) target]
		$em_($index_) target $rcvT_($index_)
	} else {
		# if no error model, then insert between mac and ll
		set rcvT_($index_) [$ns create-trace Sat/Recv $f $fromNode_ $toNode_]
		$rcvT_($index_) target [$mac_($index_) up-target]
		$mac_($index_) up-target $rcvT_($index_)
	}
	
}


###########
# TRACE MODIFICATIONS
##########

# This creates special satellite tracing elements
# See __FILE__.cc
# Support for Enque, Deque, Recv, Drop, Generic (not Session)

Class Trace/Sat/Hop -superclass Trace/Sat
Trace/Sat/Hop instproc init {} {
        $self next "h"
}

Class Trace/Sat/Enque -superclass Trace/Sat
Trace/Sat/Enque instproc init {} {
        $self next "+"
}

Trace/Sat/Deque instproc init {} {
        $self next "-"
}

Class Trace/Sat/Recv -superclass Trace/Sat
Trace/Sat/Recv instproc init {} {
        $self next "r"
}

Class Trace/Sat/Drop -superclass Trace/Sat
Trace/Sat/Drop instproc init {} {
        $self next "d"
}

Class Trace/Sat/Error -superclass Trace/Sat
Trace/Sat/Error instproc init {} {
        $self next "e"
}

Class Trace/Sat/Generic -superclass Trace/Sat
Trace/Sat/Generic instproc init {} {
        $self next "v"
}


# ======================================================================
#
# Defaults for bound variables 
#
# ======================================================================

Node/SatNode set dist_routing_ "false"; # distributed routing not yet supported
Position/Sat set time_advance_ 0; # time offset to start of simulation 
Position/Sat/Polar set plane_ 0
HandoffManager/Term set elevation_mask_ 0
HandoffManager/Term set term_handoff_int_ 10
HandoffManager/Sat set sat_handoff_int_ 10
HandoffManager/Sat set latitude_threshold_ 70
HandoffManager/Sat set longitude_threshold_ 0
HandoffManager set handoff_randomization_ "false" 
SatRouteObject set metric_delay_ "true"
SatRouteObject set data_driven_computation_ "false"
Mac/Sat/UnslottedAloha set mean_backoff_ 1s; # mean backoff time upon collision
Mac/Sat/UnslottedAloha set rtx_limit_ 3; # Retransmission limit 
Mac/Sat/UnslottedAloha set send_timeout_ 270ms; # Timer interval for new sends

Agent/SatRoute set myaddr_       0        ;# My address
Mac/Sat set bandwidth_ 2Mb 

