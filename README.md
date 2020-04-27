Scheduler-Cooperative Locks

Scheduler-Cooperative Locks (SCL) is a family of locking primitives that
controls lock usage and aligns the lock usage with system-wide scheduling
goals. SCLs are useful to avoid the scheduler subversion problem, where lock
usage patterns determine which thread runs, thereby subverting CPU scheduling
goals. For more details about SCLs, please read the paper titled "Avoiding
Scheduler Subversion problem using Scheduler-Cooperative Locks'. The paper can
be found here - https://research.cs.wisc.edu/adsl/Publications/eurosys20-scl.pdf.

The current SCLs are designed for proportional-share schedulers (Linux CFS
scheduler). In the future, lock cooperation with other types ofschedulers will
be an interesting avenue of work.  Our initial results with the ULE scheduler
are encouraging, but a complete analysis remains as future work.

We have implemented three different types of SCLS:

1. u-SCL - User-space Scheduler-Cooperative Lock is a replacement for a
standard mutex.
2. RW-SCL - Reader-Writer Scheduler-Cooperative Lock implements a reader-writer
lock.
3. k-SCL - Kernel Scheduler-Cooperative lock is a simplified version of u-SCL
and designed for usage within an OS kernel.

Please do share your views about SCLs and help us improve this work. If you
encounter a bug, please send an email to Yuvraj Patel (yuvraj@cs.wisc.edu). If
you use SCLs or plan to use SCLs, do send us a note and we will be happy to
assist you in whichever way we can. 

Thank you.
