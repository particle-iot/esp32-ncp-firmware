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

#include "xmodem_receiver.h"

#include "stream.h"
#include "util.h"

#include <algorithm>

namespace particle {

namespace {

// Control bytes
enum Ctrl: char {
    SOH = 0x01, // Start of header (128-byte packet)
    STX = 0x02, // Start of header (1024-byte packet)
    EOT = 0x04, // End of transmission
    ACK = 0x06, // Acknowledgement
    NAK = 0x15, // Negative acknowledgement
    CAN = 0x18, // Cancel transmission
    C = 0x43 // XMODEM-CRC/1K mode
};

struct __attribute__((packed)) PacketHeader {
    uint8_t start; // SOH or STX depending on the packet size
    uint8_t num; // Packet number (1-based)
    uint8_t numComp; // 255 - `num`
};

struct __attribute__((packed)) PacketCrc {
    uint8_t msb; // Most significant byte of the packet's CRC-16
    uint8_t lsb; // Least significant byte of the packet's CRC-16
};

// Size of the packet buffer
const size_t BUFFER_SIZE = 1024 + sizeof(PacketHeader) + sizeof(PacketCrc);

// Timeout settings
const unsigned NCG_INTERVAL = 3000;
const unsigned PACKET_TIMEOUT = 10000;
const unsigned SEND_TIMEOUT = 3000;
const unsigned RECV_TIMEOUT = 3000;

// Maximum number of retries before aborting the transfer
const unsigned MAX_NCG_RETRY_COUNT = 10;
const unsigned MAX_PACKET_RETRY_COUNT = 2;

// Number of CAN bytes that need to be sent or received in order to cancel the transfer
const unsigned SEND_CAN_COUNT = 8;
const unsigned RECV_CAN_COUNT = 2;

// Calculates a 16-bit checksum using the CRC-CCITT (XMODEM) algorithm
uint16_t calcCrc16(const char* data, size_t size) {
    uint16_t crc = 0;
    const auto end = data + size;
    while (data < end) {
        const uint8_t c = *data++;
        crc ^= (uint16_t)c << 8;
        for (unsigned i = 0; i < 8; ++i) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

} // particle::

XmodemReceiver::XmodemReceiver() :
        state_(State::NEW) {
}

XmodemReceiver::~XmodemReceiver() {
    destroy();
}

int XmodemReceiver::init(Stream* src, OutputStream* dest, size_t size) {
    buf_.reset(new(std::nothrow) char[BUFFER_SIZE]);
    CHECK_TRUE(buf_, RESULT_NO_MEMORY);
    srcStrm_ = src;
    destStrm_ = dest;
    fileSize_ = size;
    fileOffs_ = 0;
    packetSize_ = 0;
    packetOffs_ = 0;
    packetNum_ = 0;
    retryCount_ = 0;
    canCount_ = 0;
    lastError_ = 0;
    setState(State::SEND_NCG);
    return 0;
}

void XmodemReceiver::destroy() {
    buf_.reset();
    state_ = State::NEW;
}

int XmodemReceiver::run() {
    int ret = 0;
    switch (state_) {
    case State::SEND_NCG: {
        ret = sendNcg();
        break;
    }
    case State::RECV_SOH: {
        ret = recvSoh();
        break;
    }
    case State::RECV_PACKET_HEADER: {
        ret = recvPacketHeader();
        break;
    }
    case State::RECV_PACKET_DATA: {
        ret = recvPacketData();
        break;
    }
    case State::SEND_PACKET_ACK: {
        ret = sendPacketAck();
        break;
    }
    case State::SEND_PACKET_NAK: {
        ret = sendPacketNak();
        break;
    }
    case State::SEND_EOT_ACK: {
        ret = sendEotAck();
        break;
    }
    case State::SEND_CAN: {
        ret = sendCan();
        break;
    }
    default:
        ret = RESULT_INVALID_STATE;
        break;
    }
    if (ret == Status::DONE || ret < 0) {
        destroy();
    }
    return ret;
}

int XmodemReceiver::sendNcg() {
    const char c = Ctrl::C;
    const size_t n = CHECK(srcStrm_->write(&c, 1));
    if (n > 0) {
        LOG_DEBUG(TRACE, "Sent NCGbyte");
        setState(State::RECV_SOH);
    } else {
        CHECK(checkTimeout(SEND_TIMEOUT));
    }
    return Status::RUNNING;
}

int XmodemReceiver::recvSoh() {
    char c = 0;
    const size_t n = CHECK(srcStrm_->read(&c, 1));
    if (n > 0) {
        if (c == Ctrl::CAN) {
            if (++canCount_ == RECV_CAN_COUNT) {
                LOG(WARN, "Sender has cancelled the transfer");
                setError(RESULT_CANCELLED);
            }
        } else {
            canCount_ = 0;
            if (c == Ctrl::EOT) {
                // Write the last received chunk to the destination stream
                const int ret = flush();
                if (ret < 0) {
                    setError(ret);
                } else if (fileOffs_ < fileSize_) {
                    LOG(ERROR, "Incomplete file transfer");
                    setError(RESULT_PROTOCOL_ERROR);
                } else {
                    setState(State::SEND_EOT_ACK);
                }
            } else if (c != Ctrl::SOH && c != Ctrl::STX) {
                LOG(ERROR, "Unexpected control byte: 0x%02x", (unsigned char)c);
                setError(RESULT_PROTOCOL_ERROR);
            } else if (fileOffs_ == fileSize_) {
                LOG(ERROR, "Unexpected packet");
                setError(RESULT_PROTOCOL_ERROR);
            } else {
                buf_[0] = c;
                packetOffs_ = 1;
                setState(State::RECV_PACKET_HEADER, false);
            }
        }
    } else if (packetNum_ != 0) {
        checkPacketTimeout();
    } else if (checkTimeout(NCG_INTERVAL) != 0) {
        if (++retryCount_ > MAX_NCG_RETRY_COUNT) {
            LOG(ERROR, "No response from sender");
            return RESULT_TIMEOUT;
        }
        setState(State::SEND_NCG);
    }
    return Status::RUNNING;
}

int XmodemReceiver::recvPacketHeader() {
    packetOffs_ += CHECK(srcStrm_->read(buf_.get() + packetOffs_, sizeof(PacketHeader) - packetOffs_));
    if (packetOffs_ == sizeof(PacketHeader)) {
        // Parse packet header
        PacketHeader h = {};
        memcpy(&h, buf_.get(), sizeof(PacketHeader));
        const size_t size = ((h.start == Ctrl::SOH) ? 128 : 1024) + sizeof(PacketHeader) + sizeof(PacketCrc);
        if (h.num + h.numComp != 255) {
            LOG(ERROR, "Malformed packet header");
            setError(RESULT_PROTOCOL_ERROR);
        } else if (packetNum_ != 0 && h.num == (packetNum_ & 0xff)) {
            if (++retryCount_ > MAX_PACKET_RETRY_COUNT) {
                LOG(ERROR, "Maximum number of retransmissions exceeded");
                setError(RESULT_LIMIT_EXCEEDED);
            } else {
                // Receiving a duplicate packet
                packetSize_ = size;
                setState(State::RECV_PACKET_DATA, false);
            }
        } else if (h.num != ((packetNum_ + 1) & 0xff)) {
            LOG(ERROR, "Unexpected packet number: %u", (unsigned)h.num);
            setError(RESULT_PROTOCOL_ERROR);
        } else {
            // Write the last received chunk to the destination stream
            const int ret = flush();
            if (ret < 0) {
                setError(ret);
            } else {
                ++packetNum_;
                retryCount_ = 0;
                packetSize_ = size;
                setState(State::RECV_PACKET_DATA, false);
            }
        }
    } else {
        checkPacketTimeout();
    }
    return Status::RUNNING;
}

int XmodemReceiver::recvPacketData() {
    packetOffs_ += CHECK(srcStrm_->read(buf_.get() + packetOffs_, packetSize_ - packetOffs_));
    if (packetOffs_ == packetSize_) {
        LOG_DEBUG(TRACE, "Received packet; number: %u, size: %u", packetNum_, (unsigned)packetSize_);
        // Verify packet checksum
        PacketCrc c = {};
        memcpy(&c, buf_.get() + packetSize_ - sizeof(PacketCrc), sizeof(PacketCrc));
        const uint16_t crc = (c.msb << 8) | c.lsb; // Received CRC-16
        const uint16_t compCrc = calcCrc16(buf_.get() + sizeof(PacketHeader), packetSize_ - sizeof(PacketHeader) -
                sizeof(PacketCrc)); // Computed CRC-16
        if (compCrc == crc) {
            setState(State::SEND_PACKET_ACK);
        } else {
            LOG(WARN, "Invalid checksum");
            if (retryCount_ < MAX_PACKET_RETRY_COUNT) {
                setState(State::SEND_PACKET_NAK);
            } else {
                // Do not send NAK if we can't accept a retransmission anyway
                LOG(ERROR, "Maximum number of retransmissions exceeded");
                setError(RESULT_LIMIT_EXCEEDED);
            }
        }
    } else {
        checkPacketTimeout();
    }
    return Status::RUNNING;
}

int XmodemReceiver::sendPacketAck() {
    const char c = Ctrl::ACK;
    const size_t n = CHECK(srcStrm_->write(&c, 1));
    if (n > 0) {
        LOG_DEBUG(TRACE, "Sent ACK");
        setState(State::RECV_SOH);
    } else {
        CHECK(checkTimeout(SEND_TIMEOUT));
    }
    return Status::RUNNING;
}

int XmodemReceiver::sendPacketNak() {
    const char c = Ctrl::NAK;
    const size_t n = CHECK(srcStrm_->write(&c, 1));
    if (n > 0) {
        LOG_DEBUG(TRACE, "Sent NAK");
        setState(State::RECV_SOH);
    } else {
        CHECK(checkTimeout(SEND_TIMEOUT));
    }
    return Status::RUNNING;
}

int XmodemReceiver::sendEotAck() {
    const char c = Ctrl::ACK;
    const size_t n = CHECK(srcStrm_->write(&c, 1));
    if (n > 0) {
        LOG_DEBUG(TRACE, "Sent ACK");
        return Status::DONE;
    }
    CHECK(checkTimeout(SEND_TIMEOUT));
    return Status::RUNNING;
}

int XmodemReceiver::sendCan() {
    if (packetSize_ == 0) {
        memset(buf_.get(), Ctrl::CAN, SEND_CAN_COUNT);
        packetSize_ = SEND_CAN_COUNT;
    }
    packetOffs_ += CHECK(srcStrm_->write(buf_.get(), packetSize_ - packetOffs_));
    if (packetOffs_ == packetSize_) {
        LOG_DEBUG(TRACE, "Sent CAN (%u bytes)", (unsigned)packetSize_);
        return lastError_;
    }
    CHECK(checkTimeout(SEND_TIMEOUT));
    return Status::RUNNING;
}

int XmodemReceiver::flush() {
    size_t n = 0;
    if (packetSize_ > 0) {
        n = std::min(fileSize_ - fileOffs_, packetSize_ - sizeof(PacketHeader) - sizeof(PacketCrc));
        fileOffs_ += CHECK(destStrm_->write(buf_.get() + sizeof(PacketHeader), n));
        packetSize_ = 0;
    }
    return n;
}

int XmodemReceiver::checkTimeout(unsigned timeout) {
    if (util::millis() - stateTime_ >= timeout) {
        LOG_DEBUG(TRACE, "Timeout; state: %d", (int)state_);
        return RESULT_TIMEOUT;
    }
    return 0;
}

int XmodemReceiver::checkPacketTimeout() {
    const int ret = checkTimeout(PACKET_TIMEOUT);
    if (ret < 0) {
        setError(ret);
    }
    return ret;
}

void XmodemReceiver::setState(State state, bool restartTimer) {
    state_ = state;
    if (restartTimer) {
        stateTime_ = util::millis();
    }
}

void XmodemReceiver::setError(int error) {
    lastError_ = error;
    packetSize_ = 0;
    packetOffs_ = 0;
    setState(State::SEND_CAN);
}

} // particle
