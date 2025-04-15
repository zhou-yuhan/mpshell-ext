/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "host_queue.hh"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "poller.hh"
#include "timestamp.hh"
#include "util.hh"

using namespace std;
using namespace PollerShortNames;

HostQueue::HostQueue(std::unique_ptr<AbstractPacketQueue>&& qdisc,
                     const uint64_t& s_delay_ms, const std::string& filename,
                     const std::string& logfile, const bool repeat,
                     std::unique_ptr<AbstractPacketQueue>&& nic_packet_queue,
                     int id)
    : qdisc_enq_pkts_(0),
      flow_mem_stats_(),
      flow_deq_stats_(),
      qdisc_(move(qdisc)),
      nic_(s_delay_ms, filename, logfile, repeat, move(nic_packet_queue),
           &flow_mem_stats_),
      id_(id),
      server_fd_(SystemCall("socket", ::socket(AF_UNIX, SOCK_STREAM, 0))),
      client_fds_() {
    /* Create UNIX domain socket to provide host queue status to outside */
    string sock_path = "/tmp/unix-hostqueue-" + to_string(id);
    cleanup(sock_path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, sock_path.c_str());

    SystemCall("bind",
               ::bind(server_fd_.num(), (struct sockaddr*)&addr, sizeof(addr)));
    SystemCall("listen", ::listen(server_fd_.num(), 1));
}

HostQueue::~HostQueue() {
    SystemCall("close", server_fd_.num());
    string sock_path = "/tmp/unix-hostqueue-" + to_string(id_);
    cleanup(sock_path);
}

void HostQueue::cleanup(string& path) {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) == 0 && S_ISSOCK(buffer.st_mode)) {
        SystemCall("unlink", ::unlink(path.c_str()));
    }
}

void HostQueue::transmit(void) {
    while (!qdisc_->empty()) {
        if (nic_.can_accept_one(qdisc_->front().contents.size())) {
            std::string packet = qdisc_->dequeue().contents;
            nic_.read_packet(packet);
            // parse flow id (port) to emulate raw socket
            uint16_t port = 0;
            uint32_t ip = 0;
            if (packet_info(packet, &port, &ip) == 0 && port != 0) {
                auto it = flow_deq_stats_.find(port);
                if (it == flow_deq_stats_.end()) {
                    flow_deq_stats_[port] = FlowStats(packet.size(), 1);
                } else {
                    it->second.bytes += packet.size();
                    it->second.pkts += 1;
                }
            }
        } else {
            break;
        }
    }
}

void HostQueue::read_packet(const std::string& contents) {
    const uint64_t now = timestamp();

    if (contents.size() > PACKET_SIZE) {
        throw runtime_error("packet size is greater than maximum");
    }

    /* drain qdisc packets if possible */
    transmit();

    unsigned int bytes_before = qdisc_->size_bytes();
    unsigned int packets_before = qdisc_->size_packets();

    qdisc_->enqueue(QueuedPacket(contents, now));

    assert(qdisc_->size_packets() <= packets_before + 1);
    assert(qdisc_->size_bytes() <= bytes_before + contents.size());

    qdisc_enq_pkts_++;
    // parse flow id (port) to emulate sk_wmem_alloc
    uint16_t port = 0;
    uint32_t ip = 0;
    if (packet_info(contents, &port, &ip) == 0 && port != 0) {
        auto it = flow_mem_stats_.find(port);
        if (it == flow_mem_stats_.end()) {
            flow_mem_stats_[port] = FlowStats(contents.size(), 1);
        } else {
            it->second.bytes += contents.size();
            it->second.pkts += 1;
        }
    } else {
        fprintf(stderr, "Fail to parse packet");
    }
}

void HostQueue::new_connection(Poller& poller) {
    int client_sock =
        SystemCall("accept", accept(server_fd_.num(), nullptr, nullptr));
    client_fds_.push_back(
        unique_ptr<FileDescriptor>(new FileDescriptor(client_sock)));
    FileDescriptor& fd = *client_fds_.back();

    poller.add_action(Poller::Action(fd, Direction::In, [&]() {
        respond(fd);
        if (fd.eof()) return ResultType::Cancel;
        return ResultType::Continue;
    }));
}

