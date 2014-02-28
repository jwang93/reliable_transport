reliable_transport
==================

##CS 356, Lab 1


####Known Bugs:

1. can't handle window size of 1 right now... that's because we're not preventing the sender from sending stuff out of window 
     -- can be handled by imposing a restriction on what can be taken in 
     -- need to figure out good solution for both sender and receiver 
     -- there's the hack solution where you allow it 1k size and you just slide the start point down the array 

2. cksum doesn't work with ./reference...? don't know why. also don't know how to use ./reference with window size  (put the argument in earlier)
	-- must be because we are computing checksums in different ways 
	-- checksum is supposed to be computed on the entire packet 

3. need to clear the memory buffer space... right now you can't send words of different lengths.. it gets all garbled 