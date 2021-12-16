#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <magick/api.h>
#include "socket.h"
#include "ui.h"
#include <fcntl.h>
#include <sys/sendfile.h>
#define MAX_IMAGE_SIZE 16711568
#define MAX_MESSAGE_LENGTH 2048

//global to catch Exceptions from magick
ExceptionInfo exception;

//globals to track server socket, parent socket, number of files sent by user (for creating filenames), and port message
int server_socket_fd;
int socket_fd = -1;
int user_count = 0;
char* portm;

//client node struct to keep track of client info
typedef struct client{
  int client_socket_fd; //client fd
  struct client * next; //next element in list
} client_t;

//linked list structure to store client connections
typedef struct client_list{
  client_t* first;
} client_list_t;

//lock to add/remove clients from client list atomically
pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;

//declare clients list
client_list_t* clients = NULL;

/**
 * send_image
 * \param out_fd file descriptor to send image to
 * \param message filepath of image to be sent
 * 
 * this function returns -1 if the send fails and 0 otherwise
 * */
int send_image(int out_fd, const char* message);

/**
 * receive_image 
 * \param fd file descriptor of the socket it is receiving from, which is also the sender's ID
 * \param client_count the number of files sent by the client ID
 * 
 * this function returns the sent file's name in the form clientfd_client_count
 * */
char* receive_image(int fd, int client_count);

/**
 * input_callback
 * \param message file path of the file to be sent after the user hits enter
 * this function returns nothing
 * */
void input_callback(const char* message);

/**
 * fryer, swirl, oilpaint, implode
 * \param Image, the image to be modified
 * these functions apply said filter to the image
 * these functions return nothing
 * */
void fryer(Image** image);
void swirl(Image ** image);
void oilpaint(Image ** image);
void implode (Image ** image);

/**
 * accept_client
 * \param thread_arg, thread arguments for the thread that accepts clients
 * this thread spins in the background, accepting new connections whenever there is one
 * this thread returns nothing after execution
 * */
void* accept_client(void* thread_arg);

/**
 * client_thread
 * \param args, thread arguments for the client thread that will receive images
 * this thread runs for every client connected to the network, which will receive images sent by other users
 * this thread returns nothing after execution
 * */
void* client_thread(void* args);

/**
 * parent_thread
 * \param args, thread arguments for the parent thread that will receive images
 * this thread runs for each client that is connected to a parent, which will receive and handle images sent by parents
 * this thread returns nothing after execution
 * */
void* parent_thread(void* args);

int main ( int argc, char **argv ) {
  char infile[MaxTextExtent];
  InitializeMagick(NULL);

  if (argc != 2 && argc != 4)
    {
      (void) fprintf ( stderr, "Usage: %s <receive_folder_path> [<peer> <port number>]\n", argv[0]);
      (void) fflush(stderr);
      goto program_exit;
    }

  (void) strncpy(infile, argv[1], MaxTextExtent-1 );

  if(chdir(infile) == -1){
    perror("Directory set up failed");
    exit(1);
  }

// Set up a server socket to accept incoming connections
  unsigned short port = 0;
  server_socket_fd = server_socket_open(&port);

  //set up thread to accept client connections
  pthread_t accepter;
  pthread_create(&accepter, NULL, &accept_client, NULL);

  clients = malloc(sizeof(client_list_t));
  clients->first = NULL;
  //error checks on server socket
  if(server_socket_fd == -1){
    perror("Server socket was not opened");
    exit(1);
  }
  if (listen(server_socket_fd, 1)) {
    perror("listen failed");
    exit(EXIT_FAILURE);
  }

  //allocate string to write peer port information to UI
  portm = malloc(sizeof(char)*40);

  //write port information to UI
  snprintf(portm, 40,"Peer port number %hu\n",port);
  

  // Did the user specify a peer we should connect to?
  if (argc == 4) {
    // Unpack arguments
    char* peer_hostname = argv[2];
    unsigned short peer_port = atoi(argv[3]);

    //Connect to another peer in the chat network
    socket_fd = socket_connect(peer_hostname, peer_port);
    if(socket_fd == -1){
      perror("Failed to connect");
      exit(EXIT_FAILURE);
    }

    //set up parent thread
    pthread_t parent_check;
    pthread_create(&parent_check, NULL, &parent_thread, &socket_fd);
  }

  // Set up the user interface. The input_callback function will be called
  // each time the user hits enter to send a message.
  ui_init(input_callback);

  // Once the UI is running, you can use it to display log messages
  ui_display("Connection info", portm);

  // Run the UI loop. This function only returns once we call ui_stop() somewhere in the program.
  ui_run();

  program_exit:
  DestroyMagick();

  return 0;
}

