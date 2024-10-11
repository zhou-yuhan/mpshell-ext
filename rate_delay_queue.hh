/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef RATE_DELAY_QUEUE_HH
#define RATE_DELAY_QUEUE_HH

#include <string>

#include "delay_queue.hh"
#include "link_queue.hh"

class RateDelayQueue {
    /* Delay Queue followed by link queue in series */
   private:
    DelayQueue delay_queue_;
    LinkQueue link_queue_;

   public:
    RateDelayQueue(const uint64_t& s_delay_ms, const std::string& filename,
                   const std::string& logfile, const bool repeat,
                   std::unique_ptr<AbstractPacketQueue>&& packet_queue)
        : delay_queue_(s_delay_ms),
          link_queue_("link", filename, logfile, repeat, move(packet_queue)) {}

    void read_packet(const std::string& contents) {
        link_queue_.read_packet(contents);
    }

    void write_packets(FileDescriptor& fd);

    int wait_time(void) {
        return std::min(delay_queue_.wait_time(), link_queue_.wait_time());
    }

    bool can_accept_one(unsigned int pkt_size) {
        return link_queue_.can_accept_one(pkt_size);
    }

    unsigned int size_bytes(void) {
        return link_queue_.size_bytes();
    }

    unsigned int size_packets(void) {
        return link_queue_.size_packets();
    }
};

#endif /* RATE_DELAY_QUEUE_HH */
