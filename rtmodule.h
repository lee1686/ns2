/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- 
 *
 * Copyright (C) 2000 by USC/ISI
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
 * $Header: /home/smtatapudi/Thesis/nsnam/nsnam/ns-2/Attic/rtmodule.h,v 1.3 2000/09/16 01:46:01 haoboy Exp $
 *
 * Definition of RoutingModule, base class for all extensions to routing 
 * functionality in a Node. These modules are meant to be "plugin", and 
 * should be configured via node-config{} interface in tcl/lib/ns-lib.tcl.
 */

#ifndef ns_rtmodule_h
#define ns_rtmodule_h

#include <tclcl.h>

class Node;

class RoutingModule : public TclObject {
public:
	RoutingModule() : n_(NULL) {}
	/*
	 * Returns the node to which this module is attached.
	 */ 
	inline Node* node() { return n_; }
	/*
	 * Node-related module-specific initialization can be done here.
	 * However: (1) RoutingModule::attach() must be called from derived
	 * class so the value of n_ is setup, (2) module-specific 
	 * initialization that does not require knowledge of Node should 
	 * always stay in the module constructor.
	 *
	 * Return TCL_ERROR if initialization fails.
	 */
	virtual int attach(Node *n) { n_ = n; return TCL_OK; }
	virtual int command(int argc, const char*const* argv);
	virtual const char* module_name() const { return NULL; }
private:
	Node *n_;
};

class BaseRoutingModule : public RoutingModule {
public:
	BaseRoutingModule() : RoutingModule() {}
	virtual const char* module_name() const { return "Base"; }
};

class McastRoutingModule : public RoutingModule {
public:
	McastRoutingModule() : RoutingModule() {}
	virtual const char* module_name() const { return "Mcast"; }
};

class HierRoutingModule : public RoutingModule {
public:
	HierRoutingModule() : RoutingModule() {}
	virtual const char* module_name() const { return "Hier"; }
};

class ManualRoutingModule : public RoutingModule {
public:
	ManualRoutingModule() : RoutingModule() {}
	virtual const char* module_name() const { return "Manual"; }
};

class VcRoutingModule : public RoutingModule {
public:
	VcRoutingModule() : RoutingModule() {}
	virtual const char* module_name() const { return "VC"; }
};

#endif //  ns_rtmodule_h
