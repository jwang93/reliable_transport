
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include "rlib.h"




//need some abstraction to represent the sender 
struct Sender {
  int current_seq_num;
  int send_window_size;
  int last_frame_sent;
  packet_t pack;
}; 


//need some abstraction to represent the receiver 
struct Receiver {
  int receive_window_size;
  int largest_acceptable_frame;
  int last_frame_received; 
};

/* reliable_state type is the main data structure that holds all the crucial information for this lab */
struct reliable_state {  
  rel_t *next;			/* Linked list for traversing all connections */
  rel_t **prev;
  conn_t *c;			/* This is the connection object */

  /* Add your own data fields below this */
  struct Sender sender;
  struct Receiver receiver;

};
rel_t *rel_list; //rel_t is a type of reliable state 



/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss,
	    const struct config_common *cc)
{
  rel_t *r;

  r = xmalloc (sizeof (*r));
  memset (r, 0, sizeof (*r));

  if (!c) { //create a connection if there is no connection 
    c = conn_create (r, ss);
    if (!c) {
      free (r);
      return NULL;
    }
  }

  r->c = c; //set up r's connection 
  r->next = rel_list; 
  r->prev = &rel_list;
  if (rel_list)
    rel_list->prev = &r->next;
  rel_list = r;

  /* Do any other initialization you need here */


  return r;
}

void
rel_destroy (rel_t *r)
{
  if (r->next)
    r->next->prev = r->prev;
  *r->prev = r->next;
  conn_destroy (r->c); //destroy the connection 


  /* Free any other allocated memory here */
}


/* This function only gets called when the process is running as a
 * server and must handle connections from multiple clients.  You have
 * to look up the rel_t structure based on the address in the
 * sockaddr_storage passed in.  If this is a new connection (sequence
 * number 1), you will need to allocate a new conn_t using rel_create
 * ().  (Pass rel_create NULL for the conn_t, so it will know to
 * allocate a new connection.)
 */
void
rel_demux (const struct config_common *cc,
	   const struct sockaddr_storage *ss,
	   packet_t *pkt, size_t len)
{
}

void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{

  int checksum = pkt->cksum;
  int compare_checksum = cksum(pkt->data, n);

  if (compare_checksum != checksum) {
    /* We have an error. Discard the packet. It is corrupted. */
  }

  /* going to have to determine what type of packet we have
     couple options: 
        ack packet only 8 bytes
        data packet 
  */
  pkt_length = ntohs(pkt->len); //pkt->len comes in the type of uint16_t 

  if (pkt_length == 8) {
    rel_read(r);
    /* 
      If the packet is an ack_packet or data_packet, read the packet 
    */
  } else {
    rel_output(r);
    /*
      If the packet is a data_packet, output the packet to the console through conn_output 
    */
  }

}


void
rel_read (rel_t *s)
{
  // Gets input from conn_input, which I believe gets input from STDIN 
  int data_size = conn_input(s->c, s->sender->packet, 500 -1 ); //500 is the max size of packet 
  
  if (data_size == 0) {
    //no currently data available... stall on this? 
  } else if (data_size > 0) {
    //send the packet 
    s->sender.pack.ackno = s->receiver.last_frame_received++;
    s->receiver->last_frame_received--;
    conn_sendpkt (s->c, &s->pack, len); 

  } else {
    //you got an error 
  }

}

void
rel_output (rel_t *r)
{
	//first get size available in connection's buffer space
	//then, get length of message
	//pass in the available buffer space available to conn_output (or less if message is smaller)
	//get result
	size_t availableSpace = conn_bufspace(r->c);

//	int result = conn_output (r->c, const void *_buf, size_t _n)
//	if(result < 0) {
//		//error
//	} else {
//		//success
//	}
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */

}
