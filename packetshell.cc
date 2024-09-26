/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <utility>
#include <memory>

#include <sys/socket.h>
#include <net/route.h>
#include <signal.h>

#include "packetshell.hh"
#include "rate_delay_queue.hh"
#include "netdevice.hh"
#include "nat.hh"
#include "util.hh"
#include "get_address.hh"
#include "address.hh"
#include "make_pipe.hh"
#include "json.hh"

#include "ferry.cc"

using namespace std;
using namespace PollerShortNames;
using json = nlohmann::json;

PacketShell::PacketShell( const std::string & device_prefix, int if_num )
    : egress_ingress( get_unassigned_address_pairs(if_num) ),
      nameserver_( first_nameserver() ),
      egress_tuns_(std::vector<TunDevice>()),
      dns_outside_(),
      nats_(),
      pipes_(),
      child_processes_()
{
    /* make sure environment has been cleared */
    assert( environ == nullptr );

    for (int i = 0; i < if_num; ++i) {
        egress_tuns_.emplace_back(device_prefix + to_string(i), egress_ingress.at(i).first, egress_ingress.at(i).second);
        nats_.emplace_back(std::unique_ptr<NAT>(new NAT(egress_ingress.at(i).second)));
        pipes_.emplace_back(make_pipe());
    }
    dns_outside_ = std::unique_ptr<DNSProxy>(new DNSProxy(egress_ingress.at(0).first, nameserver_, nameserver_));
}

void PacketShell::start_uplink( const std::string & shell_prefix,
                                char ** const user_environment,
                                const std::vector<uint64_t> & delays,
                                const std::vector<std::string> & uplinks,
                                const std::vector<json> & queue_params,
                                const std::string & log_file,
                                const std::vector< string > & command)
{
    /* Fork */
    child_processes_.emplace_back( [&]() {
            size_t if_num = delays.size();
            std::vector<TunDevice> ingress_tuns;
            for (size_t i = 0; i < if_num; ++i) {
                ingress_tuns.emplace_back("ingress" + to_string(i), egress_ingress.at(i).second, egress_ingress.at(i).first);
            }
            /* bring up localhost */
            Socket ioctl_socket( UDP );
            interface_ioctl( ioctl_socket.fd(), SIOCSIFFLAGS, "lo",
                             [] ( ifreq &ifr ) { ifr.ifr_flags = IFF_UP; } );

            /* create default route through cell and wifi */
            rtentry route;
            zero( route );

            route.rt_gateway = egress_ingress.at(0).first.raw_sockaddr();
            route.rt_dst = route.rt_genmask = Address().raw_sockaddr();
            route.rt_flags = RTF_UP | RTF_GATEWAY;

            SystemCall( "ioctl SIOCADDRT", ioctl( ioctl_socket.fd().num(), SIOCADDRT, &route ) );

            for(size_t i = 1; i < if_num; ++i) {
                /* Policy routing to simulate two default interfaces */
                run( {"/sbin/ip", "route", "add", egress_ingress.at(i).first.ip(), "dev", "ingress" + to_string(i), "src", egress_ingress.at(i).second.ip(), "table", to_string(i)} );
                run( {"/sbin/ip", "route", "add", "default", "via", egress_ingress.at(i).first.ip(), "dev", "ingress" + to_string(i), "table", to_string(i)} );
                run( {"/sbin/ip", "rule", "add", "from", egress_ingress.at(i).second.ip(),"table", to_string(i)} );
                run( {"/sbin/ip", "rule", "add", "to", egress_ingress.at(i).second.ip(), "table", to_string(i)} );
                run( {"/sbin/ip", "route", "flush", "cache"} );
            }

            /* create DNS proxy if nameserver address is local */
            auto dns_inside = DNSProxy::maybe_proxy( nameserver_,
                                                     dns_outside_->udp_listener().local_addr(),
                                                     dns_outside_->tcp_listener().local_addr() );

            /* Fork again after dropping root privileges */
            drop_privileges();

            std::vector<ChildProcess> uplink_processes;
            ChildProcess bash_process( [&]() {
                    /* restore environment and tweak bash prompt */
                    environ = user_environment;
                    prepend_shell_prefix( shell_prefix );

                    const string shell = shell_path();
                    run( command, user_environment );
                    //SystemCall( "execl", execl( shell.c_str(), shell.c_str(), "-c", program_name.c_str(), static_cast<const char*>( nullptr ) ) );
                    return EXIT_FAILURE;
                } );
            uplink_processes.emplace_back(move(bash_process));

            for (size_t i = 0; i < if_num; ++i) {
                ChildProcess link_ferry( [&]() {
                        std::unique_ptr<AbstractPacketQueue> link_queue = get_packet_queue(queue_params.at(i));
                        assert(link_queue);
                        RateDelayQueue rate_delay_queue( delays.at(i), uplinks.at(i), log_file, true, move(link_queue));
                        return packet_ferry( rate_delay_queue, ingress_tuns.at(i).fd(), pipes_.at(i).first, i == 0 ? move( dns_inside ) : nullptr, {} );
                    } );
                uplink_processes.emplace_back(move(link_ferry));
            }

            return wait_on_processes( std::move( uplink_processes ) );
        }, true );  /* new network namespace */
}

void PacketShell::start_downlink( const std::vector<uint64_t> & delays,
                                  const std::vector<std::string> & downlinks,
                                  const std::vector<json> & queue_params,
                                  const std::string & log_file)
{
    size_t if_num = delays.size();
    for (size_t i = 0; i < if_num; ++i) {
        child_processes_.emplace_back( [&] () {
            drop_privileges();
            std::unique_ptr<AbstractPacketQueue> link_queue = get_packet_queue(queue_params.at(i));
            RateDelayQueue rate_delay_queue(delays.at(i), downlinks.at(i), log_file, true, move(link_queue));
            return packet_ferry(rate_delay_queue, egress_tuns_.at(i).fd(), pipes_.at(i).second, i == 0 ? move(dns_outside_) : nullptr, {});
        });
    }

}

int PacketShell::wait_on_processes( std::vector<ChildProcess> && process_vector )
{
    /* wait for either child to finish */
    Poller poller;
    SignalMask signals_to_listen_for = { SIGCHLD, SIGCONT, SIGHUP, SIGTERM };
    signals_to_listen_for.block(); /* don't let them interrupt us */

    SignalFD signal_fd( signals_to_listen_for );

    poller.add_action( Poller::Action( signal_fd.fd(), Direction::In,
                                       [&] () {
                                           return handle_signal( signal_fd.read_signal(),
                                                                 process_vector );
                                       } ) );

    while ( true ) {
        auto poll_result = poller.poll( -1 );

        if ( poll_result.result == Poller::Result::Type::Exit ) {
            return poll_result.exit_status;
        }
    }
}
