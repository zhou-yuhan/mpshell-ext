/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "host_queue.hh"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>

#include "poller.hh"
#include "timestamp.hh"

using namespace std;
using namespace PollerShortNames;

HostQueue::HostQueue(std::unique_ptr<AbstractPacketQueue>&& qdisc,
                     const uint64_t& s_delay_ms, const std::string& filename,
                     const std::string& logfile, const bool repeat,
                     std::unique_ptr<AbstractPacketQueue>&& nic_packet_queue,
                     int id)
    : qdisc_(move(qdisc)),
      nic_(s_delay_ms, filename, logfile, repeat, move(nic_packet_queue)),
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
            nic_.read_packet(qdisc_->dequeue().contents);
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
}

void HostQueue::new_connection(Poller& poller) {
    int client_sock =
        SystemCall("accept", accept(server_fd_.num(), nullptr, nullptr));
    client_fds_.push_back(unique_ptr<FileDescriptor>(new FileDescriptor(client_sock)));
    FileDescriptor& fd = *client_fds_.back();

    poller.add_action(Poller::Action(fd, Direction::In, [&]() {
        respond(fd);
        /* client EOF is handled by poller.cc:28 */
        return ResultType::Continue;
    }));

}

void HostQueue::respond(FileDescriptor& fd) {
    /** read client request from cliet fd.
     * Currently we don't care about the format of client request,
     * we report queue status as long as clients send us a message
     */
    fd.read();
    // FIXME: handle client close

    QueueStatus status = get_queue_status();
    fd.write(status.serialize());
}