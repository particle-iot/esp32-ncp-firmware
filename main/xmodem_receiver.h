/*
 * Copyright (c) 2018 Particle Industries, Inc.  All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "common.h"

namespace particle {

class Stream;
class OutputStream;

// Class implementing an XMODEM-1K receiver
class XmodemReceiver {
public:
    enum Status {
        DONE = 0,
        RUNNING
    };

    XmodemReceiver();
    ~XmodemReceiver();

    int init(Stream* src, OutputStream* dest, size_t size);
    void destroy();

    // Returns one of the values defined by the `Status` enum or a negative value in case of an error.
    // This method needs to be called in a loop
    int run();

private:
    // Receiver state
    enum class State {
        NEW, // Uninitialized
        SEND_NCG, // Sending NCGbyte
        RECV_SOH, // Receiving "start of header" byte
        RECV_PACKET_HEADER, // Receiving packet header
        RECV_PACKET_DATA, // Receiving remaining packet data
        SEND_PACKET_ACK, // Sending packet acknowledgement
        SEND_PACKET_NAK, // Sending negative packet acknowledgement
        SEND_EOT_ACK, // Sending "end of transmission" acknowledgement
        SEND_CAN // Cancelling the transfer
    };

    State state_; // Current receiver state
    uint64_t stateTime_; // Time when the receiver state was last changed
    unsigned retryCount_; // Number of retries
    unsigned canCount_; // Number of received CAN bytes
    int lastError_; // Last error

    Stream* srcStrm_; // Source stream
    OutputStream* destStrm_; // Destination stream
    size_t fileSize_; // File size
    size_t fileOffs_; // Current offset in the file

    size_t packetSize_; // Size of the current XMODEM packet
    size_t packetOffs_; // Number of received bytes of the current packet
    unsigned packetNum_; // Packet number

    std::unique_ptr<char[]> buf_; // Packet buffer

    int sendNcg();
    int recvSoh();
    int recvPacketHeader();
    int recvPacketData();
    int sendPacketAck();
    int sendPacketNak();
    int sendEotAck();
    int sendCan();

    int flush();
    int checkTimeout(unsigned timeout);
    int checkPacketTimeout();
    void setState(State state, bool restartTimer = true);
    void setError(int error);
};

} // particle
