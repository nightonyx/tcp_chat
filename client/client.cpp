#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <ncurses.h>
#include <pthread.h>
#include <thread>
#include <locale.h>
#include <string>

#define true  1
#define false 0
#define NICKNAME_LENGTH 15

using namespace std;

/**********************************************************************************************************************
 *                                                  Global variables.                                                 *
 **********************************************************************************************************************/

int socket_descriptor;
uint16_t port_number;
struct sockaddr_in server_address;
struct hostent *server;
char *nickname;
char temp_string[4];
string output_buffer;

// ncurses variables
int winrows, wincols;
WINDOW *winput, *woutput;

pthread_mutex_t cs_mutex;

time_t seconds;

/**********************************************************************************************************************
 *                                                Auxiliary functions.                                                *
 **********************************************************************************************************************/

void check_condition(int cond, char *str, int sig){
    if (cond){
        perror(str);
        exit(sig);
    }
}

void check_argc(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage %s hostname port nickname\n", argv[0]);
        exit(0);
    }
    server = gethostbyname(argv[1]);
    check_condition(server == NULL, "ERROR, no such host\n", 0);

    // convert to network-short (to big-endian)
    port_number = htons((uint16_t) atoi(argv[2]));
    check_condition(strlen(argv[3]) > NICKNAME_LENGTH,
                    "Please change the nickname. The nickname is limited to 15 characters.", 0);
    nickname = argv[3];
    check_condition(strchr(nickname, '[') != NULL, "Please change the nickname. Unacceptable symbols: [] ->.", 0);
    check_condition(strchr(nickname, ']') != NULL, "Please change the nickname. Unacceptable symbols: [] ->.", 0);
    check_condition(strchr(nickname, ' ') != NULL, "Please change the nickname. Unacceptable symbols: [] ->.", 0);
    check_condition(strchr(nickname, '-') != NULL, "Please change the nickname. Unacceptable symbols: [] ->.", 0);
    check_condition(strchr(nickname, '>') != NULL, "Please change the nickname. Unacceptable symbols: [] ->.", 0);

}

/**********************************************************************************************************************
 *                                                 ncurses-functions.                                                 *
 **********************************************************************************************************************/

void delete_last_char(){
//    if (output_buffer.length() > 0) output_buffer.erase(output_buffer.length() - 1, 1);
    if (output_buffer.length() > 0) output_buffer.pop_back();
}

void ncurses_init(){
    setlocale(LC_ALL, "");
    initscr();
    getmaxyx(stdscr, winrows, wincols);
    woutput = newwin(winrows - 1, wincols*2, 0, 0);
    winput  = newwin(1, wincols*2, winrows - 1, 0);
    keypad(winput, true);
    keypad(woutput, false);
    scrollok(woutput, true);
    use_default_colors();
    start_color();
    init_pair(1, COLOR_GREEN, -1);
    init_pair(2, COLOR_BLUE,  -1);
    wbkgd(woutput, COLOR_PAIR(1));
    wbkgd(winput, COLOR_PAIR(2));
    refresh();
    wrefresh(woutput);
    wrefresh(winput);
    output_buffer = "";
}

void ncurses_close(){
    delwin(winput);
    delwin(woutput);
    endwin();
    endwin();
    close(socket_descriptor);
}

/**********************************************************************************************************************
 *                                               TCP-connect functions.                                               *
 **********************************************************************************************************************/

void initialization_socket_descriptor(){
    //  AF_INET     - IPv4
    //  SOCK_STREAM - TCP
    //  0           - Default
    socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_descriptor < 0) {
        perror("ERROR opening socket");
        exit(1);
    }
}

void initialization_server_address(){
    bzero((char *) &server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    bcopy(server->h_addr, (char *) &server_address.sin_addr.s_addr, (size_t) server->h_length);
    server_address.sin_port = port_number;
}

void server_connect(){
    if (connect(socket_descriptor, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        perror("ERROR connecting");
        exit(1);
    }
    int temp = strlen(nickname);
    if (write(socket_descriptor, &temp, 4) < 0) {
        exit(1);
    }
    if (write(socket_descriptor, nickname, strlen(nickname)) < 0) {
        exit(1);
    }
}

void send_message(){
    if (strcmp(output_buffer.c_str(), "\\quit") == 0){
        ncurses_close();
        exit(0);
    }
    int message_length = output_buffer.length();
    int temp = write(socket_descriptor, &message_length, 4);
    if (temp <= 0) {
        ncurses_close();
        perror("ERROR writing to socket");
        exit(1);
    }
    temp = write(socket_descriptor, output_buffer.c_str(), output_buffer.length());
    if (temp <= 0) {
        ncurses_close();
        perror("ERROR writing to socket");
        exit(1);
    }
}

void read_server_response(){
    int message_size;
    int temp = read(socket_descriptor, &message_size, 4);
    if (temp <= 0) {
        ncurses_close();
        perror("ERROR reading from socket");
        exit(1);
    }
    auto input_buffer = new char[message_size];
    bzero(input_buffer, sizeof(input_buffer));
    temp = read(socket_descriptor, input_buffer, message_size);
    seconds = time(0);
    if (strlen(input_buffer) != message_size){
        free(input_buffer);
        return;
    }
    if (temp <= 0) {
        ncurses_close();
        perror("ERROR reading from socket");
        exit(1);
    }
    if (strcmp(input_buffer, "") != 0){
        pthread_mutex_lock( &cs_mutex );
        wprintw(woutput, "[%c%c:%c%c]%s\n", ctime(&seconds)[11], ctime(&seconds)[12], ctime(&seconds)[14],
                ctime(&seconds)[15], input_buffer);
        wrefresh(woutput);
        pthread_mutex_unlock( &cs_mutex );
    }
    free(input_buffer);
}

/**********************************************************************************************************************
 *                                                IO-threads functions.                                               *
 **********************************************************************************************************************/

void * input_thread_fun(){
    while(true){
        read_server_response();
    }
}

void * output_thread_fun(){
    char input_char;
    while(true){
        input_char = wgetch(winput);
        switch (input_char){
            case 7: { //KEY_BACKSPACE
                delete_last_char();
                break;
            }
            case '\n': {
                if (output_buffer.length() == 0) break;
                send_message();
                output_buffer.clear();
                break;
            }
            default: {
                output_buffer.push_back(input_char);
                break;
            }
        }
        pthread_mutex_lock( &cs_mutex );
        wclear(winput);
        wprintw(winput, output_buffer.c_str());
        wrefresh(winput);
        pthread_mutex_unlock( &cs_mutex );
    }
}

/**********************************************************************************************************************
 *                                                    main-function.                                                  *
 **********************************************************************************************************************/

int main(int argc, char *argv[]) {
    check_argc(argc, argv);
    initialization_socket_descriptor();
    initialization_server_address();
    server_connect();
    ncurses_init();

    thread input_thread(input_thread_fun);
    thread output_thread(output_thread_fun);

    output_thread.join();
    input_thread.join();
}