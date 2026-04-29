#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

//added these so you dont have to go back and forth
#define ERROR_UNREADABLE  0
#define ERROR_NAME_IN_USE 1
#define ERROR_UNKNOWN_RECIPIENT 2
#define ERROR_ILLEGAL_CHARACTER 3
#define ERROR_TOO_LONG 4

#define MAX_NAME_LEN 32
#define MAX_STATUS_LEN 64
#define MAX_MSG_LEN 80
#define MAX_BODY_LEN 99999 //5-digit length field cap

typedef struct {
    int protocol;
    char message_code[4];
    int body_length;
    char *body;
} Message;

typedef struct {
    int fd;
    char name[33];
    char status[65];
    int has_name;
    int is_connected; // 1 if yes, 0 if no --> when we traverse list we only look at clients that are connected
} Client;

Client *clients = NULL;
int total_clients = 0;
pthread_mutex_t my_lock = PTHREAD_MUTEX_INITIALIZER;


//reads the fields
//include error checking with buff size
int fill_message_helper(int fd, char *buf, int buf_size){
    char c;
    int i = 0;
    //switched from while(read(fd, &c, 1) > 0) because we need to report -1
    while(1){
        int n = read(fd, &c, 1);
        if(n < 1){
            return -1;
        }
        if(c == '|'){
            break;
        }
        if(i < buf_size){
            buf[i] = c;
            i++;
        }
    }
    buf[i] = '\0';
    return i;
}
//read message off socket and fills Message struct.
//fd is the socket to read from
//message is what we filling out
int fill_message(int fd, Message *message){
    char version[9];
    char length[9];

    message->body = NULL;

    if(fill_message_helper(fd, version, 7) < 0){
        return -1;
    }
    message->protocol = atoi(version);

    if (fill_message_helper(fd, message->message_code, 3) < 0){
         return -1;
    }
    if (fill_message_helper(fd, length, 7) < 0){
        return -1;
    }
    message->body_length = atoi(length);
    //sanity check - cant be negative or above 5-digit cap
    if(message->body_length < 0 || message->body_length > MAX_BODY_LEN){
        return -1;
    }
    message->body = malloc(message->body_length + 1);
    if(message->body == NULL){
        return -1;
    }

    int total_bytes = 0;
    while(total_bytes < message->body_length){
        int n = read(fd, message->body + total_bytes, message->body_length - total_bytes);
        if(n <= 0){
            free(message->body);
            message->body = NULL;
            return -1;
        }
        total_bytes += n;
    }
    message->body[message->body_length] = '\0';
    return 0;
}



//writes in format
//had to bump buffer way up since WHO #all responses can blow past 200
void send_message(int fd, const char *sender, const char *recipient, const char *body){
    char message[100010];

    //had to add 3 for the |
    int len = strlen(sender) + strlen(recipient) + strlen(body) + 3;

    int n = snprintf(message, sizeof(message), "1|MSG|%d|%s|%s|%s|", len, sender, recipient, body);
    write(fd, message, n);
}

//this makes an error message that will be sent to client
void send_error(int fd, int error_code, const char *explanation) {
    char message[200];
    int body_length = snprintf(NULL, 0, "%d|%s|", error_code, explanation);
    int n = snprintf(message, sizeof(message), "1|ERR|%d|%d|%s|", body_length, error_code, explanation);
    write(fd, message, n);
}

//we are going to use this in handler functions as they all need to split the fields
//so it gets the body, sender, and recipient
int split_fields(char *body, char **fields, int max_fields) {
    fields[0] = body;
    int count = 1;
    for(int i = 0; body[i] != '\0'; i++){
        if(count < max_fields && body[i] == '|'){
            body[i] = '\0';
            fields[count] = body + i + 1;
            count++;
        }
    }
    return count;
}

//returns the index in clients[] or -1 if not found
int client_search(const char *client_name){
    for(int i = 0; i < total_clients; i++){
        if(clients[i].is_connected == 1 && clients[i].has_name == 1){
            if(strcmp(clients[i].name, client_name) == 0){
                return i;
            }
        }
    }
    return -1;
}

//sends a MSG to every connected client that has a name
void send_all(const char *sender, const char *recipient, const char *body){
    for(int i = 0; i < total_clients; i++){
        if(clients[i].is_connected == 1 && clients[i].has_name == 1){
            send_message(clients[i].fd, sender, recipient, body);
        }
    }
}


//name: 1-32 chars, letters/digits/-/_
int valid_name(const char *s){
    int len = strlen(s);
    if(len < 1) return ERROR_ILLEGAL_CHARACTER;
    if(len > MAX_NAME_LEN) return ERROR_TOO_LONG;
    for(int i = 0; i < len; i++){
        char c = s[i];
        if(!(isalnum((unsigned char)c) || c == '-' || c == '_')){
            return ERROR_ILLEGAL_CHARACTER;
        }
    }
    return -1;
}

