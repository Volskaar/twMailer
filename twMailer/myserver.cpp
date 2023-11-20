#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

// directory management
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

// files
#include <fstream>

//ldap
#include <ldap.h>

//threading
#include <sys/wait.h>

using namespace std;

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;

///////////////////////////////////////////////////////////////////////////////

struct comm_args{
    int socket;
    string clientIP;
    int* loginAttempt;
};

//take care of future child processes
int childCount = 0;
pid_t child_pids[256];

///////////////////////////////////////////////////////////////////////////////

void clientCommunication(comm_args args);
void signalHandler(int sig);

///////////////////////////////////////////////////////////////////////////////

int main(void) {
    socklen_t addrlen;
    struct sockaddr_in address, cliaddress;
    int reuseValue = 1;
    string clientIP;
    int loginAttempt = 0;

    ////////////////////////////////////////////////////////////////////////////
    // SIGNAL HANDLER
    // SIGINT (Interrup: ctrl+c)
    // https://man7.org/linux/man-pages/man2/signal.2.html
    if (signal(SIGINT, signalHandler) == SIG_ERR) {
        perror("signal can not be registered");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // CREATE A SOCKET
    // https://man7.org/linux/man-pages/man2/socket.2.html
    // https://man7.org/linux/man-pages/man7/ip.7.html
    // https://man7.org/linux/man-pages/man7/tcp.7.html
    // IPv4, TCP (connection oriented), IP (same as client)
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket error"); // errno set by socket()
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // SET SOCKET OPTIONS
    // https://man7.org/linux/man-pages/man2/setsockopt.2.html
    // https://man7.org/linux/man-pages/man7/socket.7.html
    // socket, level, optname, optvalue, optlen
    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &reuseValue,
                   sizeof(reuseValue)) == -1) {
        perror("set socket options - reuseAddr");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEPORT, &reuseValue,
                   sizeof(reuseValue)) == -1) {
        perror("set socket options - reusePort");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // INIT ADDRESS
    // Attention: network byte order => big endian
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    ////////////////////////////////////////////////////////////////////////////
    // ASSIGN AN ADDRESS WITH PORT TO SOCKET
    if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind error");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // ALLOW CONNECTION ESTABLISHING
    // Socket, Backlog (= count of waiting connections allowed)
    if (listen(create_socket, 5) == -1) {
        perror("listen error");
        return EXIT_FAILURE;
    }

    while (!abortRequested) {
        /////////////////////////////////////////////////////////////////////////
        // ignore errors here... because only information message
        // https://linux.die.net/man/3/printf
        printf("Waiting for connections...\n");

        /////////////////////////////////////////////////////////////////////////
        // ACCEPTS CONNECTION SETUP
        // blocking, might have an accept-error on ctrl+c
        addrlen = sizeof(struct sockaddr_in);
        if ((new_socket = accept(create_socket, (struct sockaddr *)&cliaddress,
                                 &addrlen)) == -1) {
            if (abortRequested) {
                perror("accept error after aborted");
            } else {
                perror("accept error");
            }
            break;
        }

        /////////////////////////////////////////////////////////////////////////
        // FORKING

        pid_t pid = fork();
        child_pids[childCount] = pid;
        childCount++;

        if(pid < 0){
            perror("fork failed");
            return EXIT_FAILURE;
        }
        else if(pid == 0){
            close(create_socket);
            printf("Child process created!\n");
            comm_args args = {
                    new_socket,
                    inet_ntoa(cliaddress.sin_addr),
                    &loginAttempt
            };

            clientCommunication(args);

            exit(EXIT_SUCCESS);
        }
        else{
            close(new_socket);
        }

        new_socket = -1;
    }

    // frees the descriptor
    if (create_socket != -1) {
        if (shutdown(create_socket, SHUT_RDWR) == -1) {
            perror("shutdown create_socket");
        }
        if (close(create_socket) == -1) {
            perror("close create_socket");
        }
        create_socket = -1;
    }

    /////////////////////////////////////////////////////////////////////////
    // WAIT FOR CHILD PROCESSES

    for (int i = 0; i < childCount; i++) {
        int status;
        pid_t terminated_pid = waitpid(child_pids[i], &status, 0);

        //check if child process terminated normally
        if (WIFEXITED(status)) {
            int exit_status = WEXITSTATUS(status);

            if (exit_status == 0) {
                printf("Child process %d terminated successfully.\n", terminated_pid);
            } else {
                printf("Child process %d terminated with an error.\n", terminated_pid);
            }
        } else {
            printf("Child process %d terminated abnormally.\n", terminated_pid);
        }
    }

    //while(wait(NULL) > 0);

    return EXIT_SUCCESS;
}

