/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef HOST_QUEUE_HH
#define HOST_QUEUE_HH

#include <memory>

#include "drop_tail_packet_queue.hh"
#include "json.hh"
#include "poller.hh"
#include "rate_delay_queue.hh"
#include "timestamp.hh"
#include "util.hh"

using json = nlohmann::json;

class HostQueue {
    /** HostQueue consists of an AbstractPacketQueue acting as Qdisc and a
     * RateDelayQueue acting as NIC/driver bottleneck */
   private:
    uint64_t qdisc_enq_pkts_;
    std::unordered_map<uint16_t, FlowStats>
        flow_mem_stats_;  // emulate sk_wmem_alloc for each flow
    std::unordered_map<uint16_t, FlowStats>
        flow_deq_stats_;  // Qdisc dequeued stats for each flow
    std::unique_ptr<AbstractPacketQueue> qdisc_;
    RateDelayQueue nic_;

    int id_; /* HostQueue id */
    FileDescriptor server_fd_;
    /* NOTE: do not use vector<FileDescriptor>
     * or some nasty memory problems happen */
    std::vector<std::unique_ptr<FileDescriptor>> client_fds_;

    /* Analogy to ndo_start_xmit() in Linux kernel, transmit packets from netdev
     * qdisc to NIC driver */
    void transmit(void);

    void respond_qdisc_deq(FileDescriptor& fd);
    void respond_qdisc_size(FileDescriptor& fd);
    void respond_sock_mem(FileDescriptor& fd, uint16_t port);
    void respond_full_info(FileDescriptor& fd);

    void respond(FileDescriptor& fd);

    struct QueueStatus {
        uint64_t timestamp;
        uint64_t qdisc_bytes;
        uint64_t qdisc_packets;
        uint64_t qdisc_enq_pkts;
        uint64_t nic_bytes;
        uint64_t nic_packets;
        uint64_t nic_enq_pkts;
        uint64_t nic_deq_pkts;
        QueueStatus(uint64_t ts, uint64_t qb, uint64_t qp, uint64_t qen,
                    int64_t nb, uint64_t np, uint64_t nen, uint64_t nde)
            : timestamp(ts),
              qdisc_bytes(qb),
              qdisc_packets(qp),
              qdisc_enq_pkts(qen),
              nic_bytes(nb),
              nic_packets(np),
              nic_enq_pkts(nen),
              nic_deq_pkts(nde) {}
        std::string serialize(void) const {
            json j = {
                {"timestamp", timestamp},
                {"qdisc_bytes", qdisc_bytes},
                {"qdisc_packets", qdisc_packets},
                {"qdisc_enq_pkts", qdisc_enq_pkts},
                {"nic_bytes", nic_bytes},
                {"nic_packets", nic_packets},
                {"nic_enq_pkts", nic_enq_pkts},
                {"nic_deq_pkts", nic_deq_pkts},
            };
            return j.dump();
        }
    };

    QueueStatus get_queue_status(void) {
        return QueueStatus(timestamp(), qdisc_->size_bytes(),
                           qdisc_->size_packets(), qdisc_enq_pkts_,
                           nic_.size_bytes(), nic_.size_packets(),
                           nic_.enq_pkts(), nic_.deq_pkts());
    }

    void cleanup(std::string& path);

   public:
    const static unsigned int PACKET_SIZE =
        1504; /* default max TUN payload size */

    /* AQM schemes of Qdisc and NIC are constructed outside of HostQueue
     * construction */
    HostQueue(std::unique_ptr<AbstractPacketQueue>&& qdisc,
              const uint64_t& s_delay_ms, const std::string& filename,
              const std::string& logfile, const bool repeat,
              std::unique_ptr<AbstractPacketQueue>&& nic_packet_queue, int id);

    ~HostQueue(void);

    void read_packet(const std::string& contents);

    void write_packets(FileDescriptor& fd) {
        transmit();
        nic_.write_packets(fd);
    }

    unsigned int wait_time(void) {
        transmit();
        return nic_.wait_time();
    }

    FileDescriptor& server_fd(void) { return server_fd_; }

    void new_connection(Poller& poller);

    void reset_queue_inout(void) {
        nic_.reset_queue_inout();
        qdisc_enq_pkts_ = 0;
    }
};

#endif /* HOST_QUEUE_HH */