//status: 0-64 chars, range 32-126
int valid_status(const char *s){
    int len = strlen(s);
    if(len > MAX_STATUS_LEN) return ERROR_TOO_LONG;
    for(int i = 0; i < len; i++){
        unsigned char c = (unsigned char)s[i];
        if(c < 32 || c > 126){
            return ERROR_ILLEGAL_CHARACTER;
        }
    }
    return -1;
}

//message body: 1-80 chars, range 32-126
int valid_message(const char *s){
    int len = strlen(s);
    if(len < 1) return ERROR_ILLEGAL_CHARACTER;
    if(len > MAX_MSG_LEN) return ERROR_TOO_LONG;
    for(int i = 0; i < len; i++){
        unsigned char c = (unsigned char)s[i];
        if(c < 32 || c > 126){
            return ERROR_ILLEGAL_CHARACTER;
        }
    }
    return -1;
}


//handles NAM - pick a screen name
void handle_nam(int my_index, char *body){
    pthread_mutex_lock(&my_lock);
    int fd = clients[my_index].fd;

    int err = valid_name(body);
    if(err >= 0){
        if(err == ERROR_TOO_LONG){
            send_error(fd, err, "Too long");
        } else {
            send_error(fd, err, "Illegal character");
        }
        pthread_mutex_unlock(&my_lock);
        return;
    }

    //skip ourselves so renaming to current name doesnt count as a collision
    for(int i = 0; i < total_clients; i++){
        if(i == my_index) continue;
        if(clients[i].is_connected == 1 && clients[i].has_name == 1 && strcmp(clients[i].name, body) == 0){
            send_error(fd, ERROR_NAME_IN_USE, "Name in use");
            pthread_mutex_unlock(&my_lock);
            return;
        }
    }
    strcpy(clients[my_index].name, body);
    clients[my_index].has_name = 1;

    send_message(fd, "#all", clients[my_index].name, "Welcome to the chat!");
    pthread_mutex_unlock(&my_lock);
}


//handles SET - update status, broadcast if non-empty
void handle_set(int my_index, char *body){
    pthread_mutex_lock(&my_lock);
    int fd = clients[my_index].fd;

    int err = valid_status(body);
    if(err >= 0){
        if(err == ERROR_TOO_LONG){
            send_error(fd, err, "Too long");
        } else {
            send_error(fd, err, "Illegal character");
        }
        pthread_mutex_unlock(&my_lock);
        return;
    }

    strcpy(clients[my_index].status, body);

    //only broadcast if status is non-empty (per spec)
    if(strlen(body) > 0){
        char text[200];
        snprintf(text, sizeof(text), "%s is now \"%s\"", clients[my_index].name, body);
        send_all("#all", "#all", text);
    }
    pthread_mutex_unlock(&my_lock);
}


//handles MSG - forward to one user or broadcast to #all
void handle_msg(int my_index, char *body){
    pthread_mutex_lock(&my_lock);
    int fd = clients[my_index].fd;

    char *fields[3];
    int n = split_fields(body, fields, 3);
    if(n < 3){
        send_error(fd, ERROR_UNREADABLE, "Unreadable");
        pthread_mutex_unlock(&my_lock);
        return;
    }
    //fields[0] is sender but we ignore it - cant let users masquerade
    char *recipient = fields[1];
    char *text = fields[2];

    int err = valid_message(text);
    if(err >= 0){
        if(err == ERROR_TOO_LONG){
            send_error(fd, err, "Too long");
        } else {
            send_error(fd, err, "Illegal character");
        }
        pthread_mutex_unlock(&my_lock);
        return;
    }

    if(strcmp(recipient, "#all") == 0){
        send_all(clients[my_index].name, "#all", text);
    } else {
        int idx = client_search(recipient);
        if(idx < 0){
            send_error(fd, ERROR_UNKNOWN_RECIPIENT, "Unknown recipient");
        } else {
            send_message(clients[idx].fd, clients[my_index].name, recipient, text);
        }
    }
    pthread_mutex_unlock(&my_lock);
}


