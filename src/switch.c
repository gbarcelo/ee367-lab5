/*
 * switch.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <unistd.h>
#include <fcntl.h>

#include "main.h"
#include "net.h"
#include "man.h"
#include "host.h"
#include "switch.h"
#include "packet.h"

#define MAX_FILE_BUFFER 1000
#define MAX_MSG_LENGTH 100
#define MAX_DIR_NAME 100
#define MAX_FILE_NAME 100
#define PKT_PAYLOAD_MAX 100
#define TENMILLISEC 10000   /* 10 millisecond sleep */
#define MAX_TABLE_SIZE 100 //Forwarding table size

void switch_main(int host_id) {

	/* State */
	char dir[MAX_DIR_NAME];
	int dir_valid = 0;

	char man_msg[MAN_MSG_LENGTH];
	char man_reply_msg[MAN_MSG_LENGTH];
	char man_cmd;

	struct net_port *node_port_list;
	struct net_port **node_port;  // Array of pointers to node ports
	int node_port_num;            // Number of node ports

	int ping_reply_received;

	int i, k, n;
	int dst;
	char name[MAX_FILE_NAME];
	char string[PKT_PAYLOAD_MAX+1];

	FILE *fp;

	struct packet *in_packet; /* Incoming packet */
	struct packet *new_packet;

	struct net_port *p;
	struct host_job *new_job;
	struct host_job *new_job2;

	struct job_queue job_q;

	//Create an array of connections, this will be the forwarding table
	struct connections forwardingTable[MAX_TABLE_SIZE];

	//Initialize the 'valid' variable of each struct
	for (int i = 0; i < MAX_TABLE_SIZE; i++) {
		forwardingTable[i].valid = 0;
	}

	/*
	 * Create an array node_port[ ] to store the network link ports
	 * at the host.  The number of ports is node_port_num
	 */
	node_port_list = net_get_port_list(host_id);

	/*  Count the number of network link ports */
	node_port_num = 0;
	for (p=node_port_list; p!=NULL; p=p->next) {
		node_port_num++;
	}
		/* Create memory space for the array */
	node_port = (struct net_port **)
		malloc(node_port_num*sizeof(struct net_port *));

		/* Load ports into the array */
	p = node_port_list;
	for (k = 0; k < node_port_num; k++) {
		node_port[k] = p;
		p = p->next;
	}

	/* Initialize the job queue */
	job_q_init(&job_q);

	//printf("\th%d: online\n", host_id);

	while(1) {


			/* Get command from manager */
		n = get_man_command(man_port, man_msg, &man_cmd);

		///////////////////////////////////////////////////////////
		// Get packets from incoming links and translate to jobs //
	  //                Put jobs in job queue                  //
		///////////////////////////////////////////////////////////

		for (k = 0; k < node_port_num; k++) { /* Scan all ports */

			in_packet = (struct packet *) malloc(sizeof(struct packet));
			n = packet_recv(node_port[k], in_packet);

			if (n > 0) {
				// Convert packet to job
				new_job = (struct host_job *) malloc(sizeof(struct host_job));
				new_job->in_port_index = k;
				new_job->packet = in_packet;

				// Add to job queue
				job_q_add(&job_q, new_job);
			}
			else {
				free(in_packet);
			}
		} // End of scanning ports


		///////////////////////////////////////////////////////////
		//            Execute one job in the job queue           //
		///////////////////////////////////////////////////////////

		int portMatch = -1;

		if (job_q_num(&job_q) > 0) {

			//printf("\th%d: jobs available\n", host_id);
			/* Get a new job from the job queue */
			new_job = job_q_remove(&job_q);

			//printf("\th%d: got a job\n", host_id);

			/* Check if we already have port recorded in the forwarding table */
			for ( i = 0; i < MAX_TABLE_SIZE && portMatch == -1; i++) {
				//Found match
				if (forwardingTable[i].valid == 1 && forwardingTable[i].host == new_job-.packet->src) {
					portMatch = forwardingTable[i].port;
				}
			}

			if (portMatch != -1) {
				bool addedEntry = false;
				for (int i = 0; i < MAX_TABLE_SIZE && addedEntry == false ; i++) {
					if (forwardingTable[i].valid == 0) {
						forwardingTable[i].valid = 1;
						forwardingTable[i].host = new_job->packet->src;
						forwardingTable[i].port = new_job->in_port_index;
						portMatch = forwardingTable[i].port;
					  addedEntry = true;
					}
				}
			}

			/* There are no matches for this port, so add to forwarding table */

			//// TODO: Edit to variable names to match file
			////    START    ////
			vport = -1;
			for (i=0; i<MAX_FWD_LENGTH; i++) {
				//scan for valid match
				if (forwardingTable[i].valid && forwardingTable[i].host == new_job->packet->dst) {
					vport = forwardingTable[i].port;
				}
			}

			if (vport > -1) {
				packet_send(node_port[vport], new_job->packet);
			} else {
				for (k=0; k<node_port_num; k++) {
					if (k != new_job->in_port_index) {
						packet_send(node_port[k], new_job->packet);
					}
				}
			}

			free(new_job->packet);
			free(new_job);

			////    END    ////

		}

		///////////////////////////////////////////////////////////
		//            The host goes to sleep for 10 ms           //
		///////////////////////////////////////////////////////////
		usleep(TENMILLISEC);

	} /* End of while loop */

}
