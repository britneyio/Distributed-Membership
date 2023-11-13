#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/time.h>
#include <pthread.h>

#define MYPORT "4950"
#define UDP "4951"
#define BACKLOG 10

enum messageType
{
    REQ,
    OK,
    NEWVIEW,
    NEWLEADER,
    JOIN,
    HEARTBEAT
};
enum operationType
{
    ADD,
    DEL,
    PENDING,
    NOTHING
};
struct message
{
    int id;
    int request_id;
    int currentView_id;
    enum messageType m;
    enum operationType op;
    int *membership_list;
    int member_count;
    int peer_id;
};

// peer list
char **peer_list;

// peer send sockets
int *peer_fd;
int *udp_fd;
int peer_count = 0;
struct sockaddr *udp_addr_list[5];
socklen_t udp_addr_len[5];
int udp_addr_i = 0;



// listening socket
int listener;

// leader flag
int is_leader = 0;

// send to leader
int leader_fd;
int leader_id;

struct message current_operation;

// current peer id
int id;
pthread_mutex_t hb_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t coml_mutex = PTHREAD_MUTEX_INITIALIZER;
int heartbeat_count = 0;
int *heartbeat_list;
int peer_crashed = 0;
// delays
int start_delay = 0;
int crash_delay = 0;
char *file;

int ok_count;

int leader_will_crash = 0;


// Create the listening socket
int create_socket_and_bind(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p);
// create a send socket
int create_send_socket(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p, char *host, int token);
// create all send sockets for peer list
void initialize_peer_send_list(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p);
void *get_in_addr(struct sockaddr *sa);

// Testcase 1
// if peer starts will contact leader: use create_send_socket send join message
int send_join();
// then leader sends a REQ message to all peers with ADD op
int send_req(struct message m, enum operationType op);
// peers save the operation then sends back an OK message
int receive_req_send_ok(struct message received);
// after receiving OK from all peers with matching request id and view id
// the leader increments the view and sends membership list
int receive_ok_newview(struct message recieved);
// sends NEWVIEW message
// when peer receieves NEWVIEW also increments view id and update membership list
int receive_newview_updateview(struct message recieved);

// Testcase 2
int create_udp_listen(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p);
int initialize_heartbeat_list(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p);
int heartbeart_recvfrom(int listener_fd);
int heartbeat_send(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p);
// every peer broadcast heartbeat
// increment heartbeat_list at the peer_fd when it receives
// heartbeat from that peer
int receive_heartbeat(int peer_fd);
// if heartbeat_count == 3 and there is a peer in the list that doesnt have a count of 2



// Testcase 3
// leader sends REQ message with DEL
// use send_req() method
// peers send back OK use receive_req_send_ok
// same as T1

