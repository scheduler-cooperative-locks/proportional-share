#ifndef __RDTSC_H__
#define __RDTSC_H__

static __inline__ unsigned long long rdtsc(void)
{
	unsigned hi, lo;
	asm volatile ("rdtsc\n\t"
		  "mov %%edx, %0\n\t"
		  "mov %%eax, %1\n\t"
		  : "=r" (hi), "=r" (lo)
		  :: "%rax", "%rbx", "%rcx", "%rdx");
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

static __inline__ unsigned long long rdtsc_with_fence(void)
{
	unsigned hi, lo;
	asm volatile ("cpuid\n\t"
		  "rdtsc\n\t"
		  "mov %%edx, %0\n\t"
		  "mov %%eax, %1\n\t"
		  : "=r" (hi), "=r" (lo)
		  :: "%rax", "%rbx", "%rcx", "%rdx");
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

static __inline__ unsigned long long rdtscp(void)
{
	unsigned hi, lo;
	asm volatile ("rdtscp\n\t"
		  "mov %%edx, %0\n\t"
		  "mov %%eax, %1\n\t"
		  : "=r" (hi), "=r" (lo)
		  :: "%rax", "%rbx", "%rcx", "%rdx");
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

static __inline__ unsigned long long rdtscp_before_fence(void)
{
	unsigned hi, lo;
	asm volatile ("rdtscp\n\t"
		  "mov %%edx, %0\n\t"
		  "mov %%eax, %1\n\t"
		  "cpuid\n\t"
		  : "=r" (hi), "=r" (lo)
		  :: "%rax", "%rbx", "%rcx", "%rdx");
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

#endif // __RDTSC_H__
