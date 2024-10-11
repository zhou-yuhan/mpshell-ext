#include "rate_delay_queue.hh"

using namespace std;

void RateDelayQueue::write_packets( FileDescriptor & fd ) {
    /* Move packets from link_queue_ into delay_queue_ */
    string next_packet = link_queue_.get_next();
    while ( not next_packet.empty() ) {
        delay_queue_.read_packet( next_packet );
        next_packet = link_queue_.get_next();
    }

    /* Write out packets from delay_queue_ into fd */
    delay_queue_.write_packets( fd );
}