// Testcase 4
// if failure is called on the leader
// member with lowest id is the new leader
int set_new_leader();
// receive the new leader id
int receive_newleader();
// send the new leader id as the new leader
int send_newleader();
// create a membership list with a new peer added
void create_membership_list(int *member_list, int new_member, int size, int *output);
int message_type_determination(struct message received, int fd);
void get_peer_list(char *filename);
void free_allocated_memory();
void command_inputs(int argc, char *argv[]);
void print_member_array(const int *arr, int size);
void serialize_message(const struct message *msg, char *buffer, int buffer_size);
void unserialize_message(char *buffer, int buffer_size, struct message *msg);
int write_to_socket(int socket_fd, const char *data, size_t size);
void *timerThread(void *arg);
void handle_peer_failure(int peer_id);
void leader_delete_member(int valueToRemove);
int max(int a, int b);
// create a list of fd to send over udp
int main(int argc, char *argv[])
{
    fd_set read_fds; // read_fds file descriptor list for tcp
    int fdmax;       // max file descriptor number
    fd_set master_fds;
    FD_ZERO(&master_fds); // set to empty
    FD_ZERO(&read_fds);
    struct addrinfo hints, *servinfo, *p;
    struct timeval timeout;
    timeout.tv_sec = 1; // 1-second timeout
    timeout.tv_usec = 0;
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    int nbytes;
    char received_buffer[1024];
    size_t received_size;
    struct message to_send;
    command_inputs(argc, argv);
    get_peer_list(file);

    // int udp_listener = create_udp_listen(hints, servinfo, p);

    // initialize listening socket
    listener = create_socket_and_bind(hints, servinfo, p);
    int udp_listener = create_udp_listen(hints, servinfo, p);

           int flags = fcntl(udp_listener, F_GETFL, 0);
    fcntl(udp_listener, F_SETFL, flags | O_NONBLOCK);

    // keep track of the biggest file descriptor

    // start listening
    if (listen(listener, BACKLOG) == -1)
    {
        perror("server: listen");
    }

    fdmax = max(listener, udp_listener) + 1;
    //fdmax = listener;

    FD_SET(listener, &master_fds); // add listener to the master set
    FD_SET(udp_listener, &master_fds);

    // give time for listener to get set up before initializing peer list
    sleep(2);

    // set up send socket for all peers
    initialize_peer_send_list(hints, servinfo, p);

    if (is_leader == 1)
    {
        // current_operation.id = 0;
        current_operation.request_id = 0;
        current_operation.currentView_id = 0;
        current_operation.member_count = 1;
        current_operation.membership_list = (int*) malloc(current_operation.member_count * sizeof(int));
        memset(current_operation.membership_list, 0, sizeof(int) * current_operation.member_count);
        current_operation.membership_list[0] = current_operation.id;
        heartbeat_list = (int*) malloc((current_operation.member_count) * sizeof(int));
        memset(heartbeat_list, 0, sizeof(int) * current_operation.member_count);
    }

    if (is_leader == 0)
    {
        // current_operation.id = -1;
        current_operation.request_id = -1;
        current_operation.currentView_id = -1;
        current_operation.member_count = 0;
        current_operation.membership_list = malloc(1 * sizeof(int));
        heartbeat_list = malloc((current_operation.member_count) * sizeof(int));
        memset(heartbeat_list, 0, sizeof(int) * current_operation.member_count);
    }

    // if this peer isn't the leader
    if (is_leader == 0)
    {
            sleep(start_delay);

        // send a join message to the leader
        send_join(); // done and checked need to test


    }

    pthread_t tid;
    if (pthread_create(&tid, NULL, timerThread, NULL) != 0) {
            perror("Thread creation failed");
            exit(0);
    }

    while (1)
    {
        // if(leader_will_crash == 1) {
        //     if (current_operation.member_count == peer_count) {
        //         send_req(current_operation, DEL);
        //         exit(0);
        //     }
        // }

        read_fds = master_fds;
        if (select(fdmax + 1, &read_fds, NULL, NULL, &timeout) == -1)
        {
            perror("select");
            exit(4);
        }

        for (int i = 0; i <= fdmax; i++)
        {
            if (FD_ISSET(i, &read_fds))
            {
                if (i == listener)
                {
                    // handle new connections
                    // accept from channels
                    socklen_t addr_size = sizeof their_addr;
                    int accept_fd = accept(listener, (struct sockaddr *)&their_addr, &addr_size);
                    fcntl(accept_fd, F_SETFL, O_NONBLOCK);
                    // fprintf(stderr, "accepted, fd: %d\n", accept_fd);

                    if (accept_fd == -1)
                    {
                        perror("accept");
                    }
                    else
                    {

                        FD_SET(accept_fd, &master_fds); // add to read_fds set
                        if (accept_fd > fdmax)
                        {
                            fdmax = accept_fd;
                        }
                    }
                }

                else if(i == udp_listener) {
                    heartbeart_recvfrom(udp_listener);

                }


                else
                {
                    if ((nbytes = recv(i, &received_buffer, sizeof(received_buffer), 0)) > 0)
                    {
                        // fprintf(stderr, "Start unserialization\n");
                        struct message m;
                        unserialize_message(received_buffer, sizeof(received_buffer), &m);
                        // fprintf(stderr, "Unserialized message\n");
               
                      
                        message_type_determination(m, i);
                        free(m.membership_list);
                    }
                
                    else
                    {
                        perror("recv");

                        close(i);
                        FD_CLR(i, &master_fds); // remove from read_fds set
                    }
                }
            }
        }
    }
    free_allocated_memory();
    return 0;
}

