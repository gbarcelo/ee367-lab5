/*
 * man.h
 */

#define MAN_MSG_LENGTH 1000


/*
 *  The next two structs are ports used to transfer commands
 *  and replies between the manager and hosts
 */

struct man_port_at_host {  /* Port located at the man */
	int host_id;
	int send_fd;
	int recv_fd;
	struct man_port_at_host *next;
};

struct man_port_at_man {  /* Port located at the host */
	int host_id;
	int send_fd;
	int recv_fd;
	struct man_port_at_man *next;
};

/* 
 * Main loop for the manager.  
 */
void man_main();


