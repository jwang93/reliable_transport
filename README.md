reliable_transport
==================

##CS 356, Lab 1


####Known Bugs:

1. can't handle window size of 1 right now... that's because we're not preventing the sender from sending stuff out of window 
     -- can be handled by imposing a restriction on what can be taken in 
     -- need to figure out good solution for both sender and receiver 

2. retransmission looks good, but the output ordering is incorrect... baffled 

3. cksum doesn't work with ./reference...? don't know why. also don't know how to use ./reference with window size 
	-- must be because we are computing checksums in different ways 

4. I am using a positive ack to determine when to retransmit even though we are supposed to be a cumulative ack... need to talk to Hongze about this 

5. need to clear the memory buffer space... right now you can't send words of different lengths.. it gets all garbled 