void* parent_thread(void* args){
  //read in parent_fd
  int* parent = (int*) args;
  int parent_fd = *parent;
  int parent_count = 0;

  char* user; //variable to store username for display
  client_t* current; //variable to traverse clients list

  //main loop
  while(1){
    //wait until you receive a message
    char* message = receive_image(parent_fd, parent_count);

    //if the message is NULL and errno does not signal that the message is simply too long, disconnection must have happened
    if (message == NULL && errno != EINVAL) {
      //let the user know that there has been a disconnect
      ui_display("chatroom","Disconnected from server; You are now part of your own network");
      socket_fd = -1; //reset server socket fd

      //remind the user of peer port socket so others may connect
      ui_display("Reminder", portm);

      //close the parent server thread
      pthread_exit(NULL);
    }

    //set the cursor of current to the beginning of the clients list
    current = clients->first;

    //iterate through the clients list and forward the message to each client
    while(current != NULL){
      ui_display("sending...", "...");
      send_image(current->client_socket_fd,message);
      current = current->next;
    }

    //break apart the message and display it to the user
    ui_display(message, " Received from server");
    
  }
}

void* client_thread(void* args){

  //read in client from args and client_fd
  client_t* client = (client_t*) args;
  int client_fd = client->client_socket_fd;

  //set up variable to hold usernames incoming from messages
  int client_count = 0;
  client_t* current; //cursor to iterate through linked list

  //main loop
  while(1){

    //receive a message from the client
    char* message = receive_image(client_fd, client_count);

    //if message is NULL and errno != EINVAL then connection has failed
    if (message == NULL && errno != EINVAL) {

      //let user know that client has disconnected
      ui_display("chatroom","Client has disconnected");

      //set a cursor to update client list
      client_t* prev = NULL;

      //set the current cursor to the beginning of the list
      current = clients->first;

      //iterate until you find the client being used by this thread
      while(current != NULL){
        if(current == client){
          break;
        }
        prev = current;
        current = current->next;
      }

      //acquire a lock to edit the list to avoid conflicts between adding/removing from the list
      pthread_mutex_lock(&list_lock);

      //case where the client is the first item on the list
      if(prev == NULL && current != NULL){
        //set adequate pointers and free the client node
        clients->first = current->next;
        free(current);

        //release the lock and close the handling thread
        pthread_mutex_unlock(&list_lock);
        pthread_exit(NULL);
      }

      //if current is NULL (for some reason), the client wasn't found on the list
      if(current == NULL){
        //there is nothing to remove so release the lock and close the thread
        pthread_mutex_unlock(&list_lock);
        pthread_exit(NULL); 
      }

      //otherwise it's somewhere in the middle of the list so we use prev to change the list pointers
      prev->next = current->next;

      //free, unlock, and exit thread
      free(current);
      pthread_mutex_unlock(&list_lock);
      pthread_exit(NULL);
    }
    client_count++;

    //if there is a parent server forward the message to the parent
    if(socket_fd != -1){
      send_image(socket_fd, message);
    }

    //set the cursor to the beginning of the list and iterate
    current = clients->first;
    while(current != NULL){
      //iterate through the list and forward the message, except if the client is the same client the message came from
      if(current == client){
        current = current->next;
        continue;
      }
      send_image(current->client_socket_fd,message);
      current = current->next;
    }

    //break up the message and display
    ui_display(message, " file received from client");
    
  }
}

