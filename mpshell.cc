/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <fstream>

#include "packetshell.hh"
#include "json.hh"

using json = nlohmann::json;
using namespace std;

int main( int argc, char *argv[] )
{
    try {
        /* clear environment while running as root */
        char ** const user_environment = environ;
        environ = nullptr;

        check_requirements( argc, argv );

        if ( argc < 3 ) {
            throw Exception( "Usage", string( argv[ 0 ] ) + " config_file program" );
        }

        std::vector<uint64_t> delays;
        std::vector<std::string> uplinks;
        std::vector<std::string> downlinks;
        std::vector<json> queue_params;
        std::string log_file = "";
        get_config(argv[1], delays, uplinks, downlinks, queue_params, log_file);
        int if_num = delays.size();

        vector< string > program_to_run;
        for ( int num_args = 2; num_args < argc; num_args++ ) {
            program_to_run.emplace_back( string( argv[ num_args ] ) );
        }

        PacketShell mp_shell_app( "egress", if_num);

        mp_shell_app.start_uplink( "[ mpshell ] ",
                                   user_environment,
                                   delays,
                                   uplinks,
                                   queue_params,
                                   log_file,
                                   program_to_run);

        mp_shell_app.start_downlink( delays, downlinks, queue_params, log_file);
        return mp_shell_app.wait_for_exit();
    } catch ( const Exception & e ) {
        e.perror();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
