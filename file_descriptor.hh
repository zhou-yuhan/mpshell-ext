/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef FILE_DESCRIPTOR_HH
#define FILE_DESCRIPTOR_HH

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/ethernet.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <unistd.h>

#include "exception.hh"
#include "ezio.hh"

class FileDescriptor {
   private:
    int fd_;
    bool eof_;
    std::string buffer_;

    std::string extract_complete_packet() {
        size_t packet_length;
        uint8_t version = buffer_[0] >> 4;

        fprintf(stderr, "Version: %x\n", buffer_[0]);
        if (version == 4) {
            if (buffer_.length() < sizeof(struct iphdr)) {
                return std::string();
            }
            struct iphdr* ip_header = (struct iphdr*)(buffer_.data());
            size_t ip_header_length = ip_header->ihl * 4;
            packet_length = ntohs(ip_header->tot_len);
            fprintf(stderr,
                    "V4 Header length: %ld Packet length: %ld Source IP: %s\n",
                    ip_header_length, packet_length,
                    inet_ntoa(*(struct in_addr*)&ip_header->saddr));
        } else if (version == 6) {
            if (buffer_.length() < sizeof(struct ip6_hdr)) {
                return std::string();
            }
            struct ip6_hdr* ipv6_header = (struct ip6_hdr*)(buffer_.data());
            packet_length =
                ntohs(ipv6_header->ip6_plen) + sizeof(struct ip6_hdr);
            fprintf(stderr, "V6 Packet length: %ld\n", packet_length);
        } else {
            throw Exception("Unexpected IP version");
        }

        if (buffer_.length() < packet_length) {
            return std::string();
        }

        std::string complete_packet = buffer_.substr(0, packet_length);

        buffer_.erase(0, complete_packet.length());

        return complete_packet;
    }

   public:
    FileDescriptor(const int s_fd) : fd_(s_fd), eof_(false), buffer_() {
        if (fd_ <= 2) { /* make sure not overwriting stdout/stderr */
            throw Exception("FileDescriptor", "fd <= 2");
        }

        /* set close-on-exec flag so our file descriptors
           aren't passed on to unrelated children (like a shell) */
        SystemCall("fcntl FD_CLOEXEC", fcntl(fd_, F_SETFD, FD_CLOEXEC));
    }

    ~FileDescriptor() {
        if (fd_ < 0) { /* has already been moved away */
            return;
        }

        SystemCall("close", close(fd_));
    }

    const int& num(void) { return fd_; }
    const bool& eof(void) const { return eof_; }

    /* forbid copying FileDescriptor objects or assigning them */
    FileDescriptor(const FileDescriptor& other) = delete;
    const FileDescriptor& operator=(const FileDescriptor& other) = delete;

    /* allow moving FileDescriptor objects */
    FileDescriptor(FileDescriptor&& other) : fd_(other.fd_), eof_(other.eof_), buffer_(other.buffer_) {
        other.fd_ = -1; /* disable the other FileDescriptor */
    }

    void write(const std::string& buffer) { writeall(num(), buffer); }

    int write_buf(const void* buf, size_t len) {
        return ::write(num(), buf, len);
    }

    std::string::const_iterator write_some(
        const std::string::const_iterator& begin,
        const std::string::const_iterator& end) {
        return ::write_some(num(), begin, end);
    }

    std::string read(void) {
        auto ret = readall(num());
        if (ret.empty()) {
            eof_ = true;
        }
        return ret;
    }

    std::string read(const size_t limit) {
        auto ret = readall(num(), limit);
        if (ret.empty()) {
            eof_ = true;
        }
        return ret;
    }

    std::string read_packet(void) {
        char read_buffer[ezio::read_chunk_size];

        ssize_t bytes_read = ::read(num(), read_buffer, ezio::read_chunk_size);
        if (bytes_read == 0) {
            // End of file
            fprintf(stderr, "End of file\n");
            eof_ = true;
            return std::string();
        } else if (bytes_read < 0) {
            throw Exception("read");
        }

        fprintf(stderr, "Read %ld bytes\n", bytes_read);

        buffer_ += std::string(read_buffer, bytes_read);

        std::string packet = extract_complete_packet();
        if (!packet.empty()) {
            fprintf(stderr, "Extracted packet with length %ld\n",
                    packet.length());
            return packet;
        }
        return std::string();
    }

    void set_eof(void) { eof_ = true; }
};

#endif /* FILE_DESCRIPTOR_HH */
