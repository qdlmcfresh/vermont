/*
 * VERMONT
 * Copyright (C) 2007 Tobias Limmer <tobias.limmer@informatik.uni-erlangen.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#if !defined(CONNECTION_H)
#define CONNECTION_H

#include <stdint.h>
#include <string>

#include "common/ManagedInstance.h"
#include "IpfixRecord.hpp"
#include "common/Time.h"

using namespace std;


class Connection
{
	public:
		// static values in network order
#if __BYTE_ORDER == __LITTLE_ENDIAN
		static const uint16_t FIN = 0x0001;
		static const uint16_t SYN = 0x0002;
		static const uint16_t RST = 0x0004;
		static const uint16_t PSH = 0x0008;
		static const uint16_t ACK = 0x0010;
		static const uint16_t URG = 0x0020;
		static const uint16_t ECE = 0x0040;
		static const uint16_t CWR = 0x0080;
		static const uint16_t NS  = 0x0100;
		static const uint16_t MASK = 0x01ff; /**< mask reserved bits and size **/
#else
		static const uint16_t FIN = 0x0100;
		static const uint16_t SYN = 0x0200;
		static const uint16_t RST = 0x0400;
		static const uint16_t PSH = 0x0800;
		static const uint16_t ACK = 0x1000;
		static const uint16_t URG = 0x2000;
		static const uint16_t ECE = 0x4000;
		static const uint16_t CWR = 0x8000;
		static const uint16_t NS  = 0x0001;
		static const uint16_t MASK = 0xff01; /**< mask reserved bits and size **/
#endif
		// ATTENTION: next four elements MUST be declared sequentially without another element interrupting it
		// because hash and compare is performed by accessing the memory directly from srcIP on
		// (see function calcHash and compareTo)
		// one more notice: additional fields are to be added to function swapDataFields!
		uint32_t srcIP; /**< network-byte order! **/
		uint32_t dstIP; /**< network-byte order! **/
		uint16_t srcPort; /**< network-byte order! **/
		uint16_t dstPort; /**< network-byte order! **/

		// fields to be aggregated
		uint64_t srcTimeStart; /**< milliseconds since 1970, host-byte order */
		uint64_t srcTimeEnd; /**< milliseconds since 1970, host-byte order  */
		uint64_t dstTimeStart; /**< milliseconds since 1970, host-byte order  */
		uint64_t dstTimeEnd; /**< milliseconds since 1970, host-byte order  */
		uint64_t srcOctets; /**< network-byte order! **/
		uint64_t dstOctets; /**< network-byte order! **/
		uint64_t srcTransOctets; /**< host-byte order! **/
		uint64_t dstTransOctets; /**< host-byte order! **/
		uint64_t srcPackets; /**< network-byte order! **/
		uint64_t dstPackets; /**< network-byte order! **/
		uint16_t srcTcpControlBits; /**< network-byte order! **/
		uint16_t dstTcpControlBits; /**< network-byte order! **/
		uint8_t protocol;
		char* srcPayload;
		uint32_t srcPayloadLen; /**< host-byte order! **/
		char* dstPayload;
		uint32_t dstPayloadLen; /**< host-byte order! **/
		uint32_t srcPayloadPktCount;
		uint8_t dpaForcedExport;
		uint32_t dpaFlowCount; /**<host-byte order **/
		uint8_t dpaReverseStart;

		/**
		 * time in seconds from 1970 on when this record will expire
		 * this value is always updated when it is aggregated
		 */
		uint32_t timeExpire;

		Connection(IpfixDataRecord* record);
		virtual ~Connection();
		void addFlow(Connection* c);
		string toString();
		string printTcpControlBits(uint16_t bits);
		bool compareTo(Connection* c, bool to);
		uint32_t getHash(bool to, uint32_t maxval);
		void aggregate(Connection* c, uint32_t inactiveExpireTime, bool to);
		void swapDataFields();
		bool swapIfNeeded();

	private:
		string payloadToPlain(const char* payload, uint32_t len);
		string payloadToHex(const char* payload, uint32_t len);
};

#endif
