#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

///////////////////////////////////////////////////////////////////////////////

std::string receiveInput() {
  std::string input;
  std::cout << ">> ";
  std::getline(std::cin, input);
  return input;
}

std::string receiveUser(std::string role) {
  std::string input;
  bool wrongInput = true;

    while(wrongInput) {
        std::cout << "Please enter " << role << "username (max. 8 chars, a - z, 0 - 9)" << std::endl;
        std::cout << ">> ";
        std::getline(std::cin, input);

        if (input.size() > 8) {
            input.erase();
            std::cout << "Input too long. ";
        } else {
            for (unsigned int i = 0; i < input.length(); i++) {
                if (!(std::islower(input[i]) || std::isdigit(input[i]))) {
                    input.erase();
                    std::cout << "Wrong characters. ";
                    break;
                }
            }
        }
        if (input[0]) {
            wrongInput = false;
        }
    }
    return input;
}

std::string receiveSubject() {
  std::string inputSubject;
  bool exitCondition = false;

    std::cout << "Enter your message's subject: " << std::endl;
    while(!exitCondition){
        std::cout << ">> ";
        std::getline(std::cin, inputSubject);
        if(inputSubject.length() <= 80){
            exitCondition = true;
        }
        else{
            std::cout << "Subject too long (max. 80 characters). ";
        }
    }
    return inputSubject;
}

std::string receiveMessage(){
    std::vector<std::string> inputs;
    std::string input;
    bool exitCondition = false;
    int messageNo = 0;

    std::cout << "Enter your message: " << std::endl;
    while(!exitCondition){
        std::cout << ">> ";
        std::getline(std::cin, input);
        if(input == "." && messageNo != 0){
            exitCondition = true;
        }
        inputs.push_back(input);
        messageNo++;
        input.erase();
    }

  for (auto &iter : inputs) {
    input += iter;
    input += '\n';
  }
  return input;
}

std::string receiveNumber() {
  std::string input;
  bool wrongInput = true;

  while (wrongInput) {
    std::cout << "Please enter message number: " << std::endl;
    std::cout << ">> ";
    std::getline(std::cin, input);

    for (unsigned int i = 0; i < input.length(); i++) {
      if (!(std::isdigit(input[i]))) {
        input.erase();
        std::cout << "Wrong input, numbers only. ";
        break;
      }
    }
    if (input[0]) {
      wrongInput = false;
    }
  }
  return input;
}

