#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

//added these so you dont have to go back and forth
#define ERROR_UNREADABLE  0
#define ERROR_NAME_IN_USE 1
#define ERROR_UNKNOWN_RECIPIENT 2
#define ERROR_ILLEGAL_CHARACTER 3
#define ERROR_TOO_LONG 4
#define PORT 8000

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

typedef struct { // information we can pass to the function that handles clients
    int *client_fd;
    struct Client *client;
} Handle_Client;

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
    message->body = malloc(message->body_length + 1);

    int total_bytes = 0;
    while(total_bytes < message->body_length){
        int n = read(fd, message->body + total_bytes, message->body_length - total_bytes);
        if(n <= 0){
            free(message->body);
            return -1;
        }
        total_bytes += n;
    }
    message->body[message->body_length] = '\0';
    return 0;
}



//writes in format 
void send_message(int fd, const char *sender, const char *recipient, const char *body){
    char message[200];

    //had to add 3 for the | 
    int len = strlen(sender) + strlen(recipient) + strlen(body) + 3;

    snprintf(message, 200, "1|MSG|%d|%s|%s|%s|", len, sender, recipient, body);
    write(fd, message, strlen(message));
}

//this makes an error message that will be sent to client
//call it for all the different errors that come like 0-4 from above
void send_error(int fd, int error_code, const char *explanation) {
    char message[200];

    int body_length = snprintf(NULL, 0, "%d|%s|", error_code, explanation);
    snprintf(message, sizeof(message), "1|ERR|%d|%d|%s|", body_length, error_code, explanation);
    write(fd, message, strlen(message));
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

// returns the pointer to the client you are searching for or NULL if client is not found  
Client* client_search(char client_name[]){
    Client client;
    for(int i=0; i<total_clients; i++){
        Client temp = clients[total_clients];
        if(temp.is_connected == 1){
            if(strcmp(temp.name,client_name) == 0){
                return &temp;
            }
        }
    }

    return NULL;
}

// sends message to all clients that are currently connected
void send_all(char message[]){
    pthread_mutex_lock(&my_lock);

    for(int i=0; i<total_clients; i++){
        Client temp = clients[i];
        if(temp.is_connected == 1){
            // put write() with the message we want to send
        }
    }

    pthread_mutex_unlock(&my_lock);
}


void client_handler(void *arg) {
    Handle_Client *client_info = (Handle_Client*) arg;

    int *client_fd = client_info->client_fd; // client's fd
    Client *client = client_info->client; // client we are currently talking to

    // free(arg);

    char buffer[1024];

    while (1) {
        // read messages from client to server here
        // send messages from server to client here
        // NOTE USE BUFFER
    }

    close(*client_fd);

    pthread_mutex_lock(&my_lock);
    client->is_connected = 0;
    pthread_mutex_unlock(&my_lock);
}




int main(int argc, char **argv){
    
    int fd = socket(AF_INET, SOCK_STREAM, 0); // the class notes say this is ipv4 connection 

    struct sockaddr_in socket_address;
    socket_address.sin_family = AF_INET;
    socket_address.sin_port = htons(PORT);
    socket_address.sin_addr.s_addr = INADDR_ANY;

    bind(fd, (struct sockaddr*)&socket_address, sizeof(socket_address));
    listen(fd, 10); // pick a port to listen on 

    while (1) {
        int client_fd = accept(fd, NULL, NULL);

        pthread_mutex_lock(&my_lock);

        total_clients++;
        Client newClient;
        newClient.fd = client_fd;
        newClient.has_name = 0; // or whatever it means to not have a name
        newClient.is_connected = 1; 

        void* temp = realloc(clients, total_clients * sizeof(Client));
        if(temp == NULL){
            return -1;
        }
        clients = temp;
        clients[total_clients-1] = newClient;

        pthread_mutex_unlock(&my_lock);

        Handle_Client curr_client_info;
        curr_client_info.client_fd = &client_fd;
        curr_client_info.client = &newClient; 

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, &curr_client_info);
        pthread_detach(tid);
    }

    free(clients);
    close(fd);
    return 0;
    
}
