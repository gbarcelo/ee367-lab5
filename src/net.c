/*
 * net.c
 *
 * Here is where pipes and sockets are created.
 * Note that they are "nonblocking".  This means that
 * whenever a read/write (or send/recv) call is made,
 * the called function will do its best to fulfill
 * the request and quickly return to the caller.
 *
 * Note that if a pipe or socket is "blocking" then
 * when a call to read/write (or send/recv) will be blocked
 * until the read/write is completely fulfilled.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <netdb.h>
#include <arpa/inet.h>

#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>

#include "main.h"
#include "man.h"
#include "host.h"
#include "net.h"
#include "packet.h"


#define MAX_FILE_NAME 100
#define PIPE_READ 0
#define PIPE_WRITE 1
#define CONFIG_LINE_MAX 120 // Maximum characters per line in .config file
#define MAX_HOSTS 127 // Maximum number of hosts
#define BACKLOG 5
#define MAX_BUF_SIZE 256
#define MAX_DOM_SIZE 80
#define MAX_PORT_SIZE 10

enum bool {
    FALSE, TRUE
};

/*
 * Struct used to store a link. It is used when the
 * network configuration file is loaded.
 */

struct net_link {
    enum NetLinkType type;
    int pipe_node0;
    int pipe_node1;
    char *internal_node_dom;
    char *internal_port;
    char *external_node_dom;
    char *external_port;
};


/*
 * The following are private global variables to this file net.c
 */
static enum bool g_initialized = FALSE; /* Network initialized? */
/* The network is initialized only once */

/*
 * g_net_node[] and g_net_node_num have link information from
 * the network configuration file.
 * g_node_list is a linked list version of g_net_node[]
 */
static struct net_node *g_net_node;
static int g_net_node_num;
static struct net_node *g_node_list = NULL;

/*
 * g_net_link[] and g_net_link_num have link information from
 * the network configuration file
 */
static struct net_link *g_net_link;
static int g_net_link_num;

/*
 * Global private variables about ports of network node links
 * and ports of links to the manager
 */
static struct net_port *g_port_list = NULL;

static struct man_port_at_man *g_man_man_port_list = NULL;
static struct man_port_at_host *g_man_host_port_list = NULL;

/*
 * Loads network configuration file and creates data structures
 * for nodes and links.  The results are accessible through
 * the private global variables
 */
int load_net_data_file();

/*
 * Creates a data structure for the nodes
 */
void create_node_list();

/*
 * Creates links, using pipes
 * Then creates a port list for these links.
 */
void create_port_list();

/*
 * Creates ports at the manager and ports at the hosts so that
 * the manager can communicate with the hosts.  The list of
 * ports at the manager side is p_m.  The list of ports
 * at the host side is p_h.
 */
void create_man_ports(
        struct man_port_at_man **p_m,
        struct man_port_at_host **p_h);

void net_close_man_ports_at_hosts();

void net_close_man_ports_at_hosts_except(int host_id);

void net_free_man_ports_at_hosts();

void net_close_man_ports_at_man();

void net_free_man_ports_at_man();

/*
 * Get the list of ports for host host_id
 */
struct net_port *net_get_port_list(int host_id);

/*
 * Get the list of nodes
 */
struct net_node *net_get_node_list();


/*
 * Remove all the ports for the host from linked lisst g_port_list.
 * and create another linked list.  Return the pointer to this
 * linked list.
 */
struct net_port *net_get_port_list(int host_id) {

    struct net_port **p;
    struct net_port *r;
    struct net_port *t;

    r = NULL;
    p = &g_port_list;

    while (*p != NULL) {
        if ((*p)->pipe_host_id == host_id) {
            t = *p;
            *p = (*p)->next;
            t->next = r;
            r = t;
        } else {
            p = &((*p)->next);
        }
    }

    return r;
}

/* Return the linked list of nodes */
struct net_node *net_get_node_list() {
    return g_node_list;
}

/* Return linked list of ports used by the manager to connect to hosts */
struct man_port_at_man *net_get_man_ports_at_man_list() {
    return (g_man_man_port_list);
}

/* Return the port used by host to link with other nodes */
struct man_port_at_host *net_get_host_port(int host_id) {
    struct man_port_at_host *p;

    for (p = g_man_host_port_list;
         p != NULL && p->host_id != host_id;
         p = p->next);

    return (p);
}

// Get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* Close all host ports not used by manager */
void net_close_man_ports_at_hosts() {
    struct man_port_at_host *p_h;

    p_h = g_man_host_port_list;

    while (p_h != NULL) {
        close(p_h->send_fd);
        close(p_h->recv_fd);
        p_h = p_h->next;
    }
}

