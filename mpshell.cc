/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "packetshell.hh"

using namespace std;

int main( int argc, char *argv[] )
{
    try {
        /* clear environment while running as root */
        char ** const user_environment = environ;
        environ = nullptr;

        check_requirements( argc, argv );

        if ( argc < 6 ) {
            throw Exception( "Usage", string( argv[ 0 ] ) + " if_num [delay uplink downlink] ... program" );
        }

        const int if_num = myatoi(argv[1]);
        std::vector<uint64_t> delays;
        std::vector<std::string> uplinks;
        std::vector<std::string> downlinks;
        int idx = 2;
        for (int i = 0; i < if_num; ++i) {
            delays.emplace_back(myatoi(argv[idx++]));
            uplinks.emplace_back(argv[idx++]);
            downlinks.emplace_back(argv[idx++]);
        }

        vector< string > program_to_run;
        for ( int num_args = 1 + if_num * 3 + 1; num_args < argc; num_args++ ) {
            program_to_run.emplace_back( string( argv[ num_args ] ) );
        }

        PacketShell mp_shell_app( "egress", if_num);

        mp_shell_app.start_uplink( "[ mpshell ] ",
                                   user_environment,
                                   delays,
                                   uplinks,
                                   program_to_run);

        mp_shell_app.start_downlink( delays, downlinks );
        return mp_shell_app.wait_for_exit();
    } catch ( const Exception & e ) {
        e.perror();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
