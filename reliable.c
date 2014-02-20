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

struct Sender {
	int send_window_size;
	int last_frame_sent;
	packet_t packet;
};

struct Receiver {
	int largest_acceptable_seqno;   //going to be needed once we have a RWS/SWS
	int last_frame_received;
	packet_t packet;
};

struct WindowBuffer {
	packet_t* ptr;
	int isFull;		//0 is for empty, 1 is for full
	int timeStamp;
};

/* reliable_state type is the main data structure that holds all the crucial information for this lab */
struct reliable_state {
	rel_t *next; /* Linked list for traversing all connections */
	rel_t **prev;
	conn_t *c; /* This is the connection object */

	/* Add your own data fields below this */
	struct Sender sender;
	struct Receiver receiver;
	struct WindowBuffer windowBuffer[20];

};
rel_t *rel_list; //rel_t is a type of reliable state

void initialize(rel_t *r, int windowSize) {

	r->sender.packet.cksum = 0;
	r->sender.packet.len = 0;
	r->sender.packet.ackno = 1;
	r->sender.packet.seqno = 0;
	r->sender.last_frame_sent = -1;
	r->sender.packet.data[500] = '\0'; //trying to initialize sender packet data
	r->sender.send_window_size = windowSize;
	r->receiver.packet.cksum = 0;
	r->receiver.packet.len = 0;
	r->receiver.packet.ackno = 1;
	r->receiver.packet.seqno = 0;		//should this seqno be = 1?
	r->receiver.last_frame_received = 0;
	r->receiver.packet.data[500] = '\0';//trying to initialize receiver packet data

	memset(&r->windowBuffer, 0, sizeof(&r->windowBuffer));

}