/* Close all host ports used by manager except to host_id */
void net_close_man_ports_at_hosts_except(int host_id) {
    struct man_port_at_host *p_h;

    p_h = g_man_host_port_list;

    while (p_h != NULL) {
        if (p_h->host_id != host_id) {
            close(p_h->send_fd);
            close(p_h->recv_fd);
        }
        p_h = p_h->next;
    }
}

/* Free all host ports to manager */
void net_free_man_ports_at_hosts() {
    struct man_port_at_host *p_h;
    struct man_port_at_host *t_h;

    p_h = g_man_host_port_list;

    while (p_h != NULL) {
        t_h = p_h;
        p_h = p_h->next;
        free(t_h);
    }
}

/* Close all manager ports */
void net_close_man_ports_at_man() {
    struct man_port_at_man *p_m;

    p_m = g_man_man_port_list;

    while (p_m != NULL) {
        close(p_m->send_fd);
        close(p_m->recv_fd);
        p_m = p_m->next;
    }
}

/* Free all manager ports */
void net_free_man_ports_at_man() {
    struct man_port_at_man *p_m;
    struct man_port_at_man *t_m;

    p_m = g_man_man_port_list;

    while (p_m != NULL) {
        t_m = p_m;
        p_m = p_m->next;
        free(t_m);
    }
}


/* Initialize network ports and links */
int net_init() {
    if (g_initialized == TRUE) { /* Check if the network is already initialized */
        printf("Network already loaded\n");
        return (0);
    } else if (load_net_data_file() == 0) { /* Load network configuration file */
        return (0);
    }
/*
 * Create a linked list of node information at g_node_list
 */
    create_node_list();

/*
 * Create pipes and sockets to realize network links
 * and store the ports of the links at g_port_list
 */
    create_port_list();

/*
 * Create pipes to connect the manager to hosts
 * and store the ports at the host at g_man_host_port_list
 * as a linked list
 * and store the ports at the manager at g_man_man_port_list
 * as a linked list
 */
    create_man_ports(&g_man_man_port_list, &g_man_host_port_list);
}

/*
 *  Create pipes to connect the manager to host nodes.
 *  (Note that the manager is not connected to switch nodes.)
 *  p_man is a linked list of ports at the manager.
 *  p_host is a linked list of ports at the hosts.
 *  Note that the pipes are nonblocking.
 */
