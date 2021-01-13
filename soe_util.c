/*
MIT License

Copyright (c) 2021 Ben Buhrow

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "soe.h"
#include "util.h"
#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>
#include "soe_impl.h"

#ifdef USE_AVX512F
ALIGNED_MEM uint64_t presieve_largemasks[16][173][8];
ALIGNED_MEM uint32_t presieve_steps[32];
ALIGNED_MEM uint32_t presieve_primes[32];
ALIGNED_MEM uint32_t presieve_p1[32];

#else
// for storage of presieving lists from prime index 24 to 40 (97 to 173 inclusive)
ALIGNED_MEM uint64_t presieve_largemasks[16][173][4];
ALIGNED_MEM uint32_t presieve_steps[32];
ALIGNED_MEM uint32_t presieve_primes[32];
ALIGNED_MEM uint32_t presieve_p1[32];
#endif

uint64_t estimate_primes_in_range(uint64_t lowlimit, uint64_t highlimit)
{
	uint64_t hi_est, lo_est;

	hi_est = (uint64_t)(highlimit/log((double)highlimit));
	if (lowlimit > 1)
		lo_est = (uint64_t)(lowlimit/log((double)lowlimit));
	else
		lo_est = 0;

	return (uint64_t)((double)(hi_est - lo_est) * 1.2);
}

// row: numclasses 2 thru 480
// col: lowlimit, 10^15 thru 18
// rough tuning of where bitmap sieving is effective, as measured
// on a Intel Xeon CPU E5-2697 (AVX2 haswell) with one thread.
// numbers are the start index at which to start the bitmap sieve.
// Note: 480 classes seems to be too many to be efficient (all class
// lines must remain in memory during the bitmap sieve - this gets
// to be a lot for 480 classes!).  The same is true to a lesser
// extent for 48... but above 10^18 it is still a win.
// 
// the tuning is different on a 5122 Gold SkylakeX processor...
// likely it depends on cache size/speed.
#if 1
uint32_t bitmap_bound_tab[4][5] = {
//	/*        10^14,     10^15,     10^16,     10^17,     10^18*/
//	/*2*/   { 200000,    700000,    500000,    700000,    200000  },
//	/*8*/   { 999999999, 700000,    700000,    700000,    700000 },
//	/*48*/  { 999999999, 999999999, 999999999, 999999999, 5000000   },
//	/*480*/ { 999999999, 999999999, 999999999, 999999999, 999999999 } };
#if 1
/*            10^14,     10^15,     10^16,     10^17,     10^18*/
/*2*/       { 200000,    700000,    500000,    700000,    500000  },
/*8*/       { 999999999, 999999999, 999999999, 999999999, 700000 },
/*48*/      { 999999999, 999999999, 999999999, 999999999, 999999999 },
/*480*/     { 999999999, 999999999, 999999999, 999999999, 999999999 } };
#else
/*            10^14,     10^15,     10^16,     10^17,     10^18*/
/*2*/   { 200000,    700000,    500000,    700000,    200000  },
/*8*/   { 999999999, 700000,    700000,    700000,    700000 },
/*48*/  { 999999999, 999999999, 999999999, 999999999, 5000000 },
/*480*/ { 999999999, 999999999, 999999999, 999999999, 999999999 } };
#endif
#else
uint32_t bitmap_bound_tab[4][5] = {
	/*        10^14,     10^15,     10^16,     10^17,     10^18*/
	/*2*/   { 999999999, 999999999, 999999999, 999999999, 999999999 },
	/*8*/   { 999999999, 999999999, 999999999, 999999999, 999999999 },
	/*48*/  { 999999999, 999999999, 999999999, 999999999, 999999999 },
	/*480*/ { 999999999, 999999999, 999999999, 999999999, 999999999 } };
#endif