void clientCommunication(comm_args args) {
    char buffer[BUF];
    int size;

    int* current_socket = &args.socket;
    string clientIP = (string) args.clientIP;
    int* loginAttempt = (int*) args.loginAttempt;


    bool loggedIn = false;
    string username;

    /////////////////////////////////////////////////////////////////////////////

    // LDAP config
    // anonymous bind with user and pw empty
    const char *ldapUri = "ldap://ldap.technikum-wien.at:389";
    const int ldapVersion = LDAP_VERSION3;

    int rc = 0; // return code

    // setup LDAP connection
    LDAP *ldapHandle;

    rc = ldap_initialize(&ldapHandle, ldapUri);

    if (rc != LDAP_SUCCESS)
    {
        fprintf(stderr, "ldap_init failed\n");
    }

    printf("connected to LDAP server %s\n", ldapUri);

    // set verison options
    rc = ldap_set_option(ldapHandle, LDAP_OPT_PROTOCOL_VERSION, &ldapVersion);             // IN-Value

    if (rc != LDAP_OPT_SUCCESS){
        fprintf(stderr, "ldap_set_option(PROTOCOL_VERSION): %s\n", ldap_err2string(rc));
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
    }

    // start connection secure (initialize TLS)
    rc = ldap_start_tls_s(ldapHandle, NULL, NULL);

    if (rc != LDAP_SUCCESS){
        fprintf(stderr, "ldap_start_tls_s(): %s\n", ldap_err2string(rc));
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
    }

    ////////////////////////////////////////////////////////////////////////////
    // SEND welcome message
    strcpy(buffer, "Welcome to myserver!\r\nPlease enter your commands...\r\n");
    if (send(*current_socket, buffer, strlen(buffer), 0) == -1) {
        perror("send failed");
        //return NULL;
    }

    do {
        /////////////////////////////////////////////////////////////////////////
        // RECEIVE
        size = recv(*current_socket, buffer, BUF - 1, 0);
        if (size == -1) {
            if (abortRequested) {
                perror("recv error after aborted");
            } else {
                perror("recv error");
            }
            break;
        }

        if (size == 0) {
            printf("Client closed remote socket\n"); // ignore error
            break;
        }
        buffer[size] = '\0';

        /////////////////////////////////////////////////////////////////////////
        // SPLIT INPUT

        vector<string> input;
        string current;

        for (int i = 0; i < size; i++) {
            if (buffer[i] != '\n') {
                current += buffer[i];
            } else {
                input.push_back(current);
                current = "";
            }
        }
        if(current[0]){
            input.push_back(current);
            current = "";
        }

        int inputSize = input.size();

        /////////////////////////////////////////////////////////////////////////
        // login command

        // 1. username and password
        // 2. respond with OK or ERR
        // 3. enable all other commands for the running session
        // 4. allow only 3 attempts
        // 4.1. blacklist ip after 3 failed attempts for 1min

        bool blacklisted = false;

        if (input[0] == "LOGIN") {
            cout << input[1] << endl;
            cout << input[2] << endl;

            string output = "";

            string blacklistPath = "..";
            DIR *dirPointer = opendir(blacklistPath.c_str());

            string fPath = blacklistPath + "/blacklist.txt";

            // open file in reader
            ifstream blacklist(fPath);

            if (blacklist) {
                string line = "";

                // handle lines
                while (getline(blacklist, line)) {
                    if (line == clientIP){
                        blacklisted = true;
                    }
                }

                // close file
                blacklist.close();
            } else {
                cout << "Unable to open file" << endl;
                output = "ERR\n";
            }
            closedir(dirPointer);

            if (blacklisted) {
                printf("Invalid LOGIN command.\n");
                output = "ERR\n";
            } else {
                // bind credentials
                char ldapBindUser[256];
                char ldapBindPassword[256];
                char rawLdapUser[128];

                strcpy(rawLdapUser, input[1].c_str());
                sprintf(ldapBindUser, "uid=%s,ou=people,dc=technikum-wien,dc=at", rawLdapUser);
                strcpy(ldapBindPassword, input[2].c_str());

                username = rawLdapUser;

                BerValue bindCredentials;
                bindCredentials.bv_val = (char *)ldapBindPassword;
                bindCredentials.bv_len = strlen(ldapBindPassword);
                BerValue *servercredp; // server's credentials

                cout << rawLdapUser << endl;
                cout << ldapBindUser << endl;
                cout << ldapBindPassword << endl;

                rc = ldap_sasl_bind_s(ldapHandle, ldapBindUser, LDAP_SASL_SIMPLE, &bindCredentials, NULL, NULL, &servercredp);

                cout << rc << endl;

                strcpy(ldapBindUser, "");
                strcpy(ldapBindPassword, "");
                strcpy(rawLdapUser, "");

                if (rc != LDAP_SUCCESS){
                    fprintf(stderr, "LDAP bind error: %s\n", ldap_err2string(rc));
                    loggedIn = false;
                    *loginAttempt += 1;
                    cout << "Login attempts: " << *loginAttempt << endl;
                    output = "ERR\n";
                }
                else{
                    loggedIn = true;
                    output = "OK\n";
                }

                if(*loginAttempt >= 3){
                    string inputPath = "..";
                    opendir(inputPath.c_str());

                    string filePath = inputPath + "/blacklist.txt";

                    // open file in reader
                    ofstream file(filePath);

                    if (file) {
                        // add
                        file << clientIP << "\n";

                        // close file
                        file.close();

                        output = "ERR\n";

                    } else {
                        cout << "Unable to open file" << endl;
                        output = "ERR\n";
                    }
                    closedir(dirPointer);
                }
            }

            if(loggedIn){
                printf("Test succeeded\n");
            }

            strcpy(buffer, output.c_str());
        }

            /////////////////////////////////////////////////////////////////////////

        else if (input[0] == "SEND" && loggedIn) {
            string message = input[3];
            message += '\n';
            for(long unsigned int i = 4; i < input.size(); i++){
                message += input[i];
                message += '\n';
            }

            string output = "";
            if (inputSize < 4) {
                printf("Invalid SEND command.\n");
                output = "ERR\n";
            }

            else {
                string sender = username;
                string receiver = input[1];
                string subject = input[2];

                // 1. check if receiver has folder, if not -> create
                string inputPath = "../mail-spooler/" + receiver;
                string messageName = sender + "_" + subject + ".txt";

                DIR *directoryPointer = opendir(inputPath.c_str());

                if (directoryPointer == NULL) {
                    perror("opendir");
                    // Creating a directory if not existing
                    if (mkdir(inputPath.c_str(), 0777) == -1) {
                        cerr << "Error :  " << strerror(errno) << endl;
                        output = "ERR\n";
                    } else {
                        cout << "Directory created \n";
                    }
                }

                // 2. create textfile in correct folder
                ofstream file(inputPath + "/" + messageName);

                // 3. write sender, receiver, subject and message into textfile
                file << sender << "\n";
                file << receiver << "\n";
                file << subject << "\n";
                file << message;

                file.close();
                closedir(directoryPointer);

                if (output != "ERR\n") {
                    output = "OK\n";
                }
            }
            strcpy(buffer, output.c_str());
        }

            /////////////////////////////////////////////////////////////////////////

        else if (input[0] == "LIST" && loggedIn) {

            string output = "";

            if (inputSize < 1) {
                strcpy(buffer, "Invalid LIST command\r\n");
            } else {
                int msgCnt = 0;
                string user = username;

                // get and open directory for user (if existing)
                string inputPath = "../mail-spooler/" + user;

                DIR *directoryPointer = opendir(inputPath.c_str());
                struct dirent *entry;

                if (directoryPointer == NULL) {
                    perror("opendir");
                    output = "User unkown \n";
                } else {
                    // Reading all the entries in the directory
                    while ((entry = readdir(directoryPointer)) != NULL) {
                        // send entry name to client
                        output += entry->d_name;
                        output += "\n";
                        msgCnt++;
                    }
                    closedir(directoryPointer); // close all directory
                }

                // convert message to c_str and copy to buffer
                if (msgCnt > 0) {
                    msgCnt -= 2;
                }

                output += "Total message count: ";
                output += to_string(msgCnt);

                strcpy(buffer, output.c_str());
            }
        }

            /////////////////////////////////////////////////////////////////////////

        else if (input[0] == "READ" && loggedIn) {
            string user = username;
            int msgNr = stoi(input[1]);

            string output = "";

            string path = "../mail-spooler/" + user;

            // open user directory (if existing)
            DIR *directoryPointer = opendir(path.c_str());
            struct dirent *entry;

            if (directoryPointer == NULL) {
                perror("opendir");
                output = "ERR\n";
            } else {
                // count files in directory until msgCount matches input fileNr
                int msgCount = 0;
                while ((entry = readdir(directoryPointer)) != NULL) {
                    if (entry->d_type == DT_REG) {
                        if (msgCount == msgNr) {
                            // Open the exact msgNr file in a folder
                            string filePath = path + "/" + entry->d_name;
                            cout << "Filepath: " << filePath << endl;

                            // open file in reader
                            ifstream file(filePath);

                            if (file) {
                                output += "OK\n";
                                string line = "";

                                // handle lines
                                while (getline(file, line)) {
                                    output += line;
                                    output += "\n";
                                }

                                //removing last '\n'
                                string::iterator iter = output.end();
                                iter--;
                                output.erase(iter);

                                // close file
                                file.close();
                            } else {
                                cout << "Unable to open file" << endl;
                                output = "ERR\n";
                            }

                            break; // Exit the loop once the desired file is found
                        }

                        msgCount++;
                    }
                }
                closedir(directoryPointer); // close all directory
            }

            strcpy(buffer, output.c_str());
        }

            /////////////////////////////////////////////////////////////////////////

        else if (input[0] == "DEL" && loggedIn) {
            string user = username;
            int msgNr = stoi(input[1]);

            string output = "";

            string path = "../mail-spooler/" + user;

            // open user directory (if existing)
            DIR *directoryPointer = opendir(path.c_str());
            struct dirent *entry;

            if (directoryPointer == NULL) {
                perror("opendir");
                output = "ERR\n";
            } else {
                // count files in directory until msgCount matches input fileNr
                int msgCount = 0;
                while ((entry = readdir(directoryPointer)) != NULL) {
                    if (entry->d_type == DT_REG) {
                        if (msgCount == msgNr) {
                            // Open the exact msgNr file in a folder
                            string filePath = path + "/" + entry->d_name;
                            cout << "Filepath: " << filePath << endl;

                            // Delete the file
                            if (remove(filePath.c_str()) != 0) {
                                cout << "Unable to delete file" << endl;
                                output = "ERR\n";
                            } else {
                                output = "OK\n";
                            }

                            break; // Exit the loop once the desired file is found
                        }
                        msgCount++;
                    } else {
                        output = "ERR\n";
                    }
                }
                closedir(directoryPointer); // close all directory
            }

            strcpy(buffer, output.c_str());
        }

            /////////////////////////////////////////////////////////////////////////

        else if (input[0] == "QUIT") {
            string output = "quit";
            cout << "quit initiated" << endl;
            strcpy(buffer, output.c_str());
        }

            /////////////////////////////////////////////////////////////////////////

        else {
            if(loggedIn){
                cout << input[0]
                     << " | Command not recognized. Try SEND/LIST/READ/DEL/QUIT" << endl;
            }
            else{
                cout << "Try loggin in first, kekw" << endl;
            }
        }

        /////////////////////////////////////////////////////////////////////////

        // send response after every command
        if (send(*current_socket, buffer, strlen(buffer), 0) == -1) {
            perror("send failed");
            //return NULL;
        }
    } while (strcmp(buffer, "quit") != 0 && !abortRequested);

    // closes/frees the descriptor if not already
    if (*current_socket != -1) {
        if (shutdown(*current_socket, SHUT_RDWR) == -1) {
            perror("shutdown new_socket");
        }
        if (close(*current_socket) == -1) {
            perror("close new_socket");
        }
        *current_socket = -1;
    }

    //return NULL;
}

void signalHandler(int sig) {
    if (sig == SIGINT) {
        printf("abort Requested... \r\n"); // ignore error
        abortRequested = 1;
        /////////////////////////////////////////////////////////////////////////
        // With shutdown() one can initiate normal TCP close sequence ignoring
        // the reference count.
        // https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
        // https://linux.die.net/man/3/shutdown
        if (new_socket != -1) {
            if (shutdown(new_socket, SHUT_RDWR) == -1) {
                perror("shutdown new_socket");
            }
            if (close(new_socket) == -1) {
                perror("close new_socket");
            }
            new_socket = -1;
        }

        if (create_socket != -1) {
            if (shutdown(create_socket, SHUT_RDWR) == -1) {
                perror("shutdown create_socket");
            }
            if (close(create_socket) == -1) {
                perror("close create_socket");
            }
            create_socket = -1;
        }
    } else {
        exit(sig);
    }
}