// Send a across a socket with a header that includes the message length.
int send_image(int out_fd, const char* message) {
    // If the message is NULL, set errno to EINVAL and return an error
    if (message == NULL) {
        errno = EINVAL;
        return -1;
    }
    char *outfile = malloc(sizeof(char)*20);
    char filepath[MAX_MESSAGE_LENGTH];
    filepath[MAX_MESSAGE_LENGTH -1] = '\0';
    strcpy(filepath, message);
    sprintf(outfile, "./%d", user_count);
    strcat(outfile, "user.png");
    char* path;
    char* filter;
    ImageInfo
    *imageInfo;
    imageInfo = CloneImageInfo(0);
    GetExceptionInfo(&exception);
    path = strtok(filepath, " ");
    (void) strcpy(imageInfo->filename, path);
    
    Image* image = ReadImage(imageInfo, &exception);
    strcpy(image->filename, outfile);
    if (image == (Image *) NULL)
    {
      CatchException(&exception);
      ui_display("chatroom","send failed");
      return -1;
    } 

    filter = strtok(NULL, " ");
    if(filter == NULL){
      char* temp = outfile;
      free(temp);
      outfile = path;
      goto sender;
    }
    while(filter != NULL){
        switch (filter[0]) {
        case 'f':
            fryer(&image);
            filter = strtok(NULL, " ");
            break;
        case 's':
            swirl(&image);
            filter = strtok(NULL, " ");
            break;
        case 'o':
            oilpaint(&image);
            filter = strtok(NULL, " ");
            break;
        case 'i':
            implode(&image);
            filter = strtok(NULL, " ");
            break;
        default:
            printf("invalid option %s\n", filter);
            filter = strtok(NULL, " ");
            break;
        }
    }

    if (!WriteImage (imageInfo,image)){
      CatchException(&image->exception);
      return -1;
    }

    sender: ;
      FILE* fp = fopen(outfile, "r");
      user_count++;
      fseek(fp, 0L, SEEK_END);
      long sz = ftell(fp);
      rewind(fp);

      fclose(fp);


      int in_fd = open(outfile, O_RDONLY);
      if(path != outfile) free(outfile);
      if(in_fd <= -1){
          return -1;
      }

  // First, send the length of the message in a long
      if (write(out_fd, &sz, sizeof(long)) != sizeof(long)) {
          // Writing failed, so return an error
          return -1;
      }

      if (sendfile(out_fd, in_fd, NULL, (size_t) sz) == -1) {
          return -1;
      }

      close(in_fd);
      if (image != (Image *) NULL)
          DestroyImage(image);

      if (imageInfo != (ImageInfo *) NULL)
          DestroyImageInfo(imageInfo);

    return 0;
}

char* receive_image(int fd, int client_count){
    long filesize;
    if (read(fd, &filesize, sizeof(long)) != sizeof(long)) {
        return NULL;
    }

    // Now make sure the message length is reasonable
    if (filesize > MAX_IMAGE_SIZE) {
        errno = EINVAL;
        return NULL;
  }

    // Allocate space for the message
    void* result = malloc(filesize + 1);

    // Try to read the message. Loop until the entire message has been read.
    long bytes_read = 0;
    while (bytes_read < filesize) {
        // Try to read the entire remaining message
        ssize_t rc = read(fd, result + bytes_read, filesize - bytes_read);

        // Did the read fail? If so, return an error
        if (rc <= 0) {
            free(result);
            return NULL;
        }

        // Update the number of bytes read
        bytes_read += rc;
    }

    char* filename = malloc(sizeof(char) * 40);

    sprintf(filename, "./client%d_%d.png", fd, client_count);

    int write_fd = open(filename, O_CREAT | O_RDWR);
    if(write_fd <= -1){
        free(result);
        return NULL;
    }
    ssize_t rrc = write(write_fd, result, (size_t)bytes_read);
    client_count++;
    if(rrc <= -1){
        free(result);
        return NULL;
    }

    fchmod(write_fd, S_IRUSR | S_IWUSR);

    close(write_fd);
    free(result);
    return filename;
}
  
