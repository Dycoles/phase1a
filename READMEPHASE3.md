README FILE FOR PHASE 3
Dylan Coles
Jack Williams

Test Case #10: Our code runs slightly faster than Russ's code. Russ mentioned that 
this is likely because he uses more context switches than we do. 

Child1a(): starting
+Child1a(): current time = 39   Should be close, but does not have to be an exact match
+Child1a(): current time = 45   Should be close, but does not have to be an exact match
 Child1a(): current time = 52   Should be close, but does not have to be an exact match
-Child1a(): current time = 61   Should be close, but does not have to be an exact match
-Child1a(): current time = 67   Should be close, but does not have to be an exact match
 Child1a(): done
 start3(): calling Spawn for Child1b
 Child1b(): starting

As the test case mentions, the run time "should be close, but does not have to be an exact match". Other than
fast run times, our code should meet specifications here.

Test Case #20: This test case is meant to ensure that we are counting semaphors correctly (it runs 3 Ps and 3Vs).
The reason for the difference in output is likely due to how we are handling blocking. On the last P, we block until the
next V. It's possible the test case handles blocking slightly differently, so this last block may be quicker or not happen at
all, resulting in a slightly delayed start3() print

 We get this output when running the code:

 start3(): spawn 5
 Child1(): After P attempt #2
 Child2(): starting
-start3(): After V -- may appear before: Child1(): After P attempt #3
 Child1(): After P attempt #3 -- may appear before: start3(): After V
+start3(): After V -- may appear before: Child1(): After P attempt #3
 Child2(): After V attempt #0
 Child1(): After P attempt #4
 Child1(): done

Start3() prints slightly later than the test case. As the test case comments mention, this is still technically correct ->

 Child1(): After P attempt #3 -- may appear before: start3(): After V
+start3(): After V

The child() print prints before start3, which the comment says is OK. We should meet the requirements for this test case

Test Case #26: This test case is meant to ensure counting semaphores works across 25000 P/V calls, with an extra blocking P call
that requires the low-priority process to run in order to unblock that last P for each child. How this process works is Child1
and Child2 will race for CPU time. Each child will call the child_common_func. This function runs semV 250000 times, then it
runs semP 250000 times, then it runs semP one more time. Each process will block on its semaphor (sem1 for child 1 and 
sem2 for child 2). 

Afterwards, once Child1 and Child2 are blocked, LP_Child (the low priority process) will run and call semV(sem1) 
and semV(sem2) respectively, allowing child1 to unblock followed by child2

Now, 90% of the time, the test case runs and passes correctly. However, sometimes child2 is allocated a bit more CPU time
than child1, resulting in it finishing faster than child1. As a result, the child1 and child2 block prints are swapped:

Child2(): Semaphore 1 created.  I will now call V on it 250000 times.
 Child1(): V operations completed.  I will now call P on the semaphore the same number of times.
 Child2(): V operations completed.  I will now call P on the semaphore the same number of times.
-Child1(): P operations completed.  I will now call P once more; this will force the process to block, until the Low-Priority Child is able to give us one more V operation.
 Child2(): P operations completed.  I will now call P once more; this will force the process to block, until the Low-Priority Child is able to give us one more V operation.
+Child1(): P operations completed.  I will now call P once more; this will force the process to block, until the Low-Priority Child is able to give us one more V operation.
 LP_Child(): The low-priority child is finally running.  This must not happen until both Child1,Child2 have blocked on their last P operation.
 Child1(): Last P operation has returned.  This process will terminate.
 start3(): child 4 returned status of 1

 Unfortunately, there is no way for us to force child2 to give more CPU time to child1 to always get it to print correctly.
 Luckily, this shouldn't matter regardless. It doesn't matter whether Child1 or Child2 blocks first, they both end up blocked
 regardless. It is only when they are both blocked that LP_Child is allowed to run. Because it calls SemV(sem1)
 first and SemV(sem2) second, the processes will always unblock in the correct order.

 As a result, the race condition does not actually matter, and we get the same overall output regardless of which process
 blocks first. We should meet the requirements to get points for this problem.
