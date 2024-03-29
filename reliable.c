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

#define MAX_DATA_SIZE 499
#define ACK_PACKET_HEADER 8
#define DATA_PACKET_HEADER 12

struct Sender {
	int last_frame_sent;
	int buffer_position;
	packet_t packet;
};

struct Receiver {
	int last_frame_received;
	int ackno;
	int buffer_position;
	int max_ack;
	packet_t packet;
};

struct WindowBuffer {
	packet_t* ptr;
	int isFull;		//0 is for empty, 1 is for full
	int timeStamp;
	int acknowledged; //0 for no, 1 for yes
	int outputted; //0 for no, 1 for yes
};

/* reliable_state type is the main data structure that holds all the crucial information for this lab */
struct reliable_state {
	rel_t *next; /* Linked list for traversing all connections */
	rel_t **prev;
	conn_t *c; /* This is the connection object */

	/* Add your own data fields below this */
	struct Sender sender;
	struct Receiver receiver;
	int windowSize;
	struct WindowBuffer senderWindowBuffer[1000];
	struct WindowBuffer receiverWindowBuffer[1000];
};
rel_t *rel_list; //rel_t is a type of reliable state
int timestamp = 0;

void initialize(rel_t *r, int windowSize) {

	r->sender.packet.cksum = 0;
	r->sender.packet.len = 0;
	r->sender.packet.ackno = 1;
	r->sender.packet.seqno = 0;
	r->sender.last_frame_sent = -1;   //makes sense because you want to start at 0
	r->sender.buffer_position = 0;
	r->receiver.packet.cksum = 0;
	r->receiver.packet.len = 0;
	r->receiver.packet.ackno = 1;
	r->receiver.packet.seqno = 0;		//should this seqno be = 1?
	r->receiver.last_frame_received = 0;
	r->receiver.ackno = 0;
	r->receiver.max_ack = 0;
	r->receiver.buffer_position = 0;
	r->windowSize = windowSize;
	memset(&r->senderWindowBuffer, 0, sizeof(&r->senderWindowBuffer));
	memset(&r->receiverWindowBuffer, 0, sizeof(&r->receiverWindowBuffer));
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

void preparePacketForSending(packet_t *pkt) {
	int packetLength = pkt->len;
	pkt->ackno = htons(pkt->ackno);
	pkt->len = htons(pkt->len);
	if (packetLength >= DATA_PACKET_HEADER) {
		pkt->seqno = htons(pkt->seqno);
	}
}

void convertPacketFromNetworkByteOrder(packet_t *pkt) {
	pkt->len = ntohs(pkt->len);
	pkt->ackno = ntohs(pkt->ackno);
	pkt->seqno = ntohs(pkt->seqno);
}

/*
 * Method to resend ack packets when they were dropped.
 */
void retransmit_ack(rel_t *r, int ackVal) {
	packet_t *ackPacket = malloc(sizeof (struct packet));
	ackPacket->len = ACK_PACKET_HEADER;
	ackPacket->ackno = ackVal;
	int ackLength = ackPacket->len;
	preparePacketForSending(ackPacket);
	ackPacket->cksum = 0;
	ackPacket->cksum = cksum(ackPacket, ackLength);
	conn_sendpkt(r->c, ackPacket, ackPacket->len);
}

/*
 * Method to resend data packets when they were dropped.
 * This method is called in rel_timer().
 */
void retransmit_data(rel_t *s, int seqno) {
	struct WindowBuffer *packet = &s->senderWindowBuffer[seqno];
	conn_sendpkt(s->c, packet->ptr, packet->ptr->len);
}

/*
 * Method used to compute the correct cumulative ack number.
 * Gets tricky when frames come out of order.
 */
int compute_LFR(rel_t *r) {
	//Iterate through a section of the buffer, looking for the first place a packet is missing
	int i;
	int shift = r->receiver.buffer_position;
	for (i = shift; i <= r->windowSize + shift; i++) {
		if (r->receiverWindowBuffer[i].isFull == 0) {
			return i;
		}
	}
	return -1;
}

void send_data_pkt(rel_t *s, int data_size) {

	//update sender state when a new data packet is sent
	s->sender.last_frame_sent++;
	s->sender.packet.len = data_size + DATA_PACKET_HEADER;
	s->sender.packet.seqno = s->sender.last_frame_sent;
	s->sender.packet.ackno = s->sender.packet.seqno + 1; //ackno should always be 1 higher than seqno

	//fprintf(stderr, "seqno: %i, buffer position: %i\n", s->sender.packet.seqno, s->sender.buffer_position);
	//alert the user when user has exceeded sender's window size
	if (s->sender.packet.seqno > s->windowSize + s->sender.buffer_position || s->sender.packet.seqno == s->windowSize + s->sender.buffer_position) {
//		fprintf(stderr, "**** You have exceeded the sender's window size. Packet will not be sent. **** \n");
		s->sender.last_frame_sent--;
		return;
	}

	int positionInArray = s->sender.packet.seqno;
	int length = s->sender.packet.len;
	preparePacketForSending(&(s->sender.packet));
	s->sender.packet.cksum = 0;
	s->sender.packet.cksum = cksum(&s->sender.packet, length);

	//prepare a copy of the packet along with other state to store in sender buffer
	packet_t *sendingPacketCopy = malloc(sizeof s->sender.packet);
	memcpy(sendingPacketCopy, &s->sender.packet, sizeof s->sender.packet);
	struct WindowBuffer *packetBuffer = malloc(sizeof(struct WindowBuffer));
	packetBuffer->isFull = 1;
	packetBuffer->ptr = sendingPacketCopy;
	packetBuffer->timeStamp = timestamp;

	//place the WindowBuffer in the buffer array
	s->senderWindowBuffer[positionInArray] = *packetBuffer;

	//send the packet over network
	conn_sendpkt(s->c, &s->sender.packet, s->sender.packet.len);
	memset(&s->sender.packet, 0, sizeof(&s->sender.packet));

}

void rel_recvpkt(rel_t *r, packet_t *pkt, size_t n) {

	// Compare checksums to detect packet corruption
	int checksum = pkt->cksum;
	pkt->cksum = 0;
	int compare_checksum = cksum(pkt, ntohs(pkt->len));
	convertPacketFromNetworkByteOrder(pkt);
	if (compare_checksum != checksum) {
		return;
	}

	r->receiver.packet = *pkt;

	// CASE 1: ACK packet
	if (pkt->len == ACK_PACKET_HEADER) {
		int index = pkt->ackno;
		r->senderWindowBuffer[index-1].acknowledged = 1;

		//make sure to acknowledge everything below the most recent ackno
		int i;
		for (i = 0; i < index; i++) {
			r->senderWindowBuffer[i].acknowledged = 1;
		}

		//move the sender's buffer position pointer
		if (pkt->ackno > r->sender.buffer_position) {
			r->sender.buffer_position = pkt->ackno;
		}
	}

	// CASE 2: DATA packet
	if (pkt->len >= DATA_PACKET_HEADER) {

		// You are getting duplicate packets by nature of cumulative ack
		if (r->receiverWindowBuffer[pkt->seqno].isFull == 1) {
			/* If the seqno of packet is less than the max ack the receiver has sent
			 * that means an ack to the sender was dropped. Retransmit.
			 */
			if (pkt->seqno < r->receiver.max_ack) {
				retransmit_ack(r, r->receiver.max_ack);
			}
			return;
		}

		// Alerting the user when the receiver's window buffer is full.
		if (pkt->seqno >= r->windowSize + r->receiver.buffer_position) {
			return;
		}

		// Clear out the old data from the packet buffer
		int j;
		int start = pkt->len - DATA_PACKET_HEADER;
		for (j = start; j < MAX_DATA_SIZE; j++) {
			pkt->data[j] = '\0';
		}

		// Prepare a copy of the packet for the receiver's buffer
		packet_t *receivingPacketCopy = malloc(sizeof (struct packet));
		memcpy(receivingPacketCopy, pkt, sizeof (struct packet));
		struct WindowBuffer *packetBuffer = malloc(sizeof(struct WindowBuffer));
		packetBuffer->isFull = 1;
		packetBuffer->ptr = receivingPacketCopy;
		packetBuffer->timeStamp = timestamp;

		r->receiverWindowBuffer[pkt->seqno] = *packetBuffer;

		/* when you receive the correct seqno you have been expecting,
		 * recompute what the new ack should be */
		if (r->receiver.last_frame_received == pkt->seqno) {
			r->receiver.last_frame_received = compute_LFR(r);
		}

		rel_output(r);

		if (r->receiver.last_frame_received > r->receiver.max_ack) {
			r->receiver.max_ack = r->receiver.last_frame_received;
		}

		// method for transmit or retransmit ack is the same..
		retransmit_ack(r, r->receiver.last_frame_received);
	}
}

void rel_read(rel_t *s) {
	int data_size = 0;
	data_size = conn_input(s->c, s->sender.packet.data, MAX_DATA_SIZE);

	if (data_size == 0) {
		return;
	}

	else if (data_size > 0 && data_size <= MAX_DATA_SIZE) {
		send_data_pkt(s, data_size);
	}

	else if (data_size > MAX_DATA_SIZE) {
		send_data_pkt(s, MAX_DATA_SIZE);
	}
}

void rel_output(rel_t *r) {

	int i;
	int shift = r->receiver.buffer_position;
	for (i = shift; i < r->windowSize + shift; i++) {
		if (r->receiverWindowBuffer[i].isFull == 0) {
			return;
		}

		if (r->receiverWindowBuffer[i].isFull == 1) {
			if (r->receiverWindowBuffer[i].outputted == 0) {
				int availableSpace = conn_bufspace(r->c);
				struct WindowBuffer *packet = malloc(sizeof(struct WindowBuffer));
				packet = &r->receiverWindowBuffer[i];
				if (availableSpace >= packet->ptr->len) {
					conn_output(r->c, packet->ptr->data, packet->ptr->len);
					r->receiver.buffer_position++;
					r->receiverWindowBuffer[i].outputted = 1;
				}
			}
		}
	}
}

void rel_timer() {
	/* Retransmit any packets that need to be retransmitted */
	rel_t *r = rel_list;
	timestamp++;

	int i;
	int shift = r->sender.buffer_position;
	for (i = shift; i < r->windowSize + shift; i++) {
		if (r->senderWindowBuffer[i].isFull == 1) {
			if (r->senderWindowBuffer[i].acknowledged == 0) {
				if ((timestamp - r->senderWindowBuffer[i].timeStamp) > 5) {
					retransmit_data(r, i);
				}
			}
		}
	}
}
