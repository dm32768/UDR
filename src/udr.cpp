/*****************************************************************************
Copyright 2012 Laboratory for Advanced Computing at the University of Chicago

This file is part of UDR.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions
and limitations under the License.
*****************************************************************************/

#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sstream>
#include <limits.h>
#include <signal.h>
#include <getopt.h>
#include <cctype>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <udt.h>
#include "crypto.h"
#include "cc.h"
#include "udr_threads.h"
#include "udr_util.h"
#include "udr_options.h"
#include "version.h"

using namespace std;

char * get_udr_cmd(UDR_Options * udr_options) {
    ostringstream args;
    if (udr_options->encryption)
        args << "-n " << udr_options->encryption_type << " ";

    args << " -d " << udr_options->timeout << " ";

    if (udr_options->verbose)
        args << "-v";

    if (udr_options->specify_ip)
        args << " -i" << udr_options->specify_ip;

    if (udr_options->bandwidthcap > 0)
        args << " -r " << udr_options->bandwidthcap;

    if (udr_options->mss > 0)
        args << " -m " << udr_options->mss;

    if (udr_options->server_connect)
        args << " -t rsync";
    else
        args << " -a " << udr_options->start_port << " -b " << udr_options->end_port << " -t rsync";

    ostringstream cmd;
    cmd << udr_options->udr_program_dest << " " << args.str() << '\n';
    return strdup(cmd.str().c_str());
}

// 64 hex digits and nothing else.
static bool valid_key_hex(const char * s) {
    if (strlen(s) != HEX_PASSPHRASE_SIZE)
        return false;
    for (int i = 0; i < HEX_PASSPHRASE_SIZE; i++)
        if (!isxdigit((unsigned char) s[i]))
            return false;
    return true;
}

void print_version() {
    fprintf(stderr, "UDR version %s\n", version);
}

