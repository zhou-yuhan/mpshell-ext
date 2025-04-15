#include "rate_delay_queue.hh"

#include "util.hh"

using namespace std;

void RateDelayQueue::write_packets(FileDescriptor& fd) {
    /* Move packets from link_queue_ into delay_queue_ */
    string next_packet = link_queue_.get_next();
    while (not next_packet.empty()) {
        delay_queue_.read_packet(next_packet);
        // parse flow id (port) to emulate sk_wmem_alloc
        uint16_t port = 0;
        uint32_t ip = 0;
        if (flow_mem_stats_ && packet_info(next_packet, &port, &ip) == 0 &&
            port != 0) {
            auto it = flow_mem_stats_->find(port);
            assert(it != flow_mem_stats_->end());
            it->second.bytes -= next_packet.size();
            it->second.pkts -= 1;
        }

        next_packet = link_queue_.get_next();
        link_deq_pkts_++;
    }

    /* Write out packets from delay_queue_ into fd */
    delay_queue_.write_packets(fd);
}
