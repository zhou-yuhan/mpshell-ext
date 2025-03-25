/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <fstream>
#include <cstring>

#include "packetshell.hh"
#include "json.hh"

using json = nlohmann::json;
using namespace std;

// Helper function to find SHELL in user_environment
const char* find_shell_in_env(char** env) {
    for (; *env != nullptr; env++) {
        if (strncmp(*env, "SHELL=", 6) == 0) {
            return *env + 6;  // Return the value part after "SHELL="
        }
    }
    return "/bin/sh";  // Default to /bin/sh if SHELL is not found
}

int main( int argc, char *argv[] )
{
    try {
        /* clear environment while running as root */
        char ** const user_environment = environ;
        environ = nullptr;

        check_requirements( argc, argv );

        if ( argc < 2 ) {
            throw Exception( "Usage", string( argv[ 0 ] ) + " <config_file> [<program> [<args>...]]" );
        }

        std::vector<uint64_t> delays;
        std::vector<std::string> uplinks;
        std::vector<std::string> downlinks;
        std::vector<json> queue_params;
        std::string log_file = "";
        get_config(argv[1], delays, uplinks, downlinks, queue_params, log_file);
        int if_num = delays.size();

        vector< string > program_to_run;
        if (argc == 2) {
            // If no program is specified, run the default shell
            const char* shell = find_shell_in_env(user_environment);
            program_to_run.emplace_back(string(shell));
        } else {
            // If a program is specified, use it
            for ( int num_args = 2; num_args < argc; num_args++ ) {
                program_to_run.emplace_back( string( argv[ num_args ] ) );
            }
        }

        PacketShell host_shell_app( "egress", if_num);

        host_shell_app.start_uplink( "[ hostshell ] ",
                                   user_environment,
                                   delays,
                                   uplinks,
                                   queue_params,
                                   log_file,
                                   program_to_run);

        host_shell_app.start_downlink( delays, downlinks, queue_params, log_file);
        return host_shell_app.wait_for_exit();
    } catch ( const Exception & e ) {
        e.perror();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