void input_callback(const char* message) {
  //handle large paths
  if(strlen(message) > MAX_MESSAGE_LENGTH){
    ui_display("Warning","Messages must be 2048 characters or less");
    return;
  }
  //handle quit
  if (strcmp(message, ":quit") == 0 || strcmp(message, ":q") == 0) {
    ui_exit();
  } else {
    //display in the UI
    ui_display("user", message);

    //check if there are clients to forward to
    if(clients == NULL){
      return;
    }

    char filepath[MAX_MESSAGE_LENGTH];
    strncpy(filepath, message, MAX_MESSAGE_LENGTH);
    char* path = strtok(filepath, " ");

    if((strcmp((path + (strlen(path) - 5)), ".jpeg")) && (strcmp((path + (strlen(path) - 4)), ".png")) && (strcmp((path + (strlen(path) - 4)), ".jpg"))){
      ui_display("chatroom", "Invalid file, must be .png, .jpeg, or .jpg");
      return;
    }

    FILE* f = fopen(path, "r+");
    if(f == NULL){
      ui_display("chatroom", "Invalid filepath");
      return;
    }
    fclose(f);

    //forward the message to the parent server if there is any
    if(socket_fd != -1){
      send_image(socket_fd, message);
    }

    //iterate through the list of clients and send them the message
    client_t* current = clients->first;
    while(current != NULL){
      send_image(current->client_socket_fd, message);
      current = current->next;
    }
  }
}

void fryer(Image** image){
  // apply a bunch of filters to the image to "deep fry" it like a modern meme made in the latter half of the past decade and beyond
  ContrastImage(*image, 1);
  *image = GaussianBlurImage(*image, 1,10.0,&exception);
  *image = SharpenImage(*image, 20,10.0,&exception);
  *image = AddNoiseImage(*image, ImpulseNoise, &exception);
  for(int i = 0; i < 10; i++){
      ModulateImage(*image, "90,300,95");
  *image = SharpenImage(*image, 11,10.0,&exception);
  ModulateImage(*image, "110,300,105");
  }
  NormalizeImage(*image);
  NormalizeImage(*image);
  ModulateImage(*image, "99,200,94");
  EnhanceImage(*image, &exception);
}

void oilpaint(Image** image) {
  // apply filters to make the image look "artistic" with respect to oil painting
  size_t oil_radius = ((*image)->rows * (*image)->columns)/161568;
  *image = OilPaintImage(*image, oil_radius, &exception);
  *image = SharpenImage(*image, 0,10,&exception);
  *image = GaussianBlurImage(*image, 1,10.0,&exception);
}

void swirl(Image** image){
  // apply filter to swirl the image from the center
  srand(time(0));
  *image = SwirlImage(*image,rand()%90, &exception);
}

void implode(Image** image){
  // implode the image, literally
  *image = ImplodeImage(*image, 0.5,&exception);
}


//this function is meant to open a thread to accept clients in the background for each user
void* accept_client(void* thread_arg){
  //set up client variables
  int client_fd;
  client_t * new_client;
  while(1){
    //accept clients
    client_fd = server_socket_accept(server_socket_fd);

    //acquire list lock to modify the user list
    pthread_mutex_lock(&list_lock);

    //set up the new client node and add it to the beginning of the client list
    new_client = malloc(sizeof(client_t));
    new_client->client_socket_fd = client_fd;
    new_client->next = clients->first;
    clients->first = new_client;

    //unlock the list
    pthread_mutex_unlock(&list_lock);

    //spin up a new thread to handle this new client
    ui_display("chatroom", "A new client has connected to you");
    pthread_t client_new;
    pthread_create(&client_new, NULL, &client_thread, new_client);
  }
}