void create_man_ports(
        struct man_port_at_man **p_man,
        struct man_port_at_host **p_host) {
    struct net_node *p;
    int fd0[2];
    int fd1[2];
    struct man_port_at_man *p_m;
    struct man_port_at_host *p_h;
    int host;


    for (p = g_node_list; p != NULL; p = p->next) {
        if (p->type == HOST) {
            p_m = (struct man_port_at_man *)
                    malloc(sizeof(struct man_port_at_man));
            p_m->host_id = p->id;

            p_h = (struct man_port_at_host *)
                    malloc(sizeof(struct man_port_at_host));
            p_h->host_id = p->id;

            pipe(fd0); /* Create a pipe */
            /* Make the pipe nonblocking at both ends */
            fcntl(fd0[PIPE_WRITE], F_SETFL,
                  fcntl(fd0[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
            fcntl(fd0[PIPE_READ], F_SETFL,
                  fcntl(fd0[PIPE_READ], F_GETFL) | O_NONBLOCK);
            p_m->send_fd = fd0[PIPE_WRITE];
            p_h->recv_fd = fd0[PIPE_READ];

            pipe(fd1); /* Create a pipe */
            /* Make the pipe nonblocking at both ends */
            fcntl(fd1[PIPE_WRITE], F_SETFL,
                  fcntl(fd1[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
            fcntl(fd1[PIPE_READ], F_SETFL,
                  fcntl(fd1[PIPE_READ], F_GETFL) | O_NONBLOCK);
            p_h->send_fd = fd1[PIPE_WRITE];
            p_m->recv_fd = fd1[PIPE_READ];

            p_m->next = *p_man;
            *p_man = p_m;

            p_h->next = *p_host;
            *p_host = p_h;
        }
    }

}

/* Create a linked list of nodes at g_node_list */
void create_node_list() {
    struct net_node *p;
    int i;

    g_node_list = NULL;
    for (i = 0; i < g_net_node_num; i++) {
        p = (struct net_node *) malloc(sizeof(struct net_node));
        p->id = i;
        p->type = g_net_node[i].type;
        p->next = g_node_list;
        g_node_list = p;
    }

}

/*
 * Create links, each with either a pipe or socket.
 * It uses private global varaibles g_net_link[] and g_net_link_num
 */
void create_port_list() {
    struct net_port *p0;
    struct net_port *p1;
    int node0, node1;
    int fd01[2];
    int fd10[2];
    int i;

    g_port_list = NULL;
    for (i = 0; i < g_net_link_num; i++) {

      pipe(fd01);  /* Create a pipe */
      /* Make the pipe nonblocking at both ends */
      fcntl(fd01[PIPE_WRITE], F_SETFL,
            fcntl(fd01[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
      fcntl(fd01[PIPE_READ], F_SETFL,
            fcntl(fd01[PIPE_READ], F_GETFL) | O_NONBLOCK);

      pipe(fd10);  /* Create a pipe */
      /* Make the pipe nonblocking at both ends */
      fcntl(fd10[PIPE_WRITE], F_SETFL,
            fcntl(fd10[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
      fcntl(fd10[PIPE_READ], F_SETFL,
            fcntl(fd10[PIPE_READ], F_GETFL) | O_NONBLOCK);

      node0 = g_net_link[i].pipe_node0;
      node1 = g_net_link[i].pipe_node1; // TODO May have to edit

      p0 = (struct net_port *) malloc(sizeof(struct net_port));
      p0->type = g_net_link[i].type;
      p0->pipe_host_id = node0;

      p1 = (struct net_port *) malloc(sizeof(struct net_port));
      p1->type = g_net_link[i].type;
      p1->pipe_host_id = node1;

      // Pipe from Node0 to Node1
      p0->pipe_send_fd = fd01[PIPE_WRITE];
      p1->pipe_recv_fd = fd01[PIPE_READ];

      // Pipe from Node1 to Node0
      p1->pipe_send_fd = fd10[PIPE_WRITE];
      p0->pipe_recv_fd = fd10[PIPE_READ];

        if (g_net_link[i].type == PIPE) {

            p0->next = p1; /* Insert ports in linked lisst */
            p1->next = g_port_list;
            g_port_list = p0;

      } else if (g_net_link[i].type == SOCKET) {
          // Node1 represents socket/client

          /**************************Server Creation**************************/
          int server_socket;

          // =================== from lab 3 ===================
          int rvs;
          int yes=1;
          struct addrinfo hintss, *servinfos, *p;
          memset(&hintss, 0, sizeof hintss);
        	hintss.ai_family = AF_UNSPEC;
        	hintss.ai_socktype = SOCK_STREAM;
        	hintss.ai_flags = AI_PASSIVE; // use my IP
          if ((rvs = getaddrinfo(NULL, g_net_link[i].internal_port, &hintss, &servinfos)) != 0) {
        		fprintf(stderr, "server getaddrinfo: %s\n", gai_strerror(rvs));
        		return 1;
        	}

          // loop through all the results and bind to the first we can
        	for(p = servinfos; p != NULL; p = p->ai_next) {
            // Create server socket
        		if ((server_socket = socket(p->ai_family, p->ai_socktype,
        				p->ai_protocol)) == -1) {
        			perror("server: socket");
        			continue;
        		}

            // Making socket nonblocking
            int flags = fcntl(server_socket, F_GETFL, 0);   /* get socket's flags */
            flags |= O_NONBLOCK;  /* Add O_NONBLOCK status to socket descriptor's flags */
            int status = fcntl(server_socket, F_SETFL, flags); /* Apply the new flags to the socket */

        		if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes,
        				sizeof(int)) == -1) {
        			perror("setsockopt");
        			exit(1);
        		}

            // Bind the socket to specified IP and PORT
        		if (bind(server_socket, p->ai_addr, p->ai_addrlen) == -1) {
        			close(server_socket);
        			perror("server: bind");
        			continue;
        		}

        		break;
        	}

        	if (p == NULL)  {
        		fprintf(stderr, "server: failed to bind\n");
        		return 2;
        	}

        	freeaddrinfo(servinfos); // all done with this structure
          // =================== from lab 3 ===================

          // Start listening on socket for up to BACKLOG # of connections
          listen(server_socket, BACKLOG);

          // Any while loops would start here, before accept and after listen
          if(!fork()) { // Begin Child Process (Server)
            close(fd10[PIPE_READ]);
            close(fd01[PIPE_READ]);
            close(fd01[PIPE_WRITE]);
            int inbound_size;
            int client_socket;
            char server_message[MAX_BUF_SIZE];
            while(1){
              // puts("server while loop");
              // Accept a connection -- we now have a client to communicate with
              client_socket = accept(server_socket, NULL, NULL);
              while( (inbound_size) > 0 ) {
                inbound_size = recv(client_socket , server_message , sizeof(server_message) , 0);
              }
              if (inbound_size < 0) {perror("recv failed"); return 1;}
              write(fd10[PIPE_WRITE], server_message, sizeof(server_message));
              memset(server_message, 0, sizeof(server_message));
            }
          } else {  // Begin Parent Process
            close(fd10[PIPE_WRITE]);
            // Parent doesn't need socket
            close(server_socket);
          }
          /**************************Server Creation**************************/

          /**************************Client Creation**************************/
          if(!fork()){  // Child Process Start (Client)
            close(fd10[PIPE_READ]);
            close(fd10[PIPE_WRITE]);
            close(fd01[PIPE_WRITE]);

            int network_socket;

            // =================== from lab 3 ===================
            struct addrinfo hintsc, *servinfoc, *p;
          	int rvc;
          	char s[INET6_ADDRSTRLEN];

            memset(&hintsc, 0, sizeof hintsc);
          	hintsc.ai_family = AF_UNSPEC;
          	hintsc.ai_socktype = SOCK_STREAM;

            printf("LINK INFO BEFORE CLIENT GETADDRINFO\n");
            printf("link dom0: %s\n",g_net_link[i].internal_node_dom);
            printf("link port0: %s\n",g_net_link[i].internal_port);
            printf("link dom1: %s\n",g_net_link[i].external_node_dom);
            printf("link port1: %s\n",g_net_link[i].external_port);

          	if ((rvc = getaddrinfo(g_net_link[i].external_node_dom, g_net_link[i].external_port, &hintsc, &servinfoc)) != 0) {
          		fprintf(stderr, "client getaddrinfo: %s\n", gai_strerror(rvc));
          		return 1;
          	}

            // Keep attempting to connect
            // loop through all the results and connect to the first we can
          	for(p = servinfoc; p != NULL; p = p->ai_next) {
              // Create a socket
          		if ((network_socket = socket(p->ai_family, p->ai_socktype,
          				p->ai_protocol)) == -1) {
          			perror("client: socket");
          			continue;
          		}

          		if (connect(network_socket, p->ai_addr, p->ai_addrlen) == -1) {
          			close(network_socket);
          			perror("client: connect");
          			continue;
          		}

          		break;
          	}

          	if (p == NULL) {
          		fprintf(stderr, "client: failed to connect\n");
          		return 2;
          	}

          	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
          			s, sizeof s);
          	printf("client: connecting to %s\n", s);

          	freeaddrinfo(servinfoc); // all done with this structure
            // =================== from lab 3 ===================

            // While loop would start here
            // Buffer to recieve data from server_address
            char client_message[MAX_BUF_SIZE];
            int inbound_size;
            while(1){
              // TODO:
              // READ pipe and if not empty, do a send
              // IF RECV is not empty, WRITE TO PIPE
              // Recieve data from the server_address
              // if (recv(network_socket, &server_response, sizeof(server_response), 0)>0){
              while( (inbound_size) > 0 ) {
                inbound_size = read(fd01[PIPE_READ], client_message , sizeof(client_message));
              }
              if (inbound_size < 0) {perror("read failed"); return 1;}
              send(network_socket, client_message, sizeof(client_message), 0);
              memset(client_message, 0, sizeof(client_message));
              // } end if(recv...)
            }
            close(network_socket);
          } else {  // Parent Process Start
            close(fd01[PIPE_READ]);
          }
          /**************************Client Creation**************************/
          p0->next = p1; /* Insert ports in linked lisst */
          p1->next = g_port_list;
          g_port_list = p0;
      }
    }
}

/*
 * Loads network configuration file and creates data structures
 * for nodes and links.
 */
int load_net_data_file() {
    FILE *fp;
    char fdir[] = "config/";
    char fname[MAX_FILE_NAME];

    /* Open network configuration file */
    printf("Enter network data file: ");
    scanf("%s", fname);
    strcat(fdir, fname);
    fp = fopen(fdir, "r");
    if (fp == NULL) {
        printf("net.c: File did not open\n");
        return (0);
    }

    int i;
    int node_num;
    char node_type;
    int node_id;

    /*
     * Read node information from the file and
     * fill in data structure for nodes.
     * The data structure is an array g_net_node[ ]
     * and the size of the array is g_net_node_num.
     * Note that g_net_node[] and g_net_node_num are
     * private global variables.
     */
    fscanf(fp, "%d", &node_num);
    printf("Number of Nodes = %d: \n", node_num);
    g_net_node_num = node_num;

    if (node_num < 1) {
        printf("net.c: No nodes\n");
        fclose(fp);
        return (0);
    } else {
        g_net_node = (struct net_node *) malloc(sizeof(struct net_node) * node_num);
        for (i = 0; i < node_num; i++) {
            fscanf(fp, " %c ", &node_type);

            switch(node_type) {
              case 'S':
                fscanf(fp, " %d ", &node_id);
                g_net_node[i].type = SWITCH;
                g_net_node[i].id = node_id;
                break;
              case 'H':
                fscanf(fp, " %d ", &node_id);
                g_net_node[i].type = HOST;
                g_net_node[i].id = node_id;
                break;
              default:
                printf(" net.c: Unidentified Node Type\n");
                break;
            }

            if (i != node_id) {
                printf(" net.c: Incorrect node id\n");
                fclose(fp);
                return (0);
            }
        }
    }
    /*
     * Read link information from the file and
     * fill in data structure for links.
     * The data structure is an array g_net_link[ ]
     * and the size of the array is g_net_link_num.
     * Note that g_net_link[] and g_net_link_num are
     * private global variables.
     */

    int link_num;
    char link_type;
    int node0, node1;
    char sockStr[CONFIG_LINE_MAX];

    fscanf(fp, " %d ", &link_num);
    printf("Number of links = %d\n", link_num);
    g_net_link_num = link_num;

    if (link_num < 1) {
        printf("net.c: No links\n");
        fclose(fp);
        return (0);
    } else {
        g_net_link = (struct net_link *) malloc(sizeof(struct net_link) * link_num);
        for (i = 0; i < link_num; i++) {
            fscanf(fp, " %c ", &link_type);
            if (link_type == 'P') {
                fscanf(fp, " %d %d ", &node0, &node1);
                g_net_link[i].type = PIPE;
                g_net_link[i].pipe_node0 = node0;
                g_net_link[i].pipe_node1 = node1;
            } else if (link_type == 'S') {
                fscanf(fp, " %d ", &node0);
                if (fgets(sockStr, CONFIG_LINE_MAX, fp)!=NULL){
                  // for(char *p = strtok(sockStr," "); p!=NULL; p = strtok(NULL, " ")){}
                  // char *p = strtok(sockStr," ");
                  // char *cp;
                  int toksize;
                  g_net_link[i].type = SOCKET;
                  g_net_link[i].pipe_node0 = node0;

                  g_net_link[i].internal_node_dom = malloc(MAX_DOM_SIZE);
                  strcpy(g_net_link[i].internal_node_dom, strtok(sockStr," "));

                  g_net_link[i].internal_port = malloc(MAX_PORT_SIZE);
                  strcpy(g_net_link[i].internal_port, strtok(NULL," "));

                  g_net_link[i].pipe_node1 = node0+MAX_HOSTS; // Regular Host IDs are 0-127

                  g_net_link[i].external_node_dom = malloc(MAX_DOM_SIZE);
                  strcpy(g_net_link[i].external_node_dom, strtok(NULL," "));

                  g_net_link[i].external_port = malloc(MAX_PORT_SIZE);
                  strcpy(g_net_link[i].external_port, strtok(NULL," "));

                  printf("link dom0: %s\n",g_net_link[i].internal_node_dom);
                  printf("link port0: %s\n",g_net_link[i].internal_port);
                  printf("link dom1: %s\n",g_net_link[i].external_node_dom);
                  printf("link port1: %s\n",g_net_link[i].external_port);
                }

            } else {
                printf("   net.c: Unidentified link type\n");
            }

        }
    }

/* Display the nodes and links of the network */
    printf("Nodes:\n");
    for (i = 0; i < g_net_node_num; i++) {
        if (g_net_node[i].type == HOST) {
            printf("   Node %d HOST\n", g_net_node[i].id);
        } else if (g_net_node[i].type == SWITCH) {
            printf(" SWITCH\n");
        } else {
            printf(" Unknown Type\n");
        }
    }
    printf("Links:\n");
    for (i = 0; i < g_net_link_num; i++) {
        if (g_net_link[i].type == PIPE) {
            printf("   Link (%d, %d) PIPE\n",
                   g_net_link[i].pipe_node0,
                   g_net_link[i].pipe_node1);
        } else if (g_net_link[i].type == SOCKET) {
          printf("   Link (%d, EXTERNAL) SOCKET\n",
                 g_net_link[i].pipe_node0);
        }
    }

    fclose(fp);
    return (1);
}