int message_type_determination(struct message received, int fd)
{
    if (received.m == REQ)
    {
        // if no leader set then set it
        // if (!leader_fd) {
        //     leader_fd = fd;
        // }
        // fprintf(stderr, "receiving REQ from %d\n", received.id+1);

        receive_req_send_ok(received); // done and checked need to test
    }

    // leader receives join, join will only contain peer id
    if (received.m == JOIN)
    {
        // fprintf(stderr, "receiving JOIN from %d\n", received.id+1);
        if (1 == is_leader)
        {
            send_req(received, ADD);
        }
    }

    if (received.m == OK)
    {
        // fprintf(stderr, "receiving OK from %d\n", received.id+1);
        // fprintf(stderr, "current member count %d", current_operation.member_count);
        ok_count++;

        if (1 == is_leader && ok_count == current_operation.member_count)
        {
            // fprintf(stderr, "received all\n");
            receive_ok_newview(received);
            ok_count = 0;
        }

        if (1 == is_leader && received.op == DEL && ok_count >= current_operation.member_count - 1) {

            receive_ok_newview(received);
            ok_count = 0;
        }
        // adds OK to list, if all peers have sent OK then will start newview
    }

    if (received.m == NEWVIEW)
    {
        // fprintf(stderr, "receiving NEWVIEW from %d\n", received.id);

        receive_newview_updateview(received);
                if (current_operation.member_count == 5 && crash_delay != 0)
        {
            sleep(crash_delay);
            exit(0);
        }

        if (current_operation.member_count == 5 && leader_will_crash == 1)
        {
            struct message m;
            m.id = 4;
            send_req(m, DEL);
            sleep(2);
            exit(0);
        }
    }

    if (received.m == NEWLEADER)
    {
        fprintf(stderr, "receiveed NEWLEADER\n");
        if (is_leader == 0){
        receive_newleader(received);
        }

        if (is_leader == 1) {
                struct message m;
                m.id = received.peer_id;
                fprintf(stderr, "op: %d", received.op);
                send_req(m, received.op);
        }
    }

    return 0;
}

// peer
int send_join()
{

    struct message m;
    m.m = JOIN;
    m.currentView_id = -1;
    m.id = current_operation.id;
    m.member_count = 0;
    m.op = NOTHING;
    m.peer_id = 0; // leader
    m.request_id = -1;
    m.membership_list = NULL;
    int nbytes;

    int buffer_size = sizeof(int) * 3 + sizeof(enum messageType) + sizeof(enum operationType) +
                      2 * sizeof(int) + sizeof(int) * m.member_count;
    char *buffer = (char *)malloc(buffer_size);
    serialize_message(&m, buffer, buffer_size);
    // fprintf(stderr, "Serialized message\n");

    if ((nbytes = write_to_socket(leader_fd, buffer, buffer_size)) != 0)
    {
        fprintf(stderr, "error writing my message\n");
    }
    else
    {
        // fprintf(stderr, "JOIN SENT\n");
    }
    free(buffer);
    free(m.membership_list);
    buffer_size = 0;
}

int send_req(struct message recieved, enum operationType op)
{
    if (op == NOTHING) {
        return 0;
    }
    fprintf(stderr, "sending REQ\n");
    current_operation.peer_id = recieved.id;
    fprintf(stderr, "peer_id: %d\n",recieved.id);
    struct message m;
    m.request_id = current_operation.request_id;
    m.currentView_id = current_operation.currentView_id;
    m.op = op;
    m.peer_id = recieved.id;
    m.m = REQ;
    m.id = current_operation.id;
    m.member_count = current_operation.member_count;
    m.membership_list = (int*) malloc(m.member_count * sizeof(int));
    memset(m.membership_list, 0, sizeof(int) * m.member_count);
    for (int i = 0; i < m.member_count; i++)
    {
        m.membership_list[i] = current_operation.membership_list[i];
    }    // fprintf(stderr, "copied membership\n");
    int nbytes;
    int buffer_size = sizeof(int) * 3 + sizeof(enum messageType) + sizeof(enum operationType) +
                      2 * sizeof(int) + sizeof(int) * m.member_count;
    char *buffer = (char *)malloc(buffer_size);
    serialize_message(&m, buffer, buffer_size);
    // fprintf(stderr, "Serialized message\n");

    for (int i = 0; i < current_operation.member_count; i++)
    {
        // if (i == leader_id + 1 && leader_will_crash == 1){
        //     // new leader will not receive the instruction
        
        //     continue;
        // }
        if ((nbytes = write_to_socket(peer_fd[current_operation.membership_list[i]], buffer, buffer_size)) != 0)
        {
            fprintf(stderr, "error writing my message\n");
        }
        else
        {
            // fprintf(stderr, "%d REQ SENT to %d\n", op, current_operation.membership_list[i]+1);
        }
    }
    free(buffer);
    buffer_size = 0;
    free(m.membership_list);
    return 0;
}