//handles WHO - report on a single user or list everyone in #all
void handle_who(int my_index, char *body){
    pthread_mutex_lock(&my_lock);
    int fd = clients[my_index].fd;

    if(strcmp(body, "#all") == 0){
        //walk every connected named user, glue together with \n
        char buf[100000];
        buf[0] = '\0';
        int first = 1;
        for(int i = 0; i < total_clients; i++){
            if(clients[i].is_connected == 1 && clients[i].has_name == 1){
                if(!first) strcat(buf, "\n");
                first = 0;
                strcat(buf, clients[i].name);
                if(strlen(clients[i].status) > 0){
                    strcat(buf, ": ");
                    strcat(buf, clients[i].status);
                }
            }
        }
        send_message(fd, "#all", clients[my_index].name, buf);
        pthread_mutex_unlock(&my_lock);
        return;
    }

    int idx = client_search(body);
    if(idx < 0){
        send_error(fd, ERROR_UNKNOWN_RECIPIENT, "Unknown recipient");
        pthread_mutex_unlock(&my_lock);
        return;
    }

    char text[200];
    if(strlen(clients[idx].status) > 0){
        snprintf(text, sizeof(text), "%s: %s", clients[idx].name, clients[idx].status);
    } else {
        strcpy(text, "No status");
    }
    send_message(fd, "#all", clients[my_index].name, text);
    pthread_mutex_unlock(&my_lock);
}


//locks before send_error, used for the err 0 paths in client_handler
void send_fatal_error(int fd){
    pthread_mutex_lock(&my_lock);
    send_error(fd, ERROR_UNREADABLE, "Unreadable");
    pthread_mutex_unlock(&my_lock);
}


//runs as a thread for each connected client
void *client_handler(void *arg) {
    int my_index = *((int*)arg);
    free(arg);

    int fd = clients[my_index].fd;

    while(1){
        Message message;
        if(fill_message(fd, &message) < 0){
            break; //socket closed or read failed
        }

        //version has to be 1, anything else is fatal
        if(message.protocol != 1){
            send_fatal_error(fd);
            free(message.body);
            break;
        }

        //strip trailing | so handlers see clean fields
        //spec says every message ends with | so if its missing thats err 0
        if(message.body_length > 0 && message.body[message.body_length - 1] == '|'){
            message.body[message.body_length - 1] = '\0';
        } else {
            send_fatal_error(fd);
            free(message.body);
            break;
        }

        //need a name first - only NAM allowed before that
        pthread_mutex_lock(&my_lock);
        int has_name = clients[my_index].has_name;
        pthread_mutex_unlock(&my_lock);
        if(has_name == 0 && strcmp(message.message_code, "NAM") != 0){
            send_fatal_error(fd);
            free(message.body);
            break;
        }

        if(strcmp(message.message_code, "NAM") == 0){
            handle_nam(my_index, message.body);
        } else if(strcmp(message.message_code, "SET") == 0){
            handle_set(my_index, message.body);
        } else if(strcmp(message.message_code, "MSG") == 0){
            handle_msg(my_index, message.body);
        } else if(strcmp(message.message_code, "WHO") == 0){
            handle_who(my_index, message.body);
        } else {
            //unknown code is fatal
            send_fatal_error(fd);
            free(message.body);
            break;
        }

        free(message.body);
    }

    close(fd);

    pthread_mutex_lock(&my_lock);
    clients[my_index].is_connected = 0;
    pthread_mutex_unlock(&my_lock);
    return NULL;
}




int main(int argc, char **argv){
    if(argc != 2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return -1;
    }
    int port = atoi(argv[1]);

    //ignore SIGPIPE so a dead client doesnt kill the server
    signal(SIGPIPE, SIG_IGN);

    int fd = socket(AF_INET, SOCK_STREAM, 0); // the class notes say this is ipv4 connection

    //SO_REUSEADDR so we dont get "address in use" when restarting
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in socket_address;
    socket_address.sin_family = AF_INET;
    socket_address.sin_port = htons(port);
    socket_address.sin_addr.s_addr = INADDR_ANY;

    if(bind(fd, (struct sockaddr*)&socket_address, sizeof(socket_address)) < 0){
        perror("bind");
        return -1;
    }
    listen(fd, 10); // pick a port to listen on

    while (1) {
        int client_fd = accept(fd, NULL, NULL);

        pthread_mutex_lock(&my_lock);

        total_clients++;
        Client newClient;
        newClient.fd = client_fd;
        newClient.has_name = 0; // or whatever it means to not have a name
        newClient.is_connected = 1;
        newClient.name[0] = '\0';
        newClient.status[0] = '\0';

        void* temp = realloc(clients, total_clients * sizeof(Client));
        if(temp == NULL){
            pthread_mutex_unlock(&my_lock);
            return -1;
        }
        clients = temp;
        clients[total_clients-1] = newClient;
        int my_index = total_clients - 1;

        pthread_mutex_unlock(&my_lock);

        int *arg = malloc(sizeof(int));
        *arg = my_index;

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, arg);
        pthread_detach(tid);
    }

    free(clients);
    close(fd);
    return 0;

}