//only going to go from local -> remote and remote -> local, remote <-> remote maybe later, but local -> local doesn't make sense for UDR
int main(int argc, char* argv[]) {
    int rsync_arg_idx;

    // if we want this to be C #include <stdbool.h>
    bool use_rsync = false;
    rsync_arg_idx = -1;

    // argv[0] should always be "udr" hence starting at 1
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "rsync") == 0) {
            use_rsync = true;
            rsync_arg_idx = i;
            break;
        }
    }

    if (!use_rsync) {
        rsync_arg_idx = argc;
    }

    //now get the options using udr_options.
    struct UDR_Options curr_options;

    get_udr_options(&curr_options, argc, argv, rsync_arg_idx);

    if (curr_options.version_flag)
        print_version();

    if (!use_rsync)
        usage();

    if (curr_options.tflag) {
        return run_receiver(&curr_options);
    }//now for server mode
    //else if (curr_options.server) {
    //    return run_as_server(&curr_options);
    //}

    else if (curr_options.sflag) {
        string arguments = "";
        string sep = " ";
        char** rsync_args = &argv[rsync_arg_idx];
        int rsync_argc = argc - rsync_arg_idx;
        char hex_pp[HEX_PASSPHRASE_SIZE+1];
        unsigned char passphrase[PASSPHRASE_SIZE+1];

        // The per-transfer secret, made by the receiver and handed over ssh,
        // reaches this process through rsync's environment. It authenticates
        // this end to the receiver, and keys the cipher when -n is on.
        const char * env_key = getenv("UDR_KEY");
        if (env_key == NULL || !valid_key_hex(env_key)) {
            fprintf(stderr, "UDR ERROR: UDR_KEY is not set to the %d-character key\n", HEX_PASSPHRASE_SIZE);
            exit(EXIT_FAILURE);
        }
        memcpy(hex_pp, env_key, HEX_PASSPHRASE_SIZE + 1);
        for (int i = 0; i < PASSPHRASE_SIZE; i++) {
            unsigned int c;
            sscanf(&hex_pp[2 * i], "%02x", &c);
            passphrase[i] = (unsigned char) c;
        }
        passphrase[PASSPHRASE_SIZE] = '\0';

        snprintf(curr_options.host, PATH_MAX, "%s", argv[rsync_arg_idx - 1]);

        if (curr_options.verbose)
            fprintf(stderr, "%s Host: %s\n", curr_options.which_process, curr_options.host);

        for (int i = 0; i < rsync_argc; i++) {
            if (curr_options.verbose)
                fprintf(stderr, "%s rsync arg[%d]: %s\n", curr_options.which_process, i, rsync_args[i]);

            //hack for when no directory is specified -- because strtok is lame, probably should write own tokenizer, but this will do for now
            if (strlen(rsync_args[i]) == 0)
                arguments += ".";
            else
                arguments += rsync_args[i];

            arguments += sep;
        }

        run_sender(&curr_options, hex_pp, passphrase, arguments.c_str(), rsync_argc, rsync_args);

        if (curr_options.verbose)
            fprintf(stderr, "%s run_sender done\n", curr_options.which_process);
    }
    else {
        //get the host and username first
        get_host_username(&curr_options, argc, argv, rsync_arg_idx);

	char * udr_cmd = get_udr_cmd(&curr_options);
        if (curr_options.verbose){
            fprintf(stderr, "%s udr_cmd %s\n", curr_options.which_process, udr_cmd);
        }

        int line_size = NI_MAXSERV + PASSPHRASE_SIZE * 2 + 2;
        char * line = (char*) malloc(line_size);
        line[0] = '\0';

        /* if given double colons then use the server connection: curr_options.server_connect, curr_options.server is for the udr server */
        if (curr_options.server_connect) {
            if(curr_options.verbose){
                fprintf(stderr, "%s trying server connection\n", curr_options.which_process);
            }

            int server_exists = get_server_connection(curr_options.host, curr_options.server_port, udr_cmd, line, line_size);

            if (!server_exists) {
                fprintf(stderr, "UDR ERROR: Cannot connect to server at %s:%s\n", curr_options.host, curr_options.server_port);
                exit(EXIT_FAILURE);
            }
        }
        /* If not try ssh */
        else {
            char ssh_port_str[15];
            int sshchild_to_parent, sshparent_to_child;
            int nbytes;

            int ssh_argc;
            if (strlen(curr_options.username) != 0)
                ssh_argc = 8;
            else
                ssh_argc = 7;

            char ** ssh_argv;
            ssh_argv = (char**) malloc(sizeof (char *) * ssh_argc);
            int ssh_idx = 0;

            ssh_argv[ssh_idx++] = curr_options.ssh_program;

            // Add ssh port
            sprintf(ssh_port_str, "%d", curr_options.ssh_port);
            ssh_argv[ssh_idx++] = (char *) "-p";
            ssh_argv[ssh_idx++] = ssh_port_str;

            if (strlen(curr_options.username) != 0) {
                ssh_argv[ssh_idx++] = (char *) "-l";
                ssh_argv[ssh_idx++] = curr_options.username;
            }

            ssh_argv[ssh_idx++] = curr_options.host;
            ssh_argv[ssh_idx++] = udr_cmd;
            ssh_argv[ssh_idx++] = NULL;

            if (curr_options.verbose) {
                fprintf(stderr, "ssh_program %s\n", curr_options.ssh_program);
                for (int i = 0; i < ssh_idx; i++) {
                    fprintf(stderr, "ssh_argv[%d]: %s\n", i, ssh_argv[i]);
                }
            }

            fork_execvp(curr_options.ssh_program, ssh_argv, &sshparent_to_child, &sshchild_to_parent);

            nbytes = read(sshchild_to_parent, line, line_size-1);
            line[nbytes] = '\0';

            if (curr_options.verbose) {
                fprintf(stderr, "%s Received string: %s\n", curr_options.which_process, line);
            }

            if (nbytes <= 0) {
                fprintf(stderr, "UDR ERROR: unexpected response from server, exiting.\n");
                exit(EXIT_FAILURE);
            }
        }
        /* Now do the exact same thing no matter whether server or ssh process */

        if (strlen(line) == 0) {
            fprintf(stderr, "UDR ERROR: unexpected response from server, exiting.\n");
            exit(EXIT_FAILURE);
        }

        char * port_word = strtok(line, " ");
        if (port_word == NULL) {
            fprintf(stderr, "UDR ERROR: unexpected response from server, exiting.\n");
            exit(EXIT_FAILURE);
        }
        snprintf(curr_options.port_num, sizeof(curr_options.port_num), "%s", port_word);

        char * hex_pp = strtok(NULL, " ");

        if (curr_options.verbose) {
            fprintf(stderr, "%s port_num: %s passphrase: %s\n", curr_options.which_process, curr_options.port_num, hex_pp);
        }

        if (hex_pp != NULL) {
            char * nl = strchr(hex_pp, '\n');
            if (nl != NULL)
                *nl = '\0';
        }
        if (hex_pp == NULL || !valid_key_hex(hex_pp)) {
            fprintf(stderr, "UDR ERROR: the remote udr sent no key; both hosts need udr %s or later\n", version);
            exit(EXIT_FAILURE);
        }
        // rsync inherits the environment and passes it to the sender it
        // starts (the -e program below).
        setenv("UDR_KEY", hex_pp, 1);

        //make sure the port num str is null terminated
        char * ptr;
        if ((ptr = strchr(curr_options.port_num, '\n')) != NULL)
            *ptr = '\0';

        int parent_to_child, child_to_parent;

        //parse the rsync options
        char ** rsync_argv;

        int rsync_argc = argc - rsync_arg_idx + 5; //need more spots
        rsync_argv = (char**) malloc(sizeof (char *) * rsync_argc);

        int rsync_idx = 0;
        rsync_argv[rsync_idx] = (char*) malloc(strlen(argv[rsync_arg_idx]) + 1);
        //ok because just malloc'd based on it
        strcpy(rsync_argv[rsync_idx], argv[rsync_arg_idx]);
        rsync_idx++;

        rsync_argv[rsync_idx++] = (char *) "--blocking-io";

        //rsync_argv[rsync_idx++] = curr_options.rsync_timeout;

        rsync_argv[rsync_idx++] = (char *) "-e";

        // The transport rsync starts: this program as the sender.
        ostringstream rsh;
        rsh << curr_options.udr_program_src;
        if (curr_options.encryption)
            rsh << " -n " << curr_options.encryption_type;
        if (curr_options.verbose)
            rsh << " -v";
        if (curr_options.bandwidthcap > 0)
            rsh << " -r " << curr_options.bandwidthcap;
        if (curr_options.mss > 0)
            rsh << " -m " << curr_options.mss;
        rsh << " -s " << curr_options.port_num;
        rsync_argv[rsync_idx++] = strdup(rsh.str().c_str());

        //fprintf(stderr, "first_source_idx: %d\n", first_source_idx);
        for (int i = rsync_arg_idx + 1; i < argc; i++) {
            rsync_argv[rsync_idx] = (char*) malloc(strlen(argv[i]) + 1);
            rsync_argv[rsync_idx] = argv[i];
            rsync_idx++;
        }

        rsync_argv[rsync_idx] = NULL;

        pid_t local_rsync_pid = fork_execvp(curr_options.rsync_program, rsync_argv, &parent_to_child, &child_to_parent);
        if (curr_options.verbose)
            fprintf(stderr, "%s rsync pid: %d\n", curr_options.which_process, local_rsync_pid);

        //at this point this process should wait for the rsync process to end
        int buf_size = 4096;
        char rsync_out_buf[buf_size];
        int bytes_read;

        //This prints out the stdout from rsync to stdout
        while ((bytes_read = read(child_to_parent, rsync_out_buf, buf_size)) > 0) {
            if (write(STDOUT_FILENO, rsync_out_buf, bytes_read) < 0)
                break;
        }

        int rsync_exit_status;

        do {
            pid_t w = waitpid(local_rsync_pid, &rsync_exit_status, WUNTRACED | WCONTINUED);
            if (w == -1) {
                perror("waitpid");
                exit(EXIT_FAILURE);
            }
        } while (!WIFEXITED(rsync_exit_status) && !WIFSIGNALED(rsync_exit_status));
        exit(WEXITSTATUS(rsync_exit_status));
    }
}
