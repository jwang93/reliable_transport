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
	int buffer_position;
	packet_t packet;
};

struct Receiver {
	int largest_acceptable_seqno;   //going to be needed once we have a RWS/SWS
	int last_frame_received;
	int ackno;
	int buffer_position;
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
int allow = 0;
int allow2 = 0;
int timestamp = 0;

void initialize(rel_t *r, int windowSize) {

	r->sender.packet.cksum = 0;
	r->sender.packet.len = 0;
	r->sender.packet.ackno = 1;
	r->sender.packet.seqno = 0;
	r->sender.last_frame_sent = -1;   //makes sense because you want to start at 0
//	r->sender.packet.data[500] = '\0'; //trying to initialize sender packet data
	r->sender.expected_ack = 0;
	r->sender.buffer_position = 0;
	r->receiver.packet.cksum = 0;
	r->receiver.packet.len = 0;
	r->receiver.packet.ackno = 1;
	r->receiver.packet.seqno = 0;		//should this seqno be = 1?
	r->receiver.last_frame_received = 0;
//	r->receiver.packet.data[500] = '\0';//trying to initialize receiver packet data
	r->receiver.ackno = 0;
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

void printBuffers(rel_t *r) {
	int i;

	fprintf(stderr, "\n");
	for (i = 0; i < r->windowSize; i++) {
		fprintf(stderr, "[%i] = %i (F), %i (A), ", i, r->senderWindowBuffer[i].isFull, r->senderWindowBuffer[i].acknowledged);
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

void retransmit(rel_t *s, int seqno) {
	struct WindowBuffer *packet = &s->senderWindowBuffer[seqno];
	conn_sendpkt(s->c, packet->ptr, packet->ptr->len);
}

int compute_LFR(rel_t* r) {
	int i;
	int shift = r->receiver.buffer_position;
	for (i = shift; i <= r->windowSize + shift; i++) { //might be too small...
		if (r->receiverWindowBuffer[i].isFull == 0) {
			return i;
		}
	}
	return -1;
}

void rel_recvpkt(rel_t *r, packet_t *pkt, size_t n) {

	convertPacketToNetworkByteOrder(pkt);

//	if (pkt->seqno == 2 && allow == 0) {
//		allow = 1;
//		return;
//	}
//
//
//	if (pkt->seqno == 5 && allow2 == 0) {
//		allow2 = 1;
//		return;
//	}


//	/*
//	 * Specific Test Case
//	 * 1. Tests the receiver window buffer
//	 * 2. Rejects packets when it hasn't received the appropriate
//	 * lower sequence packet and is out of buffer space
//	 * 3. You can see this only when sender window is larger than
//	 * receiver window.
//	 */
//	if (pkt->seqno == 2) {
//		return;
//	}

//	/*
//	 * General Test Case
//	 * 1. Drops 75% of packets
//	 * 2. Tests retransmission of packets
//	 * 3. Tests correct ordering of packets
//	 * 4. Tests sliding of sender window when using a small window size
//	 */
//	if (pkt->len >= DATA_PACKET_HEADER) {
//		int num = rand() % 4;
//		if (num < 3) {
//			fprintf(stderr, "Packet[%i] dropped\n", pkt->seqno);
//			return;
//		}
//	}

//	fprintf(stderr, "Data: %s, Checksum: %i", pkt->data, pkt->cksum);

	int checksum = pkt->cksum;
	int compare_checksum = cksum(pkt->data, pkt->len);

	if (compare_checksum != checksum) {
		fprintf(stderr, "Checksums do not match. Packet corruption. Kill Connection. \n");
		return;
	}
//	fprintf(stderr, "in receiving, seqno is %i, length is %i \n", pkt->seqno, pkt->len);

	r->receiver.packet = *pkt;

//	if (r->windowBuffer[positionInArray].isFull == 1 && pkt->len != ACK_PACKET_HEADER) {
//		fprintf(stderr, "Dropped the packet that should have been at position: %i \n", positionInArray);
//		return;
//	}

	if (pkt->len == ACK_PACKET_HEADER) {
//		fprintf(stderr, "Value of ack packet: %i\n", pkt->ackno);
		int index = pkt->ackno;
		r->senderWindowBuffer[index-1].acknowledged = 1; //everything lower than ackno should be acknowledged

		if (pkt->ackno > r->sender.buffer_position) {
			r->sender.buffer_position = pkt->ackno;
		}

		int i;
		for (i = 0; i < index; i++) {
			r->senderWindowBuffer[i].acknowledged = 1;
		}
	}

	if (pkt->len >= DATA_PACKET_HEADER) {

		if (r->receiverWindowBuffer[pkt->seqno].isFull == 1) {
//			fprintf(stderr, "Throwing away pkt[%i] because it's dup\n", pkt->seqno);
			return;
		}

		if (pkt->seqno >= r->windowSize + r->receiver.buffer_position) {
//			fprintf(stderr, "**** You have exceeded the receiver's window size. Packet will be dropped and not buffered. **** \n");
			return;
		}

		packet_t *receivingPacketCopy = malloc(sizeof (struct packet));
		memcpy(receivingPacketCopy, pkt, sizeof (struct packet));
		struct WindowBuffer *packetBuffer = malloc(sizeof(struct WindowBuffer));
		packetBuffer->isFull = 1;
		packetBuffer->ptr = receivingPacketCopy;
		packetBuffer->timeStamp = timestamp;	//will need to change later
		r->receiverWindowBuffer[pkt->seqno] = *packetBuffer;

		if (r->receiver.last_frame_received == pkt->seqno) {
			r->receiver.last_frame_received = compute_LFR(r);
		}

		rel_output(r);
		packet_t *ackPacket = malloc(sizeof (struct packet));
		ackPacket->len = ACK_PACKET_HEADER;
		ackPacket->ackno = r->receiver.last_frame_received;
		preparePacketForSending(ackPacket);
		conn_sendpkt(r->c, ackPacket, ackPacket->len);
	}

}

void rel_read(rel_t *s) {
	/* Gets input from conn_input, which I believe gets input from STDIN */
	int data_size = 0;
	data_size = conn_input(s->c, s->sender.packet.data, MAX_DATA_SIZE);

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

		if (s->sender.packet.seqno > s->windowSize + s->sender.buffer_position || s->sender.packet.seqno == s->windowSize + s->sender.buffer_position) {
			fprintf(stderr, "**** You have exceeded the sender's window size. Packet will not be sent. **** \n");
			s->sender.last_frame_sent--;
			return;
		}

		int positionInArray = s->sender.packet.seqno;
		preparePacketForSending(&(s->sender.packet));

		packet_t *sendingPacketCopy = malloc(sizeof s->sender.packet);
		memcpy(sendingPacketCopy, &s->sender.packet, sizeof s->sender.packet);

		struct WindowBuffer *packetBuffer = malloc(sizeof(struct WindowBuffer));
		packetBuffer->isFull = 1;
		packetBuffer->ptr = sendingPacketCopy;
		packetBuffer->timeStamp = timestamp;	//will need to change later
		s->senderWindowBuffer[positionInArray] = *packetBuffer;
		conn_sendpkt(s->c, &s->sender.packet, s->sender.packet.len);
		memset(&s->sender.packet, 0, sizeof(&s->sender.packet));
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
				struct WindowBuffer *packet = malloc(sizeof(struct WindowBuffer));
				packet = &r->receiverWindowBuffer[i];
//				fprintf(stderr, "Size of packet: %i, Data from packet: %s", packet->ptr->len, packet->ptr->data);
				packet->ptr->data[packet->ptr->len - DATA_PACKET_HEADER] = '\0';
				packet->ptr->len++;


				int j;
				int start = packet->ptr->len - DATA_PACKET_HEADER;
				for (j = start; j < MAX_DATA_SIZE; j++) {
					packet->ptr->data[j] = '\0';
				}

//				fprintf(stderr, "Size of packet: %i, Data from packet: %s", packet->ptr->len, packet->ptr->data);


				int val = conn_output(r->c, packet->ptr->data, packet->ptr->len);
				free(packet->ptr);
//				fprintf(stderr, "Return val from conn_output: %i\n", val);
//				memset(packet, "0", sizeof(packet));
				r->receiver.buffer_position++;
				r->receiverWindowBuffer[i].outputted = 1;
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
		if (r->senderWindowBuffer[i].isFull == 1) {  //you have already sent packet[i]
			if (r->senderWindowBuffer[i].acknowledged == 0) { // you have not received ack for packet[i]
				if ((timestamp - r->senderWindowBuffer[i].timeStamp) > 5) { //it has been more than 5 time periods
//					fprintf(stderr, "Retransmitted on location: %i\n", i);
					retransmit(r, i);
				}
			}
		}
	}
}