int receive_req_send_ok(struct message received)
{
    // save the operation
    current_operation.currentView_id = received.currentView_id;
    current_operation.request_id = received.request_id;
    current_operation.m = received.m;
    current_operation.op = received.op;

    // implement send
    struct message m;
    m.currentView_id = current_operation.currentView_id;
    m.request_id = current_operation.request_id;
    m.m = OK;
    m.id = current_operation.id;
    m.member_count = current_operation.member_count;
    m.op = current_operation.op;
    m.peer_id = received.peer_id; // leader
    m.membership_list = malloc(m.member_count * sizeof(int));
    memset(m.membership_list, 0, sizeof(int) * m.member_count);
    memcpy(m.membership_list, current_operation.membership_list, current_operation.member_count * sizeof(int));
    //_array(m.membership_list, m.member_count);
    int nbytes;
    int buffer_size = sizeof(int) * 3 + sizeof(enum messageType) + sizeof(enum operationType) +
                      2 * sizeof(int) + sizeof(int) * m.member_count;
    char *buffer = (char *)malloc(buffer_size);
    serialize_message(&m, buffer, buffer_size);
    // fprintf(stderr, "Serialized message\n");

    if ((nbytes = write_to_socket(leader_fd, buffer, buffer_size)) != 0)
    {
        fprintf(stderr, "error writing my message\n");
    }
    else
    {
        // fprintf(stderr, "OK SENT\n");
    }

    free(m.membership_list);
    free(buffer);

    return 0;
}

int leader_add_member()
{
    int new_size = current_operation.member_count;
    new_size++;
    int *output = malloc(new_size * sizeof(int));
    memset(output, 0, new_size * sizeof(int));
        for (int i = 0; i < new_size; i++)
    {
          output[i] = current_operation.membership_list[i];
    }

    output[new_size-1] = current_operation.peer_id;
    // fprintf(stderr, "output:\n");
    // print_member_array(output, new_size);
    memset(current_operation.membership_list, 0, new_size * sizeof(int));

    current_operation.membership_list = realloc(current_operation.membership_list, new_size * sizeof(int));
    for (int i = 0; i < new_size; i++)
    {
        current_operation.membership_list[i] = output[i];
    }
    // fprintf(stderr, "co:\n");
    // print_member_array(current_operation.membership_list, new_size);
    free(output);
}

void leader_delete_member(int valueToRemove) {
    int i, j;
    int new_size = current_operation.member_count - 1;
    int *output = malloc(new_size * sizeof(int));

    // Iterate through the array to find the value
    j = 0;
    for (i = 0; i < current_operation.member_count; ++i) {
        if (current_operation.membership_list[i] !=  valueToRemove) {
            output[j++] = current_operation.membership_list[i];
        }
    }

    current_operation.membership_list = realloc(current_operation.membership_list, new_size * sizeof(int));

    // Copy values from output to current_operation.membership_list
    for (int i = 0; i < new_size; i++) {
        current_operation.membership_list[i] = output[i];
    }

    free(output);
}


int receive_ok_newview(struct message received)
{
    fprintf(stderr, "receive ok new view\n");
    struct message m;
    if (current_operation.op == ADD)
    {
        leader_add_member();
        current_operation.member_count++;
    }
    current_operation.currentView_id++;

    if (current_operation.op == DEL)
    {
        leader_delete_member(received.peer_id);
        current_operation.member_count--;
        peer_crashed = 0;
    }

    m.currentView_id = current_operation.currentView_id;
    m.m = NEWVIEW;
    m.request_id = current_operation.request_id;
    current_operation.request_id++;
    m.id = current_operation.id;
    m.member_count = current_operation.member_count;
    m.op = received.op;
    m.peer_id = current_operation.peer_id; // peer being added
    m.membership_list = malloc(m.member_count * sizeof(int));
    memset(m.membership_list, 0, m.member_count * sizeof(int));

    for (int i = 0; i < m.member_count; i++)
    {
        m.membership_list[i] = current_operation.membership_list[i];
    }
    // fprintf(stderr, "newview stuff");
    // print_member_array(m.membership_list, m.member_count);

    int nbytes;
    int buffer_size = sizeof(int) * 3 + sizeof(enum messageType) + sizeof(enum operationType) +
                      2 * sizeof(int) + sizeof(int) * m.member_count;
    char *buffer = (char *)malloc(buffer_size);
    serialize_message(&m, buffer, buffer_size);
    // fprintf(stderr, "Serialized message\n");
                fprintf(stderr, "NEWVIEW SENT\n");
                //print_member_array(current_operation.membership_list, current_operation.member_count);
                //print_member_array(m.membership_list, m.member_count);


    for (int i = 0; i < current_operation.member_count; i++)
    {
        // fprintf(stderr, "current_operation.member_list[%d] : %d\n", i, current_operation.membership_list[i]);
        if ((nbytes = write_to_socket(peer_fd[current_operation.membership_list[i]], buffer, buffer_size)) != 0)
        {
            fprintf(stderr, "error writing my message\n");
        }
        else
        {
            // fprintf(stderr, "NEWVIEW SENT to %d\n", current_operation.membership_list[i] + 1);
        }
    }

    free(m.membership_list);
    free(buffer);
    return 0;
}

