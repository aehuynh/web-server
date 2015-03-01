#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <dirent.h>
#include <sched.h>
#include <signal.h>

static int const  ERR_NOTFOUND = 404;
static int const  ERR_NOTIMPL = 501;

// Create socket and bind to it
int create_bind(char *num){
    int portNum, sockNum;
    struct sockaddr_in serverAddr;

    portNum = atoi(num);

    // Open new socket
    if((sockNum = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        fprintf(stderr, "Error: Could not open socket.\n");
        exit(1);
    }

    // Set up socket address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(portNum);
    
    int size = sizeof(serverAddr);
    
    // Bind socket (socketNum) to address serverAddr
    if(bind(sockNum, (struct sockaddr *) &serverAddr, size) == -1){
        perror("Error: could not bind.\n");
        exit(1);
    }
    return sockNum;
}

// Output error message depending on errorno and exit
void http_error(int errorno, int client){
    char *buf = malloc(512);
    // Note: I use http/ 1.1 with a space because http.1.1 gave an error    
    switch(errorno){
    // Not Found
    case 404:
        sprintf(buf, "HTTP/ 1.1  404 NOT FOUND\r\n");   
        write(client, buf, strlen(buf));
        sprintf(buf, "Content-Type: text/html\r\n\r\n");
        write(client, buf, strlen(buf));
        sprintf(buf, "<HTML><head>\n<TITLE> 404 Not Found</title></head>\r\n");
        write(client, buf, strlen(buf));
        sprintf(buf, "<BODY>The specified URL could not be located");
        write(client, buf, strlen(buf));
        sprintf(buf, " on the server. \r\n</BODY></HTML>\r\n");
        write(client, buf, strlen(buf));
        break;
    // Not Implemented
    case 501:
        sprintf(buf, "HTTP/ 1.1 501 NOT IMPLEMENTED\r\n");
                write(client, buf, strlen(buf));
        sprintf(buf, "Content-Type: text/html\r\n\r\n");
        write(client, buf, strlen(buf));
                sprintf(buf, "<HTML><HEAD>\n<TITLE> 501 Not Implemented</TITLE></HEAD>\r\n");
                write(client, buf, strlen(buf));
        sprintf(buf, "<BODY>The server does not support the functionality");
                write(client, buf, strlen(buf));
        sprintf(buf, " required to fullfill the request. \r\n</BODY></HTML>\r\n");
        write(client, buf, strlen(buf));
        break;

    default:
        fprintf(stderr, "Could not find error message.\n");
        break;
    }
    
    close(client);  
    exit(0);

}

// Handle a request by the client
int getRequest(void* c){
    int client = *((int *)c);
    char buffer[1024];
    char filePath[256];
    char fileType[20];
    char output[4096];
    FILE *file;
    int i;
    int re = read(client, buffer, 1024);
    
    if( re <= 0){
        fprintf(stderr, "Could not read from browser.\n");
        close(client);
        exit(1);
    }else if(re < 1024){
        buffer[re] = 0;
    }
    
    // Server only handles get requests
    if(strncmp(buffer, "GET ", 4) != 0 && strncmp(buffer, "get ", 4) != 0){
        http_error(ERR_NOTIMPL, client);
                exit(1);
    }
    
    // if requesting a directory
    if(strncmp(buffer, "GET / ", 6) == 0 || strncmp(buffer, 
        "get / ", 6) == 0){
        strcpy(buffer, "GET /. ");
    }
    // i: buffer position
    i = 5;
    // j: filePath position
    int j = 0;
    // Isolate file path from the request
    while(buffer[i] != ' ' &&  buffer[i] != '\n' &&  buffer[i] != '\0'){    
        filePath[j] = buffer[i];
        i++;
        j++;
    }
    filePath[j] = 0;

    // Check if file exists
    if((file = fopen(filePath, "r")) == NULL){
        http_error(ERR_NOTFOUND, client);
        exit(1);
    }

    // Output success message
    sprintf(output, "HTTP/ 1.1 200 OK\r\n");
    write(client, output, strlen(output));

    // Get file extension
    j = strlen(filePath) - 1;
    i = 0;
    // Navigate back to character just before file extension
    while(filePath[j] != '.' && j > 0){
        j--;
    }
    // Traverse to end of file path to get file type
    while(j < strlen(filePath) && i < 19){
        fileType[i] = filePath[j];
        i++;
        j++;
    }
    
    // Handle the request based on the extension of the file
    if(strcmp(fileType, ".jpg") == 0 || strcmp(fileType, ".jpeg") == 0
        || strcmp(fileType, ".gif") == 0){
        // Figure out content type
        if(strcmp(fileType, ".gif") == 0)
            sprintf(output,"Content-Type: image/gif\r\n\r\n");
        else
            sprintf(output,"Content-Type: image/jpeg\r\n\r\n");
        // Write content type to client
        write(client, output, strlen(output));
        
        // Get image file descriptor
        int img = open(filePath, O_RDONLY, 0);
        // Get file metadata
        struct stat stat_struct;
        if(fstat(img, &stat_struct)== -1)
            perror("stat error\n");
        size_t total = 0;
        ssize_t bytesSent;
        int imgSize = stat_struct.st_size;
        if(imgSize == -1)
            perror("File size problem\n");

        // Send file to client
        while(total < imgSize){
            bytesSent = sendfile(client, img, 0, imgSize-total);
            if(bytesSent <= 0)
                perror("Error: sending file\n");
            total += bytesSent;
        }
    }else if(strcmp(fileType, ".cgi") == 0){
        int pipe1[2];
        pipe(pipe1);
        // Create new process to run CGI and get results through a pipe
        int pid = fork();
        if(pid == 0){
            dup2(pipe1[1], 1);
            close(pipe1[0]);
            // Set proper permissions
            int param = strtol("0755", 0, 8);
            chmod(filePath, param);

            execl(filePath, filePath, NULL);
            exit(0);
        }else{
            close(pipe1[1]);
            waitpid(pid, NULL, 0);

            //Write result to client    
            char buf;
            while(read (pipe1[0],&buf, sizeof(buf)) > 0){
                write(client, &buf, 1);
            }

            close(pipe1[0]);
        }
    }else if(strcmp(fileType, ".html") == 0 || (strcmp(fileType, ".htm") == 0) || (strcmp(fileType, ".txt") == 0)) {
        // Decide content type
        if((strcmp(fileType, ".txt") == 0))
            sprintf(output, "Content-Type: text/plain\r\n\r\n");
        else
            sprintf(output, "Content-Type: text/html\r\n\r\n");
        // Write content type to client
        write(client, output, strlen(output));

        //Write the conents of the file to client
        fgets(output, sizeof(output), file);
        while(!feof(file)){
            send(client, output, strlen(output), 0);
            fgets(output, sizeof(output), file);
        }
    }else{ // directory listing request
        DIR *folder;
        struct dirent *dent;
        // if dir exists
        if((folder = opendir(filePath)) != NULL){
            // Write content type to client
            sprintf(output, "Content-Type: text/plain\r\n\r\nDirectory Listing: \n\n");
            write(client, output, strlen(output));
            // Write the file/folder names to client
            while((dent = readdir(folder)) != NULL){
                sprintf(output, dent->d_name);
                // Ignore current and parent directory links
                if(strcmp(output, ".") != 0 && strcmp(output, "..") != 0){
                    write(client, output, strlen(output));
                    write(client, "\n", 1);
                }
            }
            closedir(folder);
        }else{
            http_error(ERR_NOTFOUND, client);
        }
    }

    close(client);
    exit(0);
    return 0;
}


int main(int argc, char*argv[]){
    int sockFd, clientFd;
    struct sockaddr_in clientAddr;
    int usesThreads = 0;
    
    // Error check arguments
    if(argc != 3){
        fprintf(stderr, "Error: The input should only be a port number. \n");
        exit(1);
    }
    
    sockFd = create_bind(argv[1]);
    usesThreads = atoi(argv[2]);

    // Start listening for connections, 100 is maximum number of connections
    if(listen(sockFd, 100) < 0){
        fprintf(stderr, "Listen error.\n");
    }
        
    printf("The web server is now online!\n");
    while(1){
        int size =  sizeof(clientAddr);
        
        //Create and bind client socket and address and get client file descriptor
        if((clientFd = accept(sockFd, (struct sockaddr *) &clientAddr, &size)) < 0){
            printf("Error: Did not accept properly.%i: %s\n", errno, strerror(errno));
            continue;
        }

        // If user requested threads
        if(usesThreads == 1){
            void* stack = malloc(16000);
            pid_t pid = clone(&getRequest, (char*) stack + 16000, SIGCHLD | CLONE_FS | CLONE_SIGHAND | CLONE_VM, ((void *)(&clientFd)));
        }else if(fork() == 0){
            close(sockFd);
    
            getRequest(((void *)&clientFd));    
            close(clientFd);
            exit(0);
        }
        
        close(clientFd);    
    }
         
}
