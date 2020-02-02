#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <list>
#include <map>
#include <string.h>
#include <pthread.h>
#include <thread>
#include <time.h>

using namespace std;

#define true  1
#define false 0

/**********************************************************************************************************************
 *                                                  Global variables.                                                 *
 **********************************************************************************************************************/


int socket_descriptor;
map<int, string> clients;
list<thread> threads_list;
struct sockaddr_in server_address;

pthread_mutex_t cs_mutex;
int port;

/**********************************************************************************************************************
 *                                                Auxiliary functions.                                                *
 **********************************************************************************************************************/

void check_argc(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage %s port\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);
}

/**********************************************************************************************************************
 *                                               TCP-connect functions.                                               *
 **********************************************************************************************************************/


void initialization_socket_descriptor() {
    socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_descriptor < 0) {
        perror("ERROR opening socket");
        exit(1);
    }
}

void initialization_socket_structure() {
    uint16_t port_number = htons((uint16_t) port);

    bzero((char *) &server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = port_number;
}

void bind_host_address() {
    if (bind(socket_descriptor, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }
}

void communicating(int new_socket_descriptor);

void accept_connection() {
    struct sockaddr_in client_address;
    unsigned int client_length = sizeof(client_address);
    int nsd = accept(socket_descriptor, (struct sockaddr *) &client_address, &client_length);
    if (nsd <= 0) {
        perror("ERROR opening socket");
        return;
    }
    threads_list.emplace_back([&] { communicating(nsd); });
}

/**********************************************************************************************************************
 *                                                IO-threads functions.                                               *
 **********************************************************************************************************************/

void admin_fun(){
    string message;
    while (true){
        getline(cin, message);
        for (map<int, string>::iterator it = clients.begin(); it != clients.end(); it++){
            if (message == it->second) {
                shutdown(it->first, 2);
                close(it->first);
                break;
            }
        }
    }
}

void communicating(int new_socket_descriptor) {
    int nick_length;
    int temp = read(new_socket_descriptor, &nick_length, 4);
    if (temp < 0) {
        perror("ERROR reading from socket");
        return;
    }
    auto nick = new char[nick_length]();
    temp = read(new_socket_descriptor, nick, nick_length);
    if (temp < 0) {
        perror("ERROR reading from socket");
        return;
    }
    pthread_mutex_lock( &cs_mutex );
    clients[new_socket_descriptor] = string(nick);
    pthread_mutex_unlock( &cs_mutex );
    string nickname = string(nick);
    free(nick);

    while (true) {
        int message_size;
        int temp = read(new_socket_descriptor, &message_size, 4);
        if (temp <= 0) {
            perror("ERROR reading from socket");
            pthread_mutex_lock(&cs_mutex);
            clients.erase(clients.find(new_socket_descriptor));
            pthread_mutex_unlock(&cs_mutex);
            close(new_socket_descriptor);
            break;
        }
        auto buffer = new char[message_size];
        bzero(buffer, sizeof(buffer));
        temp = read(new_socket_descriptor, buffer, message_size);
        if (temp <= 0) {
            perror("ERROR reading from socket");
            pthread_mutex_lock(&cs_mutex);
            clients.erase(clients.find(new_socket_descriptor));
            pthread_mutex_unlock(&cs_mutex);
            close(new_socket_descriptor);
            break;
        }
        message_size = message_size + nickname.length() + 6;
        auto full_buffer = new char[message_size];
        bzero(full_buffer, sizeof(full_buffer));

        strcpy(full_buffer, "[");
        strcat(full_buffer, nickname.c_str());
        strcat(full_buffer, "] -> ");
        strcat(full_buffer, buffer);

        for (map<int, string>::iterator it = clients.begin(); it != clients.end(); it++) {
            if (it->first > 0) {
                temp = write(it->first, &message_size, 4);
                if (temp <= 0) {
                    perror("ERROR writing in socket");
                    continue;
                }
                temp = write(it->first, full_buffer, message_size);
                if (temp <= 0) {
                    perror("ERROR writing in socket");
                    continue;
                }
            }
        }
        free(buffer);
        free(full_buffer);
    }
    pthread_exit(0);
}

/**********************************************************************************************************************
 *                                                    main-function.                                                  *
 **********************************************************************************************************************/


int main(int argc, char *argv[]) {
    check_argc(argc, argv);
    initialization_socket_descriptor();
    initialization_socket_structure();
    bind_host_address();
    listen(socket_descriptor, 5);
    thread t0(admin_fun);
    while (true) {
        accept_connection();
    }
    for (thread &t: threads_list) {
        t.join();
    }
}