void HostQueue::respond_qdisc_deq(FileDescriptor& fd) {
    std::string buf;
    size_t flow_num = flow_deq_stats_.size();
    buf.append(std::string((char*)&flow_num, sizeof(flow_num)));
    for (auto it = flow_deq_stats_.begin(); it != flow_deq_stats_.end(); ++it) {
        // lsquic accepts array [port, bytes, pkts] of flow_num size
        buf.append(std::string((char*)&it->first, sizeof(it->first)));
        buf.append(std::string((char*)&it->second.bytes, sizeof(it->second.bytes)));
        buf.append(std::string((char*)&it->second.pkts, sizeof(it->second.pkts)));
    }
    fd.write(buf);
    // clear stats, as we record stats between each request
    flow_deq_stats_.clear();
}

void HostQueue::respond_qdisc_size(FileDescriptor& fd) {
    uint64_t bytes = qdisc_->size_bytes();
    uint64_t pkts = qdisc_->size_packets();
    uint8_t buf[sizeof(uint64_t) * 2];
    int pos = 0;
    memcpy(buf + pos, &bytes, sizeof(bytes));
    pos += sizeof(bytes);
    memcpy(buf + pos, &pkts, sizeof(pkts));
    pos += sizeof(pkts);
    fd.write_buf(buf, pos);
}

void HostQueue::respond_sock_mem(FileDescriptor& fd, uint16_t port) {
    uint64_t bytes = 0;
    auto it = flow_mem_stats_.find(port);
    if (it != flow_mem_stats_.end()) {
        bytes = it->second.bytes;
    }
    fd.write_buf(&bytes, sizeof(bytes));
}

void HostQueue::respond_full_info(FileDescriptor& fd) {
    QueueStatus status = get_queue_status();
    uint8_t buf[sizeof(QueueStatus)];
    int pos = 0;
    memcpy(buf + pos, &status.timestamp, sizeof(status.timestamp));
    pos += sizeof(status.timestamp);
    memcpy(buf + pos, &status.nic_bytes, sizeof(status.nic_bytes));
    pos += sizeof(status.nic_bytes);
    memcpy(buf + pos, &status.nic_packets, sizeof(status.nic_packets));
    pos += sizeof(status.nic_packets);
    memcpy(buf + pos, &status.nic_enq_pkts, sizeof(status.nic_enq_pkts));
    pos += sizeof(status.nic_enq_pkts);
    memcpy(buf + pos, &status.nic_deq_pkts, sizeof(status.nic_deq_pkts));
    pos += sizeof(status.nic_deq_pkts);
    memcpy(buf + pos, &status.qdisc_bytes, sizeof(status.qdisc_bytes));
    pos += sizeof(status.qdisc_bytes);
    memcpy(buf + pos, &status.qdisc_packets, sizeof(status.qdisc_packets));
    pos += sizeof(status.qdisc_packets);
    memcpy(buf + pos, &status.qdisc_enq_pkts, sizeof(status.qdisc_enq_pkts));
    pos += sizeof(status.qdisc_enq_pkts);
    fd.write_buf(buf, pos);

    reset_queue_inout();
}

void HostQueue::respond(FileDescriptor& fd) {
    /** read client request from cliet fd.
     * Currently we don't care about the format of client request,
     * we report queue status as long as clients send us a message
     */
    std::string req = fd.read();
    uint16_t port;
    // FIXME: handle client close
    switch ((uint8_t)req[0]) {
        case 0:  // Qdisc dequeue <=> raw socket capture
            respond_qdisc_deq(fd);
            break;
        case 1: // Qdisc size <=> rtnetlink qdisc info
            respond_qdisc_size(fd);
            break;
        case 2: // per flow Qdisc + NIC memory <=> sk_wmem_alloc
            port = *(uint16_t*)&req.data()[1];
            respond_sock_mem(fd, port);
            break;
        case 3:  // Qdisc + NIC info, only for debugging
            respond_full_info(fd);
            break;
        default:
            fprintf(stderr, "invalid request %u\n", (uint8_t)req[0]);
            throw runtime_error("invalid request");
    }
}