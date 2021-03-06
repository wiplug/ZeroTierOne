/*
 * ZeroTier One - Global Peer to Peer Ethernet
 * Copyright (C) 2012-2013  ZeroTier Networks LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * --
 *
 * ZeroTier may be used and distributed under the terms of the GPLv3, which
 * are available at: http://www.gnu.org/licenses/gpl-3.0.html
 *
 * If you would like to embed ZeroTier into a commercial application or
 * redistribute it in a modified binary form, please contact ZeroTier Networks
 * LLC. Start here: http://www.zerotier.com/
 */

#ifndef _ZT_ETHERNETTAP_HPP
#define _ZT_ETHERNETTAP_HPP

#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <list>
#include <vector>
#include <set>
#include <string>
#include <queue>
#include <stdexcept>

#include "Constants.hpp"
#include "InetAddress.hpp"
#include "MAC.hpp"
#include "Mutex.hpp"
#include "Condition.hpp"
#include "MulticastGroup.hpp"
#include "Thread.hpp"
#include "Buffer.hpp"
#include "Array.hpp"

#ifdef __WINDOWS__
#include <WinSock2.h>
#include <Windows.h>
#endif

namespace ZeroTier {

class RuntimeEnvironment;

/**
 * System ethernet tap device
 */
class EthernetTap
{
public:
	/**
	 * Construct a new TAP device
	 *
	 * Handler arguments: arg,from,to,etherType,data
	 * 
	 * @param renv Runtime environment
	 * @param tag A tag used to identify persistent taps at the OS layer (e.g. nwid in hex)
	 * @param mac MAC address of device
	 * @param mtu MTU of device
	 * @param desc If non-NULL, a description (not used on all OSes)
	 * @param handler Handler function to be called when data is received from the tap
	 * @param arg First argument to handler function
	 * @throws std::runtime_error Unable to allocate device
	 */
	EthernetTap(
		const RuntimeEnvironment *renv,
		const char *tag,
		const MAC &mac,
		unsigned int mtu,
		void (*handler)(void *,const MAC &,const MAC &,unsigned int,const Buffer<4096> &),
		void *arg)
		throw(std::runtime_error);

	/**
	 * Close tap and shut down thread
	 *
	 * This may block for a few seconds while thread exits.
	 */
	~EthernetTap();

	/**
	 * Perform OS dependent actions on network configuration change detection
	 */
	void whack();

	/**
	 * Set whether or not DHCP is enabled (disabled by default)
	 *
	 * @param dhcp DHCP status
	 * @return New state of DHCP (may be false even on 'true' if DHCP enable failed)
	 */
	bool setDhcpEnabled(bool dhcp);

	/**
	 * Set whether or not DHCP6 is enabled (disabled by default)
	 *
	 * @param dhcp DHCP6 status
	 * @return New state of DHCP6 (may be false even on 'true' if DHCP enable failed)
	 */
	bool setDhcp6Enabled(bool dhcp);

	/**
	 * Set the user display name for this connection
	 *
	 * This does nothing on platforms that don't have this concept.
	 *
	 * @param dn User display name
	 */
	void setDisplayName(const char *dn);

	/**
	 * @return MAC address of this interface
	 */
	inline const MAC &mac() const throw() { return _mac; }

	/**
	 * @return MTU of this interface
	 */
	inline unsigned int mtu() const throw() { return _mtu; }

	/**
	 * Add an IP to this interface
	 *
	 * @param ip IP and netmask (netmask stored in port field)
	 * @return True if IP added successfully
	 */
	bool addIP(const InetAddress &ip);

	/**
	 * Remove an IP from this interface
	 *
	 * @param ip IP and netmask (netmask stored in port field)
	 * @return True if IP removed successfully
	 */
	bool removeIP(const InetAddress &ip);

	/**
	 * @return Set of IP addresses / netmasks
	 */
	inline std::set<InetAddress> ips() const
	{
		Mutex::Lock _l(_ips_m);
		return _ips;
	}

	/**
	 * @return Set of IP addresses / netmasks included any we did not assign, link-local, etc.
	 */
	std::set<InetAddress> allIps() const;

	/**
	 * Set this tap's IP addresses to exactly this set of IPs
	 *
	 * New IPs are created, ones not in this list are removed.
	 *
	 * @param ips IP addresses with netmask in port field
	 */
	inline void setIps(const std::set<InetAddress> &allIps)
	{
		for(std::set<InetAddress>::iterator i(allIps.begin());i!=allIps.end();++i)
			addIP(*i);
		std::set<InetAddress> myIps(ips());
		for(std::set<InetAddress>::iterator i(myIps.begin());i!=myIps.end();++i) {
			if (!allIps.count(*i))
				removeIP(*i);
		}
	}

	/**
	 * Put a frame, making it available to the OS for processing
	 *
	 * @param from MAC address from which frame originated
	 * @param to MAC address of destination (typically MAC of tap itself)
	 * @param etherType Ethernet protocol ID
	 * @param data Frame payload
	 * @param len Length of frame
	 */
	void put(const MAC &from,const MAC &to,unsigned int etherType,const void *data,unsigned int len);

	/**
	 * @return OS-specific device or connection name
	 */
	std::string deviceName() const;

	/**
	 * Fill or modify a set to contain multicast groups for this device
	 *
	 * This populates a set or, if already populated, modifies it to contain
	 * only multicast groups in which this device is interested.
	 *
	 * This should always include the blind wildcard MulticastGroup (MAC of
	 * ff:ff:ff:ff:ff:ff and 0 ADI field).
	 *
	 * @param groups Set to modify in place
	 * @return True if set was changed since last call
	 */
	bool updateMulticastGroups(std::set<MulticastGroup> &groups);

	/**
	 * Thread main method; do not call elsewhere
	 */
	void threadMain()
		throw();

private:
	const MAC _mac;
	const unsigned int _mtu;

	const RuntimeEnvironment *_r;

	std::set<InetAddress> _ips;
	Mutex _ips_m;

	void (*_handler)(void *,const MAC &,const MAC &,unsigned int,const Buffer<4096> &);
	void *_arg;

	bool _dhcp;
	bool _dhcp6;

	Thread _thread;

#ifdef __UNIX_LIKE__
	char _dev[16];
	int _fd;
	int _shutdownSignalPipe[2];
#endif

#ifdef __WINDOWS__
	HANDLE _tap;
	OVERLAPPED _tapOvlRead,_tapOvlWrite;
	char _tapReadBuf[ZT_IF_MTU + 32];
	HANDLE _injectSemaphore;
	GUID _deviceGuid;
	std::string _myDeviceInstanceId; // NetCfgInstanceId, a GUID
	std::string _myDeviceInstanceIdPath; // DeviceInstanceID, another kind of "instance ID"
	std::queue< std::pair< Array<char,ZT_IF_MTU + 32>,unsigned int > > _injectPending;
	Mutex _injectPending_m;
	volatile bool _run;
#endif
};

} // namespace ZeroTier

#endif
