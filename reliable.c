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
	int last_frame_sent;
	int expected_ack;
	packet_t packet;
};

struct Receiver {
	int largest_acceptable_seqno;   //going to be needed once we have a RWS/SWS
	int last_frame_received;
	int ackno;
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
	int windowSize;
	struct WindowBuffer senderWindowBuffer[1000];
	struct WindowBuffer receiverWindowBuffer[1000];


};
rel_t *rel_list; //rel_t is a type of reliable state
int allow = 0;
int timestamp = 0;

void initialize(rel_t *r, int windowSize) {

	r->sender.packet.cksum = 0;
	r->sender.packet.len = 0;
	r->sender.packet.ackno = 1;
	r->sender.packet.seqno = 0;
	r->sender.last_frame_sent = -1;   //makes sense because you want to start at 0
	r->sender.packet.data[500] = '\0'; //trying to initialize sender packet data
	r->sender.expected_ack = 0;
	r->receiver.packet.cksum = 0;
	r->receiver.packet.len = 0;
	r->receiver.packet.ackno = 1;
	r->receiver.packet.seqno = 0;		//should this seqno be = 1?
	r->receiver.last_frame_received = 0;
	r->receiver.packet.data[500] = '\0';//trying to initialize receiver packet data
	r->receiver.ackno = 0;
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

void debugger(char* function_name, packet_t *pkt) {

	fprintf(stderr,
			"DEBUGGER:: Function Name: %s, Data: %s\n cksum:%x, len:%d, ackno:%d, seqno:%d \n\n",
			function_name, pkt->data, pkt->cksum, pkt->len, pkt->ackno,
			pkt->seqno);
}

void printBuffers(rel_t *r) {
	int i;

	fprintf(stderr, "\n");
	for (i = 0; i < r->windowSize; i++) {
		fprintf(stderr, "[%i] = %i, ", i, r->senderWindowBuffer[i].isFull);
	}
	fprintf(stderr, " SENDER BUFFER. \n");

	for (i = 0; i < r->windowSize; i++) {
		fprintf(stderr, "[%i] = %i, ", i, r->receiverWindowBuffer[i].isFull);
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
	pkt->cksum = htons(cksum(pkt->data, packetLength));
}
void convertPacketToNetworkByteOrder(packet_t *pkt) {
	pkt->len = ntohs(pkt->len);
	pkt->ackno = ntohs(pkt->ackno);
	pkt->seqno = ntohs(pkt->seqno);
	pkt->cksum = ntohs(pkt->cksum);
}

// This method retransmits the packet w/ given seqno
void retransmit(rel_t *s, int seqno) {
	struct WindowBuffer *packet = &s->senderWindowBuffer[seqno];
//	printBuffers(s);
//	convertPacketToNetworkByteOrder(packet->ptr); //confirmed that this packet is correct

	conn_sendpkt(s->c, packet->ptr, packet->ptr->len);
	memset(packet->ptr, 0, sizeof(packet->ptr));

//	fprintf(stderr, "You want to retransmit packet w/ seqno: %i and data: %s", packet->ptr->seqno, packet->ptr->data);
}

int compute_LFR(rel_t* r) {
	/*PREMISE: you just received this seqno, what should the last_frame_received be?
	 *
	 */
	int i;
	for (i = 0; i < r->windowSize; i++) { //might be too small...
//		printBuffers(r);
		if (r->receiverWindowBuffer[i].isFull == 0) {
//			fprintf(stderr, "Return values from computeLFR: %i*****\n", i);
			return i;
		}
	}
	return -1; //should cause a seg fault
}

void rel_recvpkt(rel_t *r, packet_t *pkt, size_t n) {

	convertPacketToNetworkByteOrder(pkt);
//	debugger("rel_recvpkt", pkt);

	if (pkt->seqno == 1 && allow == 0) {
		allow = 1;
		return;
	}

	int checksum = pkt->cksum;
	int compare_checksum = cksum(pkt->data, pkt->len);

	if (compare_checksum != checksum) {
		fprintf(stderr, "Checksums do not match. Packet corruption. Kill Connection. \n");
		return;
	}
//	fprintf(stderr, "in receiving, seqno is %i, length is %i \n", pkt->seqno, pkt->len);



	r->receiver.packet = *pkt; //had to set the receiver packet to pkt somehow but seems hacky.
							   //without setting it here, rel_output doesn't have the received packet




//	if (r->windowBuffer[positionInArray].isFull == 1 && pkt->len != ACK_PACKET_HEADER) {
//		fprintf(stderr, "Dropped the packet that should have been at position: %i \n", positionInArray);
//		return;
//	}

//	printBuffers(r);

	if (pkt->len == ACK_PACKET_HEADER) {
		int pos = (pkt->ackno - 1) % r->windowSize;
//		int num = rand() % 2;
		int num = 0;
		fprintf(stderr, "Value of ack packet: %i\n", pkt->ackno);

//		if (pkt->ackno != r->sender.expected_ack) {
//			fprintf(stderr, "Shit hits the fan. You expected: %i\n", r->sender.expected_ack);
//			//retransmit the packet w/ seqno pkt->ackno
//			fprintf(stderr, "Retransmitting on: %i\n", pkt->ackno);
//			retransmit(r, pkt->ackno);
//		}

		if (num == 0) {
			//got the ACK
//			free(r->windowBuffer[pos].ptr);
//			r->windowBuffer[pos].isFull = 0;
//			fprintf(stderr, "ACK PACKET RECEIVED!\n");
		} else {
			//network dropped the ACK
			fprintf(stderr, "ACK PACKET WAS DROPPED \n");
		}
	}

	if (r->receiver.last_frame_received == pkt->seqno) {
		r->receiver.last_frame_received = compute_LFR(r) + 1;
//		r->receiver.last_frame_received += 1;
	}

	if(pkt->len != ACK_PACKET_HEADER) {

//		printBuffers(r);
	}


	if (pkt->len >= DATA_PACKET_HEADER) {

		packet_t *receivingPacketCopy = malloc(sizeof (struct packet));
		memcpy(receivingPacketCopy, pkt, sizeof (struct packet));
		struct WindowBuffer *packetBuffer = malloc(sizeof(struct WindowBuffer));
		packetBuffer->isFull = 1;
		packetBuffer->ptr = pkt;
		packetBuffer->timeStamp = 0;	//will need to change later
		r->receiverWindowBuffer[pkt->seqno] = *packetBuffer;

		//you should only output when in order

		if (pkt->seqno == r->receiver.ackno) {
			rel_output(r);
		}

		packet_t *ackPacket = malloc(sizeof (struct packet));
		ackPacket->len = ACK_PACKET_HEADER;
//		fprintf(stderr, "\n Putting into the ack packet an ackno of: %i\n", r->receiver.last_frame_received);
		ackPacket->ackno = r->receiver.last_frame_received;
		preparePacketForSending(ackPacket);
		conn_sendpkt(r->c, ackPacket, ackPacket->len);
	}

}

void rel_read(rel_t *s) {
	/* Gets input from conn_input, which I believe gets input from STDIN */
	int positionInArray = (s->sender.last_frame_sent + 1) % s->windowSize;
//	fprintf(stderr, "window size is: %i \n", s->sender.send_window_size);
	int data_size = 0;
//	fprintf(stderr, "windowBuffer isFull value %i \n", s->windowBuffer[positionInArray].isFull);


	data_size = conn_input(s->c, s->sender.packet.data, MAX_DATA_SIZE);

	if (s->senderWindowBuffer[positionInArray].isFull == 0) {
	} else {
//		fprintf(stderr, "\n Packet was not read because there is no space in the sender's window.\n");
//		return;
	}

	if (data_size == 0) {
		return;
	}

	else if (data_size > 0) {

		s->sender.last_frame_sent++;

		s->sender.packet.len = data_size + DATA_PACKET_HEADER;
		s->sender.packet.seqno = s->sender.last_frame_sent;
		s->sender.expected_ack = s->sender.packet.seqno + 1;
		s->sender.packet.ackno = s->sender.packet.seqno + 1; //ackno should always be 1 higher than seqno
		s->sender.packet.cksum = cksum(&s->sender.packet, s->sender.packet.len);

//		debugger("rel_read for sender", &(s->sender.packet));

		preparePacketForSending(&(s->sender.packet));

		packet_t *sendingPacketCopy = malloc(sizeof s->sender.packet);
		memcpy(sendingPacketCopy, &s->sender.packet, sizeof s->sender.packet);

		struct WindowBuffer *packetBuffer = malloc(sizeof(struct WindowBuffer));
		packetBuffer->isFull = 1;
		packetBuffer->ptr = sendingPacketCopy;
		packetBuffer->timeStamp = timestamp;	//will need to change later
		s->senderWindowBuffer[positionInArray] = *packetBuffer;
//		fprintf(stderr, "conn_sendpkt sent with data: %s", s->sender.packet.data);
		conn_sendpkt(s->c, &s->sender.packet, s->sender.packet.len);
		memset(&s->sender.packet, 0, sizeof(&s->sender.packet));
	}

	else {
		//you got an error
	}

//	printBuffers(s);

}
int firstSeqNoToPrint(int seqno, rel_t *r) {
	int positionInArray = (seqno % r->windowSize) + r->windowSize;
	return -1;	//FILL THIS FUNCTION IN LATER
}

void rel_output(rel_t *r) {

	int availableSpace = conn_bufspace(r->c);
	int seqno = firstSeqNoToPrint(r->receiver.last_frame_received, r);
	if (availableSpace >= r->receiver.packet.len && r->receiver.packet.len > 0) {

		conn_output(r->c, r->receiver.packet.data, r->receiver.packet.len);
		r->receiver.packet.len = 0;
		memset(&r->receiver.packet, 0, sizeof(&r->receiver.packet));
		int positionInArray = (r->receiver.packet.seqno % r->windowSize) + r->windowSize;
		//free(r->windowBuffer[positionInArray].ptr);
//		r->windowBuffer[positionInArray].isFull = 0;
	}

	/* send ack packet back to receiver */
}

void rel_timer() {
	/* Retransmit any packets that need to be retransmitted */
	rel_t *r = rel_list;
	timestamp++;



//	if (pkt->ackno != r->sender.expected_ack) {
//		fprintf(stderr, "Shit hits the fan. You expected: %i\n", r->sender.expected_ack);
//		//retransmit the packet w/ seqno pkt->ackno
//		fprintf(stderr, "Retransmitting on: %i\n", pkt->ackno);
//		retransmit(r, pkt->ackno);
//	}
}