int receive_newview_updateview(struct message received)
{
        current_operation.currentView_id = received.currentView_id;
    current_operation.peer_id = received.peer_id;
    // fprintf(stderr, "update view: ");
    // print_member_array(received.membership_list, received.member_count);

    free(current_operation.membership_list);
    current_operation.membership_list = malloc(received.member_count * sizeof(int));
    memset(current_operation.membership_list , 0, received.member_count * sizeof(int));
        for (int i = 0; i < received.member_count; i++)
    {
        current_operation.membership_list[i] = received.membership_list[i];
    }

    heartbeat_list = realloc(heartbeat_list, (received.member_count) * sizeof(int));
    memset(heartbeat_list, 0, (received.member_count) * sizeof(int));
    pthread_mutex_lock(&hb_mutex);
    heartbeat_count = 0;
    pthread_mutex_unlock(&hb_mutex);
    // fprintf(stderr, "heartbeat list:");
    // print_member_array(heartbeat_list, received.member_count);
    current_operation.member_count = received.member_count;
    fprintf(stderr, "viewID: %d, ", current_operation.currentView_id);
    print_member_array(current_operation.membership_list, current_operation.member_count);
    current_operation.op = NOTHING;
}

int send_newleader()
{
    struct message m;
    m.request_id = current_operation.request_id++;
    m.currentView_id = current_operation.currentView_id;
    m.op = PENDING;
    m.m = NEWLEADER;
    m.id = current_operation.id;
    m.member_count = current_operation.member_count;
    m.peer_id = current_operation.peer_id; // peer being added
    m.membership_list = malloc(m.member_count * sizeof(int));
    memset(m.membership_list, 0, m.member_count * sizeof(int));

    for (int i = 0; i < m.member_count; i++)
    {
        m.membership_list[i] = current_operation.membership_list[i];
    }

    int nbytes;
    int buffer_size = sizeof(int) * 3 + sizeof(enum messageType) + sizeof(enum operationType) +
                      2 * sizeof(int) + sizeof(int) * m.member_count;
    char *buffer = (char *)malloc(buffer_size);

    serialize_message(&m, buffer, buffer_size);
    fprintf(stderr, "Serialized message\n");

    for (int i = 0; i < current_operation.member_count; i++)
    {
        if (current_operation.membership_list[i] == leader_id) {
            continue;
        }

        // fprintf(stderr, "current_operation.member_list[%d] : %d\n", i, current_operation.membership_list[i]);
        if ((nbytes = write_to_socket(peer_fd[current_operation.membership_list[i]], buffer, buffer_size)) != 0)
        {
            fprintf(stderr, "error writing my message\n");
        }
        else
        {
            fprintf(stderr, "NEWLEADER SENT to %d\n", current_operation.membership_list[i]);
        }
    }
    free(buffer);
    return 0;
}

int receive_newleader(struct message received)
{
    // implement send
    struct message m;
    m.currentView_id = received.currentView_id;
    m.request_id = received.request_id;
    m.op = current_operation.op;
    m.peer_id = current_operation.peer_id;
    m.m = NEWLEADER;
    m.id = current_operation.id;
    m.member_count = current_operation.member_count;
    m.peer_id = current_operation.peer_id; // peer being added
    m.membership_list = malloc(m.member_count * sizeof(int));
    memset(m.membership_list, 0, m.member_count * sizeof(int));

    for (int i = 0; i < m.member_count; i++)
    {
        m.membership_list[i] = current_operation.membership_list[i];
    }
    int nbytes;
    int buffer_size = sizeof(int) * 3 + sizeof(enum messageType) + sizeof(enum operationType) +
                      2 * sizeof(int) + sizeof(int) * m.member_count;
    char *buffer = (char *)malloc(buffer_size);
    serialize_message(&m, buffer, buffer_size);
    // fprintf(stderr, "Serialized message\n");

    if ((nbytes = write_to_socket(leader_fd, buffer, buffer_size)) != 0)
    {
        fprintf(stderr, "error writing my message\n");
    }
    else
    {
        fprintf(stderr, "NEWLEADER SENT\n");
    }

    //free(m.membership_list);
    free(buffer);

    return 0;
}