/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t *
rel_create(conn_t *c, const struct sockaddr_storage *ss,
		const struct config_common *cc) {
	rel_t *r;

	r = xmalloc(sizeof(*r));
	memset(r, 0, sizeof(*r));

	if (!c) { //create a connection if there is no connection
		c = conn_create(r, ss);
		if (!c) {
			free(r);
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

	initialize(r, cc->window);

	return r;
}

void rel_destroy(rel_t *r) {
	if (r->next)
		r->next->prev = r->prev;
	*r->prev = r->next;
	conn_destroy(r->c); //destroy the connection

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
void rel_demux(const struct config_common *cc,
		const struct sockaddr_storage *ss, packet_t *pkt, size_t len) {
}

void debugger(char* function_name, packet_t *pkt) {

	fprintf(stderr,
			"DEBUGGER:: Function Name: %s, Data: %s\n cksum:%x, len:%d, ackno:%d, seqno:%d \n\n",
			function_name, pkt->data, pkt->cksum, pkt->len, pkt->ackno,
			pkt->seqno);
}

//if type = 0, print sender buffer, else print receiver buffer
void printBuffers(rel_t *r) {
	int i;
	int length = 2 * r->sender.send_window_size;

	fprintf(stderr, "\n");
	for (i = 0; i < length/2; i++) {
		fprintf(stderr, "[%i] = %i, ", i, r->windowBuffer[i].isFull);
	}
	fprintf(stderr, " SENDER BUFFER. \n");

	for (i = length/2; i < length; i++) {
		int pos = i%r->sender.send_window_size;
		fprintf(stderr, "[%i] = %i, ", pos, r->windowBuffer[i].isFull);
	}
	fprintf(stderr, " RECEIVER BUFFER. \n");
	fprintf(stderr, "\n");

}

void preparePacketForSending(packet_t *pkt) {
	int packetLength = pkt->len;
	pkt->ackno = htons(pkt->ackno);
	pkt->len = htons(pkt->len);
	if (packetLength >= DATA_PACKET_HEADER) {
		pkt->seqno = htons(pkt->seqno);
	}
	pkt->cksum = cksum(pkt, packetLength);
}
void convertPacketToNetworkByteOrder(packet_t *pkt) {
	pkt->len = ntohs(pkt->len);
	pkt->ackno = ntohs(pkt->ackno);
	pkt->seqno = ntohs(pkt->seqno);
}



void rel_recvpkt(rel_t *r, packet_t *pkt, size_t n) {

	debugger("rel_recvpkt", pkt);
	int checksum = pkt->cksum;
	int compare_checksum = cksum(pkt->data, n);

	if (compare_checksum != checksum) {
		/* We have an error. Discard the packet. It is corrupted. */
	}
	convertPacketToNetworkByteOrder(pkt);
	fprintf(stderr, "in receiving, seqno is %i, length is %i \n", pkt->seqno,
			pkt->len);

	r->receiver.packet = *pkt; //had to set the receiver packet to pkt somehow but seems hacky.
							   //without setting it here, rel_output doesn't have the received packet

	int positionInArray = (pkt->seqno % r->sender.send_window_size)
			+ r->sender.send_window_size;

	//if it is full AND it is not an ackpacket
	if (r->windowBuffer[positionInArray].isFull == 1 && pkt->len != ACK_PACKET_HEADER) {
		//drop packet
		fprintf(stderr, "packet is dropped!!! \n");
		fprintf(stderr, "position in array when dropped is %i \n",
				positionInArray);
		return;
	}

	if(pkt->len != ACK_PACKET_HEADER) {
		packet_t *receivingPacketCopy = malloc(sizeof pkt);
		memcpy(receivingPacketCopy, pkt, sizeof pkt);
		struct WindowBuffer *packetBuffer = malloc(sizeof(struct WindowBuffer));
		packetBuffer->isFull = 1;
		packetBuffer->ptr = pkt;
		packetBuffer->timeStamp = 0;	//will need to change later
		r->windowBuffer[positionInArray] = *packetBuffer;
	}

	printBuffers(r);

	//Case when pkt is ACK or DATA
	if (pkt->len == ACK_PACKET_HEADER) {
		//REMEMBER TO FREE THE PACKET MEMORY AND MARK THAT THE LOCATION IN THE ARRAY IS FREE
		fprintf(stderr, "THERE WAS AN ACK PACKET RECEIVED!!!!!!!! \n");
		int pos = (pkt->ackno - 1) % r->sender.send_window_size;
		//free(r->windowBuffer[pos].ptr);
		r->windowBuffer[pos].isFull = 0;
	}
	if (pkt->len >= DATA_PACKET_HEADER) {
		r->receiver.last_frame_received = pkt->seqno;
		rel_output(r);

		packet_t *ackPacket = malloc(sizeof pkt->len);	//probably should be sizeof packet_t, but C won't let me do this
		ackPacket->len = ACK_PACKET_HEADER;
		ackPacket->ackno = pkt->ackno;
		preparePacketForSending(ackPacket);
		conn_sendpkt(r->c, ackPacket, ackPacket->len);
	}

}

void rel_read(rel_t *s) {
	/* Gets input from conn_input, which I believe gets input from STDIN */
	int positionInArray = (s->sender.last_frame_sent + 1) % s->sender.send_window_size;
	fprintf(stderr, "window size is: %i \n", s->sender.send_window_size);
	int data_size = 0;
	fprintf(stderr, "windowBuffer isFull value %i \n", s->windowBuffer[positionInArray].isFull);

	if (s->windowBuffer[positionInArray].isFull == 0) {
		//only get the data if there is room in the buffer
		data_size = conn_input(s->c, s->sender.packet.data, MAX_DATA_SIZE);
	} else {
		return;
	}

	//need to check sender window and see if full 
	//make an array of packets 3 or 4 times the size of the window 

	if (data_size == 0) {
		return;
	}

	else if (data_size > 0) {

		s->sender.last_frame_sent++;

		s->sender.packet.len = data_size + DATA_PACKET_HEADER;
		s->sender.packet.seqno = s->sender.last_frame_sent;
		s->sender.packet.ackno = s->sender.packet.seqno + 1; //ackno should always be 1 higher than seqno
		s->sender.packet.cksum = cksum(&s->sender.packet, s->sender.packet.len);

		debugger("rel_read for sender", &(s->sender.packet));

		preparePacketForSending(&(s->sender.packet));

		packet_t *sendingPacketCopy = malloc(sizeof s->sender.packet);
		memcpy(sendingPacketCopy, &s->sender.packet, sizeof s->sender.packet);

		struct WindowBuffer *packetBuffer = malloc(sizeof(struct WindowBuffer));
		packetBuffer->isFull = 1;
		packetBuffer->ptr = sendingPacketCopy;
		packetBuffer->timeStamp = 0;	//will need to change later
		s->windowBuffer[positionInArray] = *packetBuffer;
		conn_sendpkt(s->c, &s->sender.packet, s->sender.packet.len);
		memset(&s->sender.packet, 0, sizeof(&s->sender.packet));
	}

	else {
		//you got an error
	}
	printBuffers(s);

}
int firstSeqNoToPrint(int seqno, rel_t *r) {
	int positionInArray = (seqno % r->sender.send_window_size) + r->sender.send_window_size;
	return -1;	//FILL THIS FUNCTION IN LATER
}

void rel_output(rel_t *r) {
	//first get size available in connection's buffer space
	//then, get length of message
	//pass in the available buffer space available to conn_output (or less if message is smaller)
	//get result
	int availableSpace = conn_bufspace(r->c);
	int seqno = firstSeqNoToPrint(r->receiver.last_frame_received, r);
	if (availableSpace >= r->receiver.packet.len && r->receiver.packet.len > 0) {

		conn_output(r->c, r->receiver.packet.data, r->receiver.packet.len);
		r->receiver.packet.len = 0;
		memset(&r->receiver.packet, 0, sizeof(&r->receiver.packet));
		int positionInArray = (r->receiver.packet.seqno % r->sender.send_window_size) + r->sender.send_window_size;
		//free(r->windowBuffer[positionInArray].ptr);
		//r->windowBuffer[positionInArray].isFull = 0;
	}

	/* send ack packet back to receiver */
}

void rel_timer() {
	/* Retransmit any packets that need to be retransmitted */

}
