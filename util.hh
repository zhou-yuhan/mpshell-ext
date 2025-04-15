/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef UTIL_HH
#define UTIL_HH

#include <string>
#include <cstring>
#include <vector>
#include <memory>

#include "address.hh"
#include "poller.hh"
#include "signalfd.hh"
#include "child_process.hh"
#include "abstract_packet_queue.hh"
#include "json.hh"

std::string shell_path( void );
void drop_privileges( void );
void check_requirements( const int argc, const char * const argv[] );
bool check_folder_existence( const std::string & directory );
void check_storage_folder( const std::string & directory );
Address first_nameserver( void );
std::vector< Address > all_nameservers( void );
void list_files( const std::string & dir, std::vector< std::string > & files );
void prepend_shell_prefix( const std::string & str );
template <typename T> void zero( T & x ) { memset( &x, 0, sizeof( x ) ); }

PollerShortNames::Result handle_signal( const signalfd_siginfo & sig,
                                        std::vector<ChildProcess> & child_processes );

void assert_not_root( void );
std::unique_ptr<AbstractPacketQueue> get_packet_queue(const nlohmann::json & params);
void get_config(const char * filename, std::vector<uint64_t> & delays, std::vector<std::string> & uplinks, std::vector<std::string> & downlinks, std::vector<nlohmann::json> & queue_params, std::string & log_file);
void hexdump(const char *desc, const uint8_t *data, int len);
int packet_info(const std::string &packet, uint16_t *src_port, uint32_t *src_ip);

struct FlowStats {
   public:
    uint64_t bytes;
    uint64_t pkts;
    FlowStats() : bytes(0), pkts(0) {}
    FlowStats(uint64_t b, uint64_t p) : bytes(b), pkts(p) {}
};

#endif /* UTIL_HH */