// create sockets
int create_send_socket(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p, char *host, int token)
{
    int sock_fd;
    char ipstr[100];
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
        if (token == 1) {
    hints.ai_socktype = SOCK_DGRAM;
    } else {
    hints.ai_socktype = SOCK_STREAM;
    }
    if ((status = getaddrinfo(host, MYPORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }
    int yes = 1;

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }

        setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (token != 1) {
        if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1)
        {
            perror("server: connect");
            continue;
        }
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "error in socket");
    }
            int flags = fcntl(sock_fd, F_GETFL, 0);
        fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);


    freeaddrinfo(servinfo);

    return sock_fd;
}

int create_socket_and_bind(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p)
{
    int sock_fd;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }
    int yes = 1;

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
    
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }

        setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        /** step 4 **/
        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1)
        {
            perror("server: bind");
            continue;
        }

        // fprintf(stderr, "socket binded: %s\n", ipstr);
        int flags = fcntl(sock_fd, F_GETFL, 0);
        fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK);

        break;
    }

   if (servinfo != NULL) {
    freeaddrinfo(servinfo);
}
    return sock_fd;
}


void get_peer_list(char *filename)
{
    char hostname[10];
    hostname[9] = '\0';
    FILE *fptr;
    peer_list = malloc(10 * sizeof(char *));
    if (peer_list == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    int host_i = 0;
    // fprintf(stderr, "Opening host file:%s\n", filename);
    // Open file
    fptr = fopen(filename, "r");
    if (fptr == NULL)
    {
        printf("Cannot open file \n");

        exit(0);
    }

    gethostname(hostname, 10);
    fprintf(stderr, "hostname: %s\n", hostname);
    char name[10];
    // Read contents from file
    while (fscanf(fptr, "%s", name) == 1)

    {
        peer_list[host_i] = strdup(name);
        // fprintf(stderr, "peerlist[%d]: %s\n", host_i, peer_list[host_i]);

        if (strcmp(peer_list[host_i], hostname) == 0)
        {
            current_operation.id = host_i;
            if (host_i == 0)
            {
                is_leader = 1;
                
            }
        }
        host_i++;
    }
    leader_id = 0;
    peer_count = host_i;
    if (peer_count <= 0) {
        fprintf(stderr, "peer_count is invalid");
    }
    fclose(fptr);
}

void initialize_peer_send_list(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p)
{
    peer_fd = (int*) malloc((peer_count) * sizeof(int));

    memset(peer_fd, 0, (peer_count) * sizeof(int));

    for (int i = 0; i < peer_count; i++)
    {
        if (i == 0)
        {
            leader_fd = create_send_socket(hints, servinfo, p, peer_list[i], 0);

        }

        peer_fd[i] = create_send_socket(hints, servinfo, p, peer_list[i], 0);

    }
                



}



void free_allocated_memory()
{
    free(peer_list);
    free(peer_fd);
    free(file);
    free(heartbeat_list);
}

void command_inputs(int argc, char *argv[])
{
    for (int i = 0; i < argc; i++)
    {
        // fprintf(stderr, "argv[%d] = '%s' \n", i, argv[i]);
        if (strcmp(argv[i], "-d") == 0)
        {
            start_delay = atof(argv[i + 1]);
        }
        if (strcmp(argv[i], "-c") == 0)
        {
            crash_delay = atof(argv[i + 1]);
        }
        if (strcmp(argv[i], "-h") == 0)
        {
            file = argv[i + 1];
        }
        if (strcmp(argv[i], "-t") == 0)
        {
            leader_will_crash = 1;
        }
    }
}

void print_member_array(const int *arr, int size)
{
    // fprintf(stderr, "current membership list: ");
    for (int i = 0; i < size; i++)
    {
        fprintf(stderr, "%d ", arr[i] + 1);
    }
    fprintf(stderr, "\n"); // Add a newline at the end
}


void serialize_message(const struct message *msg, char *buffer, int buffer_size)
{
    int offset = 0;

    // Convert values to network byte order before serialization
    int id_network = htonl(msg->id);
    memcpy(buffer + offset, &id_network, sizeof(int));
    offset += sizeof(int);

    int request_id_network = htonl(msg->request_id);
    memcpy(buffer + offset, &request_id_network, sizeof(int));
    offset += sizeof(int);

    int currentView_id_network = htonl(msg->currentView_id);
    memcpy(buffer + offset, &currentView_id_network, sizeof(int));
    offset += sizeof(int);

    memcpy(buffer + offset, &(msg->m), sizeof(enum messageType));
    offset += sizeof(enum messageType);

    memcpy(buffer + offset, &(msg->op), sizeof(enum operationType));
    offset += sizeof(enum operationType);

    int member_count_network = htonl(msg->member_count);
    memcpy(buffer + offset, &member_count_network, sizeof(int));
    offset += sizeof(int);

    int peer_id_network = htonl(msg->peer_id);
    memcpy(buffer + offset, &peer_id_network, sizeof(int));
    offset += sizeof(int);

    // Convert membership_list values to network byte order before serialization
    for (int i = 0; i < msg->member_count; ++i) {
        int membership_list_element_network = htonl(msg->membership_list[i]);
        memcpy(buffer + offset, &membership_list_element_network, sizeof(int));
        offset += sizeof(int);
    }

    // Check if the offset exceeds the buffer size
    if (offset > buffer_size)
    {
        fprintf(stderr, "Error: Buffer overflow in serialize_message.\n");
        // Handle the error appropriately for your program
        return;
    }
}


int write_to_socket(int socket_fd, const char *data, size_t size)
{
    ssize_t bytes_written = send(socket_fd, data, size, MSG_DONTWAIT);
    if (bytes_written < 0)
    {
        perror("Error writing to socket");
        return -1;
    }
    return 0;
}

// Deserialize data into a message struct
void unserialize_message(char *buffer, int buffer_size, struct message *msg)
{
    int offset = 0;

    // Copy each field from the buffer to the struct
    memcpy(&(msg->id), buffer + offset, sizeof(int));
    msg->id = ntohl(msg->id);  // Convert to host byte order
    offset += sizeof(int);

    memcpy(&(msg->request_id), buffer + offset, sizeof(int));
    msg->request_id = ntohl(msg->request_id);
    offset += sizeof(int);

    memcpy(&(msg->currentView_id), buffer + offset, sizeof(int));
    msg->currentView_id = ntohl(msg->currentView_id);
    offset += sizeof(int);

    memcpy(&(msg->m), buffer + offset, sizeof(enum messageType));
    offset += sizeof(enum messageType);

    memcpy(&(msg->op), buffer + offset, sizeof(enum operationType));
    offset += sizeof(enum operationType);

    memcpy(&(msg->member_count), buffer + offset, sizeof(int));
    msg->member_count = ntohl(msg->member_count);
    offset += sizeof(int);

    memcpy(&(msg->peer_id), buffer + offset, sizeof(int));
    msg->peer_id = ntohl(msg->peer_id);
    offset += sizeof(int);

    // Allocate memory for the membership_list based on member_count
    msg->membership_list = (int *)malloc(msg->member_count * sizeof(int));
    
    // Convert each element in membership_list to host byte order
    for (int i = 0; i < msg->member_count; ++i) {
        memcpy(&(msg->membership_list[i]), buffer + offset, sizeof(int));
        msg->membership_list[i] = ntohl(msg->membership_list[i]);
        offset += sizeof(int);
    }

    // Check if the offset exceeds the buffer size
    if (offset > buffer_size)
    {
        fprintf(stderr, "Error: Buffer overflow in unserialize_message.\n");
        // Handle the error appropriately for your program
        return;
    }
}


int create_udp_listen(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p)
{

    int listener_fd;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(NULL, UDP, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {

        if ((listener_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }

        if (bind(listener_fd, p->ai_addr, p->ai_addrlen) == -1)
        {
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);
    return listener_fd;
}

int heartbeart_recvfrom(int listener_fd)
{

    struct sockaddr_storage their_addr;
    socklen_t addr_len = sizeof their_addr;
    char recv_buf[128];
    char host[128];
    char service[20];
    int size;
    int status;

    memset(&their_addr, 0, sizeof their_addr);
    memset(&recv_buf, 0, sizeof recv_buf);



    ssize_t bytes_read = recvfrom(listener_fd, (char *)recv_buf,
                                  127, 0,
                                  (struct sockaddr *)&their_addr,
                                  &addr_len);

    if (bytes_read != -1)
    {
        if (their_addr.ss_family == AF_INET)
        {
            size = sizeof(struct sockaddr_in);
        }
        else
        {
            size = sizeof(struct sockaddr_in6);
        }

        memset(host, 0, sizeof host);
        memset(service, 0, sizeof service);

        if ((status = getnameinfo((struct sockaddr *)&their_addr, size, host, sizeof host, service, sizeof service, 0)) != 0)
        {
            fprintf(stderr, "getnameinfo error: %s\n", gai_strerror(status));
            exit(1);
        }
        
        // fprintf(stderr, "peer %d: recv_count: %d, my count: %d\n  peer %d: recv_count: %d, my count: %d\n  peer %d: recv_count: %d, my count: %d\n peer %d: recv_count: %d, my count: %d\n peer %d: recv_count: %d, my count: %d\n", 1, heartbeat_list[0], heartbeat_count, 
        // 2, heartbeat_list[1], heartbeat_count, 3, heartbeat_list[2], heartbeat_count, 4, heartbeat_list[3], heartbeat_count, 5, heartbeat_list[4], heartbeat_count);
        const char s[2] = ".";
        char *token = strtok(host, s);

        // fprintf(stderr, "recieved %s from %s\n", recv_buf, token);
        for (int i = 0; i < current_operation.member_count; i++)
        {
            if (strcmp(token, peer_list[current_operation.membership_list[i]]) == 0)
            {
                // fprintf(stderr, "peer %d: recv_count: %d, my count: %d\n", i+1, heartbeat_list[i], heartbeat_count);
                heartbeat_list[i]++;
            }
        }
    }

    return 0;
}

void handle_peer_failure(int peer_id) {
    fprintf(stderr, "Peer %d not reachable.\n", peer_id + 1);
    if (is_leader == 1 && peer_crashed == 0) {
    struct message m;
    m.id = peer_id;
    send_req(m, DEL);
    peer_crashed = 1;

    }

    if (is_leader == 0 && leader_id == peer_id) {
        leader_fd = peer_fd[current_operation.membership_list[leader_id + 1]];
        leader_delete_member(peer_id);
        current_operation.member_count--;
        fprintf(stderr, "deleting old leader from list: ");
        print_member_array(current_operation.membership_list, current_operation.member_count);
        heartbeat_list = realloc(heartbeat_list, (current_operation.member_count) * sizeof(int));
        memset(heartbeat_list, 0, (current_operation.member_count) * sizeof(int));
        pthread_mutex_lock(&hb_mutex);
        heartbeat_count = 0;
        pthread_mutex_unlock(&hb_mutex);
        if (current_operation.id == leader_id + 1) {
            is_leader = 1;
            fprintf(stderr, "Sending NEWLEADER\n");
            send_newleader();
        }
    }
}

int heartbeat_send(struct addrinfo hints, struct addrinfo *servinfo, struct addrinfo *p)
{
    int status;
    int send_fd;
    char send_buf[] = "HEARTBEAT";

        for(int g = 0; g < current_operation.member_count; g++) {

        memset(&hints, 0, sizeof hints);
        memset(&servinfo, 0, sizeof servinfo);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE;
        
       // fprintf(stderr, "hostname: %s\n", hosts[g]);
        status = getaddrinfo(peer_list[current_operation.membership_list[g]], UDP, &hints, &servinfo);

        if(status != 0) {
            // fprintf(stderr, "getaddrinfo-talker: %s\n", gai_strerror(status));
           // exit(1);
           continue;
        }

     for(p = servinfo; p != NULL; p = p->ai_next) {
        send_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (send_fd == -1) {
            perror("peer: socket");
            continue;
        }
        break;

     }

    if (sendto(send_fd, send_buf, strlen(send_buf), MSG_DONTWAIT, p->ai_addr, p->ai_addrlen) == -1) {
                fprintf(stderr, "Error pinging host: %d\n", current_operation.membership_list[g]);
                exit(1);
            }
        }


    for (int g = 0; g < current_operation.member_count; g++)
    {

//                 // fprintf(stderr, "peer %d, heartbeat[]: %d, %d, \n", g+1,heartbeat_list[g], heartbeat_count);
               if (heartbeat_list[g] + 2 < heartbeat_count)
    {
                    handle_peer_failure(current_operation.membership_list[g]);

    }
    }

        // fprintf(stderr, "sent HEARTBEAT to %d\n", g+1);

//     }


    pthread_mutex_lock(&hb_mutex);
    heartbeat_count++;
    pthread_mutex_unlock(&hb_mutex);

    return 0;
}




void *timerThread(void *arg) {
    struct addrinfo hints, *servinfo, *p;
    while(1) {
    heartbeat_send(hints, servinfo, p);
    sleep(1); // Simulating a timer for 5 seconds
    }
}


int max(int a, int b) {
    return (a > b) ? a : b;
}


