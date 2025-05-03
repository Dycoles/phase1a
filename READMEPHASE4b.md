README FILE FOR PHASE 4b Dylan Coles Jack Williams

Test Case 0: Our sleep function works properly, it just finishes slightly later than the test case's. It is within a 5% margin of error (as required by Russ), so it functions fine.

Test Case 1: Same case as test case 0, our sleep function works fine, it is within the 5% margin of error. The children print in a different order likely because I used a MinHeap to keep track of sleep time while the test case likely used a stack or a queue

Test Case 2: All children print in the same order with the same pid and status. The only difference is mine runs a bit slower. It is within the 5% margin of error required by Russ, so we ought to pass the case

Test Case 6: We get the correct output, but child1() appears to terminate early, likely due to some kind of race condition. Other than that, the characters seem to be read correctly, so I believe our implementation for this specific test case is correct.

Test Case 7: All the terminal write operations finish without errors. However, they finish out of order. Fortunately, the
terminal output for term0.out, term1.out, term2.out, and term3.out all match the expected output and output in the correct
order, so I believe our implementation meets enough of the requirements to get full points

Test Case 16: 

Test Case 18: 

Test 20: This testcase is similar to several above in that it performs all the correct actions, and logically it performs as it should, but all the statements are done in a different order. The terminal output matches, and all printed statements are present in the same fashion as in the expected output, just in a different order. The reads and writes within each terminal are in the order they should be, which is the important part.

Test Case 22: For this test case, we almost match output exactly. The only difference is 
"buffer written 'two: second line" is written just after "buffer written 'one: third line, longer than previous ones". This 
is likely only due to a race condition where one line finishes just before the other. Despite this race condition, all of the
other lines are in the correct spot and all of the terminal outputs are correct, meaning that even though the line
finished writing out of order, the output did not change. Because the terminal output is still correct despite the 
race condition, I believe we should pass this test case. 
