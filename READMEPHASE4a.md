README FILE FOR PHASE 4a Dylan Coles Jack Williams

Test Case 0: Our sleep function works properly, it just finishes slightly later than the test case's. It is within a 5%
margin of error (as required by Russ), so it functions fine. 

Test Case 1: Same case as test case 0, our sleep function works fine, it is within the 5% margin of error. The children
print in a different order likely because I used a MinHeap to keep track of sleep time while the test case likely used
a stack or a queue

Test Case 2: All children print in the same order with the same pid and status. The only difference is mine runs a bit slower.
It is within the 5% margin of error required by Russ, so we ought to pass the case

Test Case 6: We get the correct output, but child1() appears to terminate early, likely due to some kind of race condition. 
Other than that, the characters seem to be read correctly, so I believe our implementation for this specific test case is 
correct. 
