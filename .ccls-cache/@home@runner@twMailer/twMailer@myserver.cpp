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

using namespace std;

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;

///////////////////////////////////////////////////////////////////////////////

void *clientCommunication(void *data);
void signalHandler(int sig);

///////////////////////////////////////////////////////////////////////////////

int main(void) {
  socklen_t addrlen;
  struct sockaddr_in address, cliaddress;
  int reuseValue = 1;

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
    // START CLIENT
    // ignore printf error handling
    printf("Client connected from %s:%d...\n", inet_ntoa(cliaddress.sin_addr),
           ntohs(cliaddress.sin_port));
    clientCommunication(&new_socket); // returnValue can be ignored
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

  return EXIT_SUCCESS;
}

void *clientCommunication(void *data) {
  char buffer[BUF];
  int size;
  int *current_socket = (int *)data;

  ////////////////////////////////////////////////////////////////////////////
  // SEND welcome message
  strcpy(buffer, "Welcome to myserver!\r\nPlease enter your commands...\r\n");
  if (send(*current_socket, buffer, strlen(buffer), 0) == -1) {
    perror("send failed");
    return NULL;
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

    // remove ugly debug message, because of the sent newline of client
    /*
    if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n')
    {
       size -= 2;
    }
    else if (buffer[size - 1] == '\n')
    {
       --size;
    }
     */

    buffer[size] = '\0';
    // printf("Message received: %s\n", buffer); // ignore error

    /**-------------------------------------------------**/

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
    input.push_back(current);
    current = "";

    int inputSize = input.size();

    /////////////////////////////////////////////////////////////////////////

    if (input[0] == "SEND") {
      string output = "";
      if (inputSize < 6) {
        printf("Invalid SEND command.\n");
        output = "ERR\n";
      }

      else {
        string sender = input[1];
        string receiver = input[2];
        string subject = input[3];
        string message = input[4];

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
        file << message << "\n";

        file.close();
        closedir(directoryPointer);

        if (output != "ERR\n") {
          output = "OK\n";
        }
      }
      strcpy(buffer, output.c_str());
    }

    /////////////////////////////////////////////////////////////////////////

    else if (input[0] == "LIST") {

      string output = "";

      if (inputSize < 2) {
        strcpy(buffer, "Invalid LIST command\r\n");
      } else {
        int msgCnt = 0;
        string user = input[1];

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

        cout << "Buffer: " << endl << buffer << endl;
      }
    }

    /////////////////////////////////////////////////////////////////////////

    else if (input[0] == "READ") {
      string user = input[1];
      int msgNr = stoi(input[2]);

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

                // close file
                file.close();
              } else {
                cout << "Unable to open file" << endl;
                output = "ERR\n";
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

    else if (input[0] == "DEL") {
      string user = input[1];
      int msgNr = stoi(input[2]);

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
      cout << input[0]
           << " | Command not recognized. Try SEND/LIST/READ/DEL/QUIT" << endl;
    }

    /////////////////////////////////////////////////////////////////////////

    // send response after every command
    if (send(*current_socket, buffer, strlen(buffer), 0) == -1) {
      perror("send failed");
      return NULL;
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

  return NULL;
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
