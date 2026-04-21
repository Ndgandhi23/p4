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
} Client;

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



int main(int argc, char **argv){
    
}
