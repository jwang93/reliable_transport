reliable_transport
==================

##CS 356, Lab 1

README

Jay Wang (jmw86)
Harris Osserman (hgo)

Why our program cannot communicate with the Reference program:

Our Reliable program is able to receive packets sent by Reference.  But, when Reliable sends the ack packets to Reference, Reference is unable to read the ack number from our ack packet.  We tried debugging this by putting Reference in debug mode, but regardless of what value our ack number was in the packet, Reference always thought that the ack number was 65536.  We even tried specifically setting the ack number to be 5 and 300, but the ack number received by Reference was always 65536.  We believe that this was occurring because Reliable was not reading the correct memory location of the packet that we sent.  Unfortunately, we were unable to debug this further because Reliable is a "black box," and we are unable to see the implementation details of the program. 