int main(int argc, char **argv){
   int create_socket;
   char buffer[BUF];
   struct sockaddr_in address;
   int size;
   int isQuit = 0;

  ////////////////////////////////////////////////////////////////////////////
  // CREATE A SOCKET
  if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("Socket error");
    return EXIT_FAILURE;
  }

  ////////////////////////////////////////////////////////////////////////////
  // INIT ADDRESS
  memset(&address, 0, sizeof(address)); // init storage with 0
  address.sin_family = AF_INET;         // IPv4
  address.sin_port = htons(PORT);
  if (argc < 2) {
    inet_aton("127.0.0.1", &address.sin_addr);
  } else {
    inet_aton(argv[1], &address.sin_addr);
  }

  ////////////////////////////////////////////////////////////////////////////
  // CREATE A CONNECTION
  if (connect(create_socket, (struct sockaddr *)&address, sizeof(address)) ==
      -1) {
    perror("Connect error - no server available");
    return EXIT_FAILURE;
  }
  printf("Connection with server (%s) established\n",
         inet_ntoa(address.sin_addr));

  ////////////////////////////////////////////////////////////////////////////
  // RECEIVE DATA
  size = recv(create_socket, buffer, BUF - 1, 0);
  if (size == -1) {
    perror("recv error");
  } else if (size == 0) {
    printf("Server closed remote socket\n"); // ignore error
  } else {
    buffer[size] = '\0';
    printf("%s", buffer); // ignore error
  }

  int inputCorrect = 0;
  std::string input;
  std::vector<std::string> inputs;

    ////////////////////////////////////////////////////////////////////////////
    // HANDLE INPUT

    //do-loop handles input and receives answer until exit condition (quit)
    do{
       while(inputCorrect == 0){
           input = receiveInput();

           if(input == "SEND"){
               std::cout << "SEND command " << std::endl;
               inputs.push_back(input);
               input.erase();

               input = receiveUser("Sender's ");
               inputs.push_back(input);
               input.erase();

               input = receiveUser("Receiver's ");
               inputs.push_back(input);
               input.erase();

               input = receiveSubject();
               inputs.push_back(input);
               input.erase();

               input = receiveMessage();
               inputs.push_back(input);
               input.erase();

               //transforming vector<string> inputs into one single string input seperated by '\n'
               for(auto & iter : inputs){
                   input += iter;
                   input += '\n';
               }

               //transforming c++ std::string input into c-array char[] buffer
               strcpy(buffer, input.c_str());
               size = strlen(buffer);
               input.erase();

               inputCorrect++;
           }
           else if (input == "LIST") {
               std::cout << "LIST command " << std::endl;
               inputs.push_back(input);
               input.erase();

               input = receiveUser("");
               inputs.push_back(input);
               input.erase();

               //transforming vector<string> inputs into one single string input seperated by '\n'
               for (auto &iter : inputs) {
                 input += iter;
                 input += '\n';
               }

               //transforming c++ std::string input into c-array char[] buffer
               strcpy(buffer, input.c_str());
               size = strlen(buffer);
               input.erase();

               inputCorrect++;
           }
           else if (input == "READ") {
              std::cout << "READ command " << std::endl;
              inputs.push_back(input);
              input.erase();

              input = receiveUser("");
              inputs.push_back(input);
              input.erase();

              input = receiveNumber();
              inputs.push_back(input);
              input.erase();

              //transforming vector<string> inputs into one single string input seperated by '\n'
              for(auto & iter : inputs){
                  input += iter;
                  input += '\n';
              }

              //transforming c++ std::string input into c-array char[] buffer
              strcpy(buffer, input.c_str());
              size = strlen(buffer);
              input.erase();

              inputCorrect++;
           }
           else if(input == "DEL"){
              std::cout << "DEL command " << std::endl;
              inputs.push_back(input);
              input.erase();

              input = receiveUser("");
              inputs.push_back(input);
              input.erase();

              input = receiveNumber();
              inputs.push_back(input);
              input.erase();

              //transforming vector<string> inputs into one single string input seperated by '\n'
              for (auto &iter : inputs) {
                  input += iter;
                  input += '\n';
              }

              //transforming c++ std::string input into c-array char[] buffer
              strcpy(buffer, input.c_str());
              size = strlen(buffer);
              input.erase();

              inputCorrect++;
           }
           else if(input == "QUIT" || input == "quit"){
               isQuit++;
               inputCorrect++;
           }
       else{
           std::cout << "Unknown command. Please enter your commands (SEND/LIST/READ/DEL/QUIT)" << std::endl;
       }
   }
   inputCorrect = 0;

    //////////////////////////////////////////////////////////////////////
    // SEND DATA
    if ((send(create_socket, buffer, size, 0)) == -1) {
      perror("send error");
      break;
    }

    //////////////////////////////////////////////////////////////////////
    // CLEAR BUFFERS
    inputs.clear();
    strcpy(buffer, "");

    //////////////////////////////////////////////////////////////////////
    // RECEIVE FEEDBACK
    size = recv(create_socket, buffer, BUF - 1, 0);
    if (size == -1) {
      perror("recv error");
      break;
    } else if (size == 0) {
      printf("Server closed remote socket\n"); // ignore error
      break;
    } else {
      buffer[size] = '\0';
      printf("<< %s\n", buffer); // ignore error
    }
  } while (!isQuit);

   ////////////////////////////////////////////////////////////////////////////
   // CLOSES THE DESCRIPTOR
   if (create_socket != -1){
      if (shutdown(create_socket, SHUT_RDWR) == -1){
         // invalid in case the server is gone already
         perror("shutdown create_socket");
      }
      if (close(create_socket) == -1){
         perror("close create_socket");
      }
      create_socket = -1;
   }
  return EXIT_SUCCESS;
}