uint32_t modinv_1(uint32_t a, uint32_t p) {

    /* thanks to the folks at www.mersenneforum.org */

    uint32_t ps1, ps2, parity, dividend, divisor, rem, q, t;


    q = 1;
    rem = a;
    dividend = p;
    divisor = a;
    ps1 = 1;
    ps2 = 0;
    parity = 0;

    while (divisor > 1) {
        rem = dividend - divisor;
        t = rem - divisor;
        if (rem >= divisor) {
            q += ps1; rem = t; t -= divisor;
            if (rem >= divisor) {
                q += ps1; rem = t; t -= divisor;
                if (rem >= divisor) {
                    q += ps1; rem = t; t -= divisor;
                    if (rem >= divisor) {
                        q += ps1; rem = t; t -= divisor;
                        if (rem >= divisor) {
                            q += ps1; rem = t; t -= divisor;
                            if (rem >= divisor) {
                                q += ps1; rem = t; t -= divisor;
                                if (rem >= divisor) {
                                    q += ps1; rem = t; t -= divisor;
                                    if (rem >= divisor) {
                                        q += ps1; rem = t;
                                        if (rem >= divisor) {
                                            q = dividend / divisor;
                                            rem = dividend % divisor;
                                            q *= ps1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        q += ps2;
        parity = ~parity;
        dividend = divisor;
        divisor = rem;
        ps2 = ps1;
        ps1 = q;
    }

    if (parity == 0)
        return ps1;
    else
        return p - ps1;
}

uint32_t modinv_1b(uint32_t a, uint32_t p) {

    /* thanks to the folks at www.mersenneforum.org */

    /* modification: p is fixed at 2^32.  a is only valid if odd */

    uint64_t dividend = (uint64_t)0x1 << 32;
    uint32_t ps1, ps2, parity, divisor, rem, q, t;

    q = 1;
    rem = a;
    //dividend = p;
    divisor = a;
    ps1 = 1;
    ps2 = 0;
    parity = 0;

    while (divisor > 1) {
        rem = (uint32_t)(dividend - (uint64_t)divisor);
        t = rem - divisor;
        if (rem >= divisor) {
            q += ps1; rem = t; t -= divisor;
            if (rem >= divisor) {
                q += ps1; rem = t; t -= divisor;
                if (rem >= divisor) {
                    q += ps1; rem = t; t -= divisor;
                    if (rem >= divisor) {
                        q += ps1; rem = t; t -= divisor;
                        if (rem >= divisor) {
                            q += ps1; rem = t; t -= divisor;
                            if (rem >= divisor) {
                                q += ps1; rem = t; t -= divisor;
                                if (rem >= divisor) {
                                    q += ps1; rem = t; t -= divisor;
                                    if (rem >= divisor) {
                                        q += ps1; rem = t;
                                        if (rem >= divisor) {
                                            q = (uint32_t)(dividend / (uint64_t)divisor);
                                            rem = (uint32_t)(dividend % (uint64_t)divisor);
                                            q *= ps1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        q += ps2;
        parity = ~parity;
        dividend = divisor;
        divisor = rem;
        ps2 = ps1;
        ps1 = q;
    }

    if (parity == 0)
        return ps1;
    else
        return 0xFFFFFFFF - ps1 + 1;
}

uint32_t modinv_1c(uint32_t a, uint32_t p) {

    /* thanks to the folks at www.mersenneforum.org */
    // for use when it is known that p >> a, in which case
    // the first set of if/else blocks can be skipped
    uint32_t ps1, ps2, parity, dividend, divisor, rem, q, t;

    q = p / a;
    rem = p % a;
    dividend = a;
    divisor = rem;
    ps1 = q;
    ps2 = 1;
    parity = ~0;

    while (divisor > 1) {
        rem = dividend - divisor;
        t = rem - divisor;
        if (rem >= divisor) {
            q += ps1; rem = t; t -= divisor;
            if (rem >= divisor) {
                q += ps1; rem = t; t -= divisor;
                if (rem >= divisor) {
                    q += ps1; rem = t; t -= divisor;
                    if (rem >= divisor) {
                        q += ps1; rem = t; t -= divisor;
                        if (rem >= divisor) {
                            q += ps1; rem = t; t -= divisor;
                            if (rem >= divisor) {
                                q += ps1; rem = t; t -= divisor;
                                if (rem >= divisor) {
                                    q += ps1; rem = t; t -= divisor;
                                    if (rem >= divisor) {
                                        q += ps1; rem = t;
                                        if (rem >= divisor) {
                                            q = dividend / divisor;
                                            rem = dividend % divisor;
                                            q *= ps1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        q += ps2;
        parity = ~parity;
        dividend = divisor;
        divisor = rem;
        ps2 = ps1;
        ps1 = q;
    }

    if (parity == 0)
        return ps1;
    else
        return p - ps1;
}

uint64_t spGCD(uint64_t x, uint64_t y)
{
    uint64_t a, b, c;
    a = x; b = y;
    while (b != 0)
    {
        c = a % b;
        a = b;
        b = c;
    }
    return a;
}

void get_numclasses(uint64_t highlimit, uint64_t lowlimit, soe_staticdata_t *sdata)
{
	uint64_t numclasses, prodN, startprime;

    sdata->use_monty = 0;

	sieve_line_ptr = &sieve_line;
    sdata->FLAGBITS = 18;
    sdata->BUCKETSTARTI = 33336;

    sdata->FLAGSIZE = 8 * sdata->SOEBLOCKSIZE;
    sdata->FLAGSIZEm1 = sdata->FLAGSIZE - 1;

    switch (sdata->SOEBLOCKSIZE)
    {
    case 32768:
        sdata->FLAGBITS = 18;
        sdata->BUCKETSTARTI = 33336;

        // the avx2 version is faster on avx512 capable cpus...
#ifdef USE_AVX512Fa
        sieve_line_ptr = &sieve_line_avx512_32k;
#elif defined(USE_AVX2)
        sieve_line_ptr = &sieve_line_avx2_32k;
#endif
        break;
    case 65536:
        sdata->FLAGBITS = 19;
        sdata->BUCKETSTARTI = 43392;
        break;
    case 131072:
        sdata->FLAGBITS = 20;
        sdata->BUCKETSTARTI = 123040;
#ifdef USE_AVX512F
        sieve_line_ptr = &sieve_line_avx512_128k;
#elif defined(USE_AVX2)
        sieve_line_ptr = &sieve_line_avx2_128k;
#endif
        break;
    case 262144:
#ifdef USE_AVX512F
        sieve_line_ptr = &sieve_line_avx512_256k;
#elif defined(USE_AVX2)

#endif
        sdata->FLAGBITS = 21;
        sdata->BUCKETSTARTI = 233416;
        break;
    case 524288:
#ifdef USE_AVX512F
        sieve_line_ptr = &sieve_line_avx512_512k;
#elif defined(USE_AVX2)
        // the non-avx2 sieve is better, at least,
        // for huge offsets when you might be using this blocksize.
        //sieve_line_ptr = &sieve_line_avx2_512k;
#endif
        sdata->FLAGBITS = 22;
        sdata->BUCKETSTARTI = 443920;
        break;
    case 1048576:
        sdata->FLAGBITS = 23;
        sdata->BUCKETSTARTI = 846248;
        break;
    default:
        printf("Bad soe_block\n");
        exit(1);
    }

	//printf("Sieve Parameters:\nBLOCKSIZE = %u\nFLAGSIZE = %u\nFLAGBITS = %u\nBUCKETSTARTI = %u\n",
	//	SOEBLOCKSIZE, FLAGSIZE, FLAGBITS, BUCKETSTARTI);

	//more efficient to sieve using mod210 when the range is big
	if ((highlimit - lowlimit) > 40000000000ULL)
	{
        if (lowlimit < 100000000000000ULL)
        {
            numclasses = 480;
            prodN = 2310;
            startprime = 5;
        }
        else
        {
            numclasses = 48;
            prodN = 210;
            startprime = 4;
        }
#if defined(USE_AVX2)
        sdata->use_monty = 1;
#endif
	}	
	else if ((highlimit - lowlimit) > 4000000000ULL)
	{
        numclasses = 48;
        prodN = 210;
        startprime = 4;
#if defined(USE_AVX2)
        sdata->use_monty = 1;
#endif
	}
	else if ((highlimit - lowlimit) > 100000000)
	{
		numclasses=8;
		prodN=30;
		startprime=3;
#if defined(USE_AVX2)
        sdata->use_monty = 1;
#endif
	}
	else
	{
		numclasses=2;
		prodN=6;
		startprime=2;
	}

	sdata->numclasses = numclasses;
	sdata->prodN = prodN;
	sdata->startprime = startprime;

	return;
}

int check_input(uint64_t highlimit, uint64_t lowlimit, uint32_t num_sp, uint32_t *sieve_p,
	soe_staticdata_t *sdata, mpz_t offset)
{
	int i;

	sdata->orig_hlimit = highlimit;
	sdata->orig_llimit = lowlimit;

	// the wrapper should handle this, but just in case we are called
	// directly and not via the wrapper...
	if (highlimit - lowlimit < 1000000)
		highlimit = lowlimit + 1000000;

	if ((highlimit - lowlimit) > 1000000000000ULL)
	{
		printf("range too big\n");
		return 1;
	}

	if (highlimit > 4000000000000000000ULL)
	{
		printf("input too high\n");
		return 1;
	}

	// set sieve primes in the local data structure to the ones that were passed in
	sdata->sieve_p = sieve_p;	
	
	if (offset == NULL)
	{
		//see if we were provided enough primes to do the job
		sdata->pbound = (uint64_t)(sqrt((int64_t)(highlimit)));

		if (sieve_p[num_sp - 1] < sdata->pbound)
		{
			printf("found %d primes, max = %u: not enough sieving primes\n", 
                num_sp, sieve_p[num_sp - 1]);
			exit(1);
		}

		// find the highest index that we'll need.  Much of the rest of the code is 
		// sensitive to this.  Note that this could be slow for large numbers of
		// sieve primes... could replace with a binary search.
		for (i=0; i<num_sp; i++)
		{
			// stop when we have enough for this input
            if (sieve_p[i] > sdata->pbound)
            {
                break;
            }
		}
		sdata->pboundi = i;	

#ifdef USE_AVX2
        // plus perhaps a few extra to get us to a convienient vector boundary
        while (sdata->pboundi & 7) sdata->pboundi++;
#endif
		
		sdata->offset = NULL;
		sdata->sieve_range = 0;
	}
	else
	{
		// for ranges with offsets, don't worry if we don't have enough
		// primes, but still check to see if we have too many.
		mpz_t tmpz;

		mpz_init(tmpz);
		mpz_add_ui(tmpz, offset, highlimit);
		mpz_sqrt(tmpz, tmpz);

		if (mpz_cmp_ui(tmpz, sieve_p[num_sp - 1]) < 0)
		{
			// then we were passed too many.  truncate the input list.
			sdata->pbound = mpz_get_ui(tmpz);
			for (i=0; i<num_sp; i++)
			{
				// stop when we have enough for this input
				if (sieve_p[i] > sdata->pbound)
					break;
			}
			sdata->pboundi = i;	
		}
		else
		{
			// use all of 'em.
			sdata->pbound = sieve_p[num_sp - 1];
			sdata->pboundi = num_sp;
		}
		sdata->offset = offset;
		mpz_clear(tmpz);
		sdata->sieve_range = 1;
	}

	return 0;
}

uint64_t init_sieve(soe_staticdata_t *sdata)
{
    int i, k;
    uint64_t numclasses = sdata->numclasses;
    uint64_t prodN = sdata->prodN;
    uint64_t allocated_bytes = 0;
    uint64_t lowlimit = sdata->orig_llimit;
    uint64_t highlimit = sdata->orig_hlimit;
    uint64_t numbytes, numlinebytes;


    // some groupings of residue classes and blocks per thread
    // 240 = 48 * 5
    // 240 = 8 * 30
    // 240 = 16 * 15
    // 256 = 8 * 32
    // 256 = 16 * 16
    // 272 = 16 * 17
    // 272 = 8 * 34

    // masks for removing or reading single bits in a byte.  nmasks are simply
    // the negation of these masks, and are filled in within the spSOE function.
    uint8_t masks[8] = { 0xfe, 0xfd, 0xfb, 0xf7, 0xef, 0xdf, 0xbf, 0x7f };
    uint8_t nmasks[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
    uint32_t masks32[32] = {
        0xfffffffe, 0xfffffffd, 0xfffffffb, 0xfffffff7,
        0xffffffef, 0xffffffdf, 0xffffffbf, 0xffffff7f,
        0xfffffeff, 0xfffffdff, 0xfffffbff, 0xfffff7ff,
        0xffffefff, 0xffffdfff, 0xffffbfff, 0xffff7fff,
        0xfffeffff, 0xfffdffff, 0xfffbffff, 0xfff7ffff,
        0xffefffff, 0xffdfffff, 0xffbfffff, 0xff7fffff,
        0xfeffffff, 0xfdffffff, 0xfbffffff, 0xf7ffffff,
        0xefffffff, 0xdfffffff, 0xbfffffff, 0x7fffffff };
    uint32_t nmasks32[32] = {
        0x00000001, 0x00000002, 0x00000004, 0x00000008,
        0x00000010, 0x00000020, 0x00000040, 0x00000080,
        0x00000100, 0x00000200, 0x00000400, 0x00000800,
        0x00001000, 0x00002000, 0x00004000, 0x00008000,
        0x00010000, 0x00020000, 0x00040000, 0x00080000,
        0x00100000, 0x00200000, 0x00400000, 0x00800000,
        0x01000000, 0x02000000, 0x04000000, 0x08000000,
        0x10000000, 0x20000000, 0x40000000, 0x80000000 };

    for (i = 0; i < 8; i++)
    {
        sdata->masks[i] = masks[i];
        sdata->nmasks[i] = nmasks[i];
    }
    
    for (i = 0; i < 32; i++)
    {
        sdata->masks32[i] = masks32[i];
        sdata->nmasks32[i] = nmasks32[i];
    }

    // allocate the residue classes.  
    sdata->rclass = (uint32_t *)malloc(numclasses * sizeof(uint32_t));
    allocated_bytes += numclasses * sizeof(uint32_t);

    // find the residue classes
    k = 0;
    for (i = 1; i < prodN; i++)
    {
        if (spGCD(i, (uint64_t)prodN) == 1)
        {
            sdata->rclass[k] = (uint32_t)i;
            k++;
        }
    }

    sdata->min_sieved_val = 1ULL << 63;

    // temporarily set lowlimit to the first multiple of numclasses*prodN < lowlimit
    if (sdata->sieve_range == 0)
    {
        lowlimit = (lowlimit / (numclasses*prodN))*(numclasses*prodN);
        sdata->lowlimit = lowlimit;
    }
    else
    {
        mpz_t tmpz, tmpz2;
        mpz_init(tmpz);
        mpz_init(tmpz2);

        //the start of the range of interest is controlled by offset, not lowlimit
        //figure out how it needs to change to accomodate sieving
        mpz_tdiv_q_ui(tmpz, *sdata->offset, numclasses * prodN);
        mpz_mul_ui(tmpz, tmpz, numclasses * prodN);
        mpz_sub(tmpz2, *sdata->offset, tmpz);

        //raise the high limit by the amount the offset was lowered, so that
        //we allocate enough flags to cover the range of interest
        highlimit += mpz_get_ui(tmpz2);
        sdata->orig_hlimit += mpz_get_ui(tmpz2);

        //also raise the original lowlimit so that we don't include sieve primes
        //that we shouldn't when finalizing the process.
        sdata->orig_llimit += mpz_get_ui(tmpz2);

        //copy the new value to the pointer, which will get passed back to sieve_to_depth
        mpz_set(*sdata->offset, tmpz);
        mpz_clear(tmpz);
        mpz_clear(tmpz2);

        //set the lowlimit to 0; the real start of the range is controlled by offset
        sdata->lowlimit = 0;
    }

    // reallocate flag structure for wheel and block sieving.
    // we want the smallest number of blocks such that all integers in the requested
    // range are represented.  Each block contains 32768 * 8 bit-flags, each of
    // which represents integers spaced 'prodN' apart.
    sdata->blocks = (highlimit - lowlimit) / prodN / sdata->FLAGSIZE;
    if (((highlimit - lowlimit) / prodN) % sdata->FLAGSIZE != 0) sdata->blocks++;
    sdata->numlinebytes = sdata->blocks * sdata->SOEBLOCKSIZE;
    numlinebytes = sdata->numlinebytes;
    highlimit = (uint64_t)((uint64_t)sdata->numlinebytes * (uint64_t)prodN * (uint64_t)BITSINBYTE + lowlimit);
    sdata->highlimit = highlimit;

    // each flag in a block is spaced prodN integers apart.  record the resulting size of the 
    // number line encoded in each block.
    sdata->blk_r = sdata->FLAGSIZE*prodN;

    // compute the breakpoints at which we switch to other sieving methods	
    sdata->num_bitmap_primes = 0;
    sdata->bitmap_start_id = sdata->pboundi;
    if (sdata->pboundi > sdata->BUCKETSTARTI)
    {
        sdata->bucket_start_id = sdata->BUCKETSTARTI;

        // also see if a bitmap sieving step will be beneficial.
        sdata->bitmap_lower_bound = 99999999999999ULL;
        if (((int)log10(sdata->orig_llimit) - 14) >= 0)
        {
            sdata->bitmap_start_id = MIN(
                bitmap_bound_tab[sdata->startprime - 2][(int)log10(sdata->orig_llimit) - 14],
                sdata->pboundi);

            if ((sdata->bitmap_start_id < sdata->pboundi))
                sdata->num_bitmap_primes = sdata->pboundi - sdata->bitmap_start_id;
            else
                sdata->bitmap_start_id = sdata->pboundi;
        }

        // buckets operate up to the bitmap bound (which may be the 
        // equal to the sieve bound, if not benficial).
        sdata->num_bucket_primes = sdata->bitmap_start_id - sdata->bucket_start_id;
    }
    else
    {
        sdata->num_bucket_primes = 0;
        sdata->bucket_start_id = sdata->pboundi;
    }

    // any prime larger than this will only hit the interval once (in residue space)
    sdata->large_bucket_start_prime = sdata->blocks * sdata->FLAGSIZE;

    // allocate space for the root of each sieve prime (used by the bucket sieve)
    sdata->root = (int *)xmalloc_align(sdata->bitmap_start_id * sizeof(int));
    allocated_bytes += sdata->bitmap_start_id * sizeof(uint32_t);
    if (sdata->root == NULL)
    {
        printf("error allocating roots\n");
        exit(-1);
    }
    else
    {
        if (sdata->VFLAG > 2)
        {
            printf("allocated %u bytes for roots\n",
                (uint32_t)(sdata->bitmap_start_id * sizeof(uint32_t)));
        }
    }

    sdata->r2modp = (uint32_t *)xmalloc_align(sdata->bitmap_start_id * sizeof(uint32_t));
    allocated_bytes += sdata->bitmap_start_id * sizeof(uint32_t);
    if (sdata->r2modp == NULL)
    {
        printf("error allocating r2modp\n");
        exit(-1);
    }
    else
    {
        if (sdata->VFLAG > 2)
        {
            printf("allocated %u bytes for r2modp\n",
                (uint32_t)(sdata->bitmap_start_id * sizeof(uint32_t)));
        }
    }

    // experimental montgomery arithmetic
    sdata->pinv = (uint32_t *)xmalloc_align(sdata->bitmap_start_id * sizeof(uint32_t));
    allocated_bytes += sdata->bitmap_start_id * sizeof(uint32_t);
    if (sdata->pinv == NULL)
    {
        printf("error allocating pinv\n");
        exit(-1);
    }
    else
    {
        if (sdata->VFLAG > 2)
        {
            printf("allocated %u bytes for pinv\n",
                (uint32_t)(sdata->bitmap_start_id * sizeof(uint32_t)));
        }
    }


    // these are used by the bucket sieve
    sdata->lower_mod_prime = (uint32_t *)xmalloc_align(sdata->bitmap_start_id * sizeof(uint32_t));
    allocated_bytes += sdata->bitmap_start_id * sizeof(uint32_t);
    if (sdata->lower_mod_prime == NULL)
    {
        printf("error allocating lower mod prime\n");
        exit(-1);
    }
    else
    {
        if (sdata->VFLAG > 2)
        {
            printf("allocated %u bytes for lower mod prime\n",
                (uint32_t)sdata->bitmap_start_id * (uint32_t)sizeof(uint32_t));
        }
    }

    // allocate all of the lines if we are computing primes or if we
    // are using bitmap sieving.  otherwise lines will
    // be allocated as needed during sieving
    sdata->lines = (uint8_t **)xmalloc_align(sdata->numclasses * sizeof(uint8_t *));
    numbytes = 0;
    
    if ((sdata->only_count == 0) || (sdata->num_bitmap_primes > 0))
    {
        //actually allocate all of the lines as a continuous linear array of bytes
        //sdata->lines[0] = (uint8_t *)xmalloc_align(numlinebytes * sdata->numclasses * sizeof(uint8_t));
        //if (sdata->lines[0] == NULL)
        //{
        //    printf("error allocated sieve lines\n");
        //    exit(-1);
        //}
        numbytes += sdata->numclasses * numlinebytes * sizeof(uint8_t);

        for (i = 0; i < sdata->numclasses; i++)
        {
            //sdata->lines[i] = sdata->lines[0] + i * numlinebytes;
            sdata->lines[i] = (uint8_t*)xmalloc_align(numlinebytes * sizeof(uint8_t));
        }
    }
    else
    {
        //don't allocate anything now, but
        //provide an figure for the memory that will be allocated later
        numbytes = numlinebytes * sizeof(uint8_t) * sdata->THREADS;
    }


#ifdef USE_AVX2
    // during presieveing, storing precomputed lists will start to get unwieldy, so
    // generate the larger lists here.
#ifdef USE_AVX512Fa
#define DYNAMIC_BOUND 512
#else
#define DYNAMIC_BOUND 256
#endif

    int j;
    sdata->presieve_max_id = 40;
    
    for (j = 24; j < sdata->presieve_max_id; j++)
    {
        uint32_t prime = sdata->sieve_p[j];

        // for each possible starting location
        for (i = 0; i < prime; i++)
        {
            int x;
            uint64_t interval[DYNAMIC_BOUND/64];

            for (x = 0; x < DYNAMIC_BOUND/64; x++)
                interval[x] = 0xffffffffffffffffULL;

            // sieve up to the bound, printing each 64-bit word as we fill it
            for (k = i; k < DYNAMIC_BOUND; k += prime)
            {
                interval[k >> 6] &= ~(1ULL << (k & 63));
            }

            //printf("largemask[%d][%d] = %016lx, %016lx, %016lx, %016lx", j-24, i,
            //    interval[0], interval[1], interval[2], interval[3]);
            presieve_largemasks[j - 24][i][0] = interval[0];
            presieve_largemasks[j - 24][i][1] = interval[1];
            presieve_largemasks[j - 24][i][2] = interval[2];
            presieve_largemasks[j - 24][i][3] = interval[3];
#ifdef USE_AVX512Fa
            //printf(", %016lx, %016lx, %016lx, %016lx", 
            //    interval[4], interval[5], interval[6], interval[7]);
            presieve_largemasks[j - 24][i][4] = interval[4];
            presieve_largemasks[j - 24][i][5] = interval[5];
            presieve_largemasks[j - 24][i][6] = interval[6];
            presieve_largemasks[j - 24][i][7] = interval[7];
#endif
            //printf("\n");

        }        
    }

    //printf("primes: ");
    for (j = 24; j < sdata->presieve_max_id; j++)
    {        
        presieve_primes[j - 24] = sdata->sieve_p[j];
        //printf("%u ", presieve_primes[j - 24]);
    }
    //printf("\n");

    for (j = 24; j < sdata->presieve_max_id; j++)
    {
        presieve_p1[j - 24] = sdata->sieve_p[j] - 1;
    }

    //printf("steps: ");
    for (j = 24; j < sdata->presieve_max_id; j++)
    {        
        presieve_steps[j - 24] = DYNAMIC_BOUND % sdata->sieve_p[j];
        //printf("%u ", presieve_steps[j - 24]);
    }
    //printf("\n");

#endif

#if defined(USE_BMI2) || defined(USE_AVX512F)
#ifdef __INTEL_COMPILER
    if (_may_i_use_cpu_feature(_FEATURE_BMI))
#elif defined(__GNUC__)
    if (__builtin_cpu_supports("bmi2"))
#else
    if (0)
#endif
    {
        compute_8_bytes_ptr = &compute_8_bytes_bmi2;
    }
    else
    {
        compute_8_bytes_ptr = &compute_8_bytes;
    }
#else
    compute_8_bytes_ptr = &compute_8_bytes;
#endif

#if defined(USE_AVX2)
#ifdef __INTEL_COMPILER
    if (_may_i_use_cpu_feature(_FEATURE_AVX2))
#elif defined(__GNUC__)
    if (__builtin_cpu_supports("avx2"))
#else
    if (1)
#endif
    {
        pre_sieve_ptr = &pre_sieve_avx2;
    }
    else
    {
        pre_sieve_ptr = &pre_sieve;
        sdata->presieve_max_id = 10;
    }

#ifdef USE_AVX512Fa
    pre_sieve_ptr = &pre_sieve_avx512;
#endif

#else
    // if we haven't built the code with AVX2 support, or if at runtime
    // we find that AVX2 isn't supported, use the portable version
    // of these routines.
    pre_sieve_ptr = &pre_sieve;
    sdata->presieve_max_id = 10;
#endif

    if (sdata->VFLAG > 2)
    {
        printf("allocated %" PRIu64 " bytes for sieve lines\n", numbytes);
    }
	allocated_bytes += numbytes;

	return allocated_bytes;
}

void set_bucket_depth(soe_staticdata_t *sdata)
{
	uint64_t numlinebytes = sdata->numlinebytes;
	int i;

	if (sdata->num_bucket_primes > 0)
	{
		// then we have primes bigger than BUCKETSTARTP - need to bucket sieve
		uint64_t flagsperline = numlinebytes * 8;
		uint64_t num_hits = 0;
		uint64_t hits_per_bucket;
		
		for (i = sdata->bucket_start_id; i < sdata->bucket_start_id + sdata->num_bucket_primes; i++)
		{
			// condition to see if the current prime only hits the sieve interval once
			if ((sdata->sieve_p[i] * sdata->prodN) > (sdata->blk_r * sdata->blocks))
				break;
			num_hits += ((uint32_t)flagsperline / sdata->sieve_p[i] + 1);
		}

		// assume hits are evenly distributed among buckets.
		hits_per_bucket = num_hits / sdata->blocks;

		// add some margin
		hits_per_bucket = (uint64_t)((double)hits_per_bucket * 1.10);

		// set the bucket allocation amount, with a minimum of at least 50000
		// because small allocation amounts may violate the uniformity assumption
		// of hits per bucket.  The idea is to set this right once, even if it is too big,
		// so that we don't have to keep checking for full buckets in the middle of
		// the bucket sieve (which would be slow)
		sdata->bucket_alloc = MAX(hits_per_bucket,50000);

		// now count primes that only hit the interval once
		num_hits = 0;
		for (; i < sdata->bucket_start_id + sdata->num_bucket_primes; i++)
			num_hits++;

		// assume hits are evenly distributed among buckets.
		hits_per_bucket = num_hits / sdata->blocks;

		// add some margin
		hits_per_bucket = (uint64_t)((double)hits_per_bucket * 1.1);

		if (num_hits > 0)
			sdata->large_bucket_alloc = MAX(hits_per_bucket,50000);
		else
			sdata->large_bucket_alloc = 0;

	}


	return;
}

uint64_t alloc_threaddata(soe_staticdata_t *sdata, thread_soedata_t *thread_data)
{
	uint32_t bucket_alloc = sdata->bucket_alloc;
	uint32_t large_bucket_alloc = sdata->large_bucket_alloc;
	uint64_t allocated_bytes = 0;
	uint32_t bucket_depth = sdata->num_bucket_primes;
	int i,j;
	
	allocated_bytes += sdata->THREADS * sizeof(thread_soedata_t);
	for (i=0; i< sdata->THREADS; i++)
	{
		thread_soedata_t *thread = thread_data + i;

        // presieving scratch space
        thread->ddata.presieve_scratch = (uint32_t *)xmalloc_align(16 * sizeof(uint32_t));

		// allocate a bound for each block
        //printf("allocated space for %d blocks in pbounds\n", sdata->blocks);
		thread->ddata.pbounds = (uint64_t *)malloc(
			sdata->blocks * sizeof(uint64_t));
		allocated_bytes += sdata->blocks * sizeof(uint64_t);

		thread->ddata.pbounds[0] = sdata->pboundi;

		// we'll need to store the offset into the next block for each prime.
		// actually only need those primes less than BUCKETSTARTP since bucket sieving
		// doesn't use the offset array.
		j = MIN(sdata->pboundi, sdata->BUCKETSTARTI);
        thread->ddata.offsets = (uint32_t *)xmalloc_align(j * sizeof(uint32_t));
		allocated_bytes += j * sizeof(uint32_t);
		if (thread->ddata.offsets == NULL)
		{
			printf("error allocating offsets\n");
			exit(-1);
		}
		else
		{
            if (sdata->VFLAG > 2)
            {
                printf("allocated %u bytes for offsets for %d sieving primes \n",
                    (uint32_t)(j * sizeof(uint32_t)), j);
            }
		}
		thread->ddata.bucket_depth = 0;

		if (bucket_depth > 0)
		{			
			//create a bucket for each block
            thread->ddata.sieve_buckets = (uint64_t **)malloc(
                sdata->blocks * sizeof(uint64_t *));
            allocated_bytes += sdata->blocks * sizeof(uint64_t *);

			if (thread->ddata.sieve_buckets == NULL)
			{
				printf("error allocating buckets\n");
				exit(-1);
			}
			else
			{
                if (sdata->VFLAG > 2)
                {
                    printf("allocated %u bytes for bucket bases\n",
                        (uint32_t)sdata->blocks * (uint32_t)sizeof(uint64_t *));
                }
			}

			if (large_bucket_alloc > 0)
			{
				thread->ddata.large_sieve_buckets = (uint32_t **)malloc(
					sdata->blocks * sizeof(uint32_t *));
				allocated_bytes += sdata->blocks * sizeof(uint32_t *);

				if (thread->ddata.large_sieve_buckets == NULL)
				{
					printf("error allocating large buckets\n");
					exit(-1);
				}
				else
				{
                    if (sdata->VFLAG > 2)
                    {
                        printf("allocated %u bytes for large bucket bases\n",
                            (uint32_t)sdata->blocks * (uint32_t)sizeof(uint32_t*));
                    }
				}
			}
            else
            {
                thread->ddata.large_sieve_buckets = NULL;
            }

			//create a hit counter for each bucket
			thread->ddata.bucket_hits = (uint32_t *)malloc(
				sdata->blocks * sizeof(uint32_t));
			allocated_bytes += sdata->blocks * sizeof(uint32_t);
			if (thread->ddata.bucket_hits == NULL)
			{
				printf("error allocating hit counters\n");
				exit(-1);
			}
			else
			{
                if (sdata->VFLAG > 2)
                {
                    printf("allocated %u bytes for hit counters\n",
                        (uint32_t)sdata->blocks * (uint32_t)sizeof(uint32_t));
                }
			}

			if (large_bucket_alloc > 0)
			{
				thread->ddata.large_bucket_hits = (uint32_t *)malloc(
					sdata->blocks * sizeof(uint32_t));
				allocated_bytes += sdata->blocks * sizeof(uint32_t);
				if (thread->ddata.large_bucket_hits == NULL)
				{
					printf("error allocating large hit counters\n");
					exit(-1);
				}
				else
				{
                    if (sdata->VFLAG > 2)
                    {
                        printf("allocated %u bytes for large hit counters\n",
                            (uint32_t)sdata->blocks * (uint32_t)sizeof(uint32_t));
                    }
				}
			}
            else
            {
                thread->ddata.large_bucket_hits = NULL;
            }

			//each bucket must be able to hold a hit from every prime used above BUCKETSTARTP.
			//this is overkill, because every prime will not hit every bucket when
			//primes are greater than FLAGSIZE.  but we always write to the end of each
			//bucket so the depth probably doesn't matter much from a cache access standpoint,
			//just from a memory capacity standpoint, and we shouldn't be in any danger
			//of that as long as the input range is managed (should be <= 10B).
			thread->ddata.bucket_depth = bucket_depth;
			thread->ddata.bucket_alloc = bucket_alloc;
			thread->ddata.bucket_alloc_large = large_bucket_alloc;

			for (j = 0; j < sdata->blocks; j++)
			{
                thread->ddata.sieve_buckets[j] = (uint64_t *)malloc(
                    bucket_alloc * sizeof(uint64_t));
                allocated_bytes += bucket_alloc * sizeof(uint64_t);

				if (thread->ddata.sieve_buckets[j] == NULL)
				{
					printf("error allocating buckets\n");
					exit(-1);
				}			

				thread->ddata.bucket_hits[j] = 0;

				if (large_bucket_alloc > 0)
				{
					thread->ddata.large_sieve_buckets[j] = (uint32_t *)malloc(
						large_bucket_alloc * sizeof(uint32_t));
					allocated_bytes += large_bucket_alloc * sizeof(uint32_t);

					if (thread->ddata.large_sieve_buckets[j] == NULL)
					{
						printf("error allocating large buckets\n");
						exit(-1);
					}	

					thread->ddata.large_bucket_hits[j] = 0;
				}
							
			}

            if (sdata->VFLAG > 2)
            {
                printf("allocated %u bytes for buckets\n",
                    (uint32_t)sdata->blocks * (uint32_t)bucket_alloc * (uint32_t)sizeof(uint64_t));
            }

            if (sdata->VFLAG > 2)
            {
                printf("allocated %u bytes for large buckets\n",
                    (uint32_t)sdata->blocks * (uint32_t)large_bucket_alloc * (uint32_t)sizeof(uint32_t));
            }

		}	

		//this threads' count of primes in its' line
		thread->linecount = 0;
		//share the common static data structure
		thread->sdata = *sdata;
	}

	return allocated_bytes;
}

