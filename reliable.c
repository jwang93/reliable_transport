
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


#define MAX_DATA_SIZE 500
#define ACK_PACKET_HEADER 8
#define DATA_PACKET_HEADER 12


//need some abstraction to represent the sender
struct Sender {
  int send_window_size; //How do we know how large the SWS is?
  int last_frame_sent;
  packet_t packet;
};


//need some abstraction to represent the receiver
struct Receiver {
  int largest_acceptable_seqno;   //going to be needed once we have a RWS/SWS
  int last_frame_received;
  packet_t packet;
};


/*
Ideally, the sender and receiver should not be in the reliable_state struct. They should be their own
entities. However, because all the signatures are programmed to accept only rel_T, we thought that it would
be most convenient to include sender/receiver in the reliable_state struct.
*/

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

void initialize(rel_t *r) {

  r->sender.packet.cksum = 0;
  r->sender.packet.len = 0;
  r->sender.packet.ackno = 0;
  r->sender.packet.seqno = 1;
  r->sender.last_frame_sent = 0;

  r->receiver.packet.cksum = 0;
  r->receiver.packet.len = 0;
  r->receiver.packet.ackno = 0;
  r->receiver.packet.seqno = 0;
  r->receiver.last_frame_received = 0;

}


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

  initialize(r);

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

//  print_pkt (pkt, (char*)'i', pkt->len);
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
  int pkt_length = ntohs(pkt->len); //pkt->len comes in the type of uint16_t


  //Case when pkt is ACK or DATA
  if (pkt_length >= ACK_PACKET_HEADER) {
    rel_read(r);
    /*
      If the packet is an ack_packet or data_packet, read the packet
    */
  } if(pkt_length >= DATA_PACKET_HEADER) {
    rel_output(r);
    /*
      If the packet is a data_packet, output the packet to the console through conn_output
    */
  }

}


void
rel_read (rel_t *s)
{
  /* Gets input from conn_input, which I believe gets input from STDIN */
  int data_size = conn_input(s->c, s->sender.packet.data, MAX_DATA_SIZE);

  if (data_size == 0) {
    return;
  } else if (data_size > 0) {
    s->sender.packet.len = data_size;
    s->sender.last_frame_sent++;
    s->sender.packet.seqno = s->sender.last_frame_sent;
    s->sender.packet.ackno = -1; //NOT SURE?
    conn_sendpkt (s->c, &s->sender.packet, s->sender.packet.len);
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

  /* TWO Checls
    1. enough available space
    2. making sure the receiver data is not empty
  */
  if (availableSpace >= r->receiver.packet.len && r->receiver.packet.len > 0) {
    int bytes_written = conn_output(r->c, r->receiver.packet.data, r->receiver.packet.len);
    r->receiver.last_frame_received++; //by outputting the data, you have proved you have received the packet
    r->receiver.packet.len = 0;

  }

  /* send ack packet back to receiver */
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */

}
