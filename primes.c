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
#include "soe_impl.h"
#include "ytools.h"
#include "threadpool.h"
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>


//for testing one of 8 bits in a byte in one of 8 lines.
//bit num picks the row, lines num picks the col.	
const uint64_t nmasks64[8][8] = {
    { 1ULL, 256ULL, 65536ULL, 16777216ULL, 4294967296ULL, 1099511627776ULL, 281474976710656ULL, 72057594037927936ULL },
    { 2ULL, 512ULL, 131072ULL, 33554432ULL, 8589934592ULL, 2199023255552ULL, 562949953421312ULL, 144115188075855872ULL },
    { 4ULL, 1024ULL, 262144ULL, 67108864ULL, 17179869184ULL, 4398046511104ULL, 1125899906842624ULL, 288230376151711744ULL },
    { 8ULL, 2048ULL, 524288ULL, 134217728ULL, 34359738368ULL, 8796093022208ULL, 2251799813685248ULL, 576460752303423488ULL },
    { 16ULL, 4096ULL, 1048576ULL, 268435456ULL, 68719476736ULL, 17592186044416ULL, 4503599627370496ULL, 1152921504606846976ULL },
    { 32ULL, 8192ULL, 2097152ULL, 536870912ULL, 137438953472ULL, 35184372088832ULL, 9007199254740992ULL, 2305843009213693952ULL },
    { 64ULL, 16384ULL, 4194304ULL, 1073741824ULL, 274877906944ULL, 70368744177664ULL, 18014398509481984ULL, 4611686018427387904ULL },
    { 128ULL, 32768ULL, 8388608ULL, 2147483648ULL, 549755813888ULL, 140737488355328ULL, 36028797018963968ULL, 9223372036854775808ULL } };

void compute_primes_dispatch(void *vptr)
{
    tpool_t *tdata = (tpool_t *)vptr;
    soe_userdata_t *t = (soe_userdata_t *)tdata->user_data;
    soe_staticdata_t *sdata = t->sdata;

    // launch one range of computation for each thread.  don't really
    // need a threadpool for this, but the infrastructure is there...
    if (sdata->sync_count < sdata->THREADS)
    {
        tdata->work_fcn_id = 0;
        sdata->sync_count++;
    }
    else
    {
        tdata->work_fcn_id = tdata->num_work_fcn;
    }

    return;
}

void compute_primes_work_fcn(void *vptr)
{
    tpool_t *tdata = (tpool_t *)vptr;
    soe_userdata_t *udata = (soe_userdata_t *)tdata->user_data;
    soe_staticdata_t *sdata = udata->sdata;
    thread_soedata_t *t = &udata->ddata[tdata->tindex];
    int i;

    if (sdata->THREADS > 1)
    {
        t->linecount = 0;
    }

#if defined(USE_BMI2) || defined(USE_AVX512F)
    if (sdata->has_bmi2)
    {
        for (i = t->startid; i < t->stopid; i += 8)
        {
            t->linecount = compute_8_bytes_bmi2(sdata, t->linecount, t->ddata.primes, i);
        }
    }
    else
    {
        for (i = t->startid; i < t->stopid; i += 8)
        {
            t->linecount = compute_8_bytes(sdata, t->linecount, t->ddata.primes, i);
        }
    }
#else
    for (i = t->startid; i < t->stopid; i += 8)
    {
        t->linecount = compute_8_bytes(sdata, t->linecount, t->ddata.primes, i);
    }
#endif

    return;
}


uint64_t primes_from_lineflags(soe_staticdata_t *sdata, thread_soedata_t *thread_data,
	uint32_t start_count, uint64_t *primes)
{
	//compute primes using all of the sieved lines we have stored
	uint32_t pcount = start_count;	
	uint64_t i;
	int j;
	uint32_t range, lastid;
    int GLOBAL_OFFSET = sdata->GLOBAL_OFFSET;

    //timing
    double t;
    struct timeval tstart, tstop;

    // threading structures
    tpool_t *tpool_data;
    soe_userdata_t udata;

    if (sdata->VFLAG > 1)
    {
        gettimeofday(&tstart, NULL);
    }

	// each thread needs to work on a number of bytes that is divisible by 32
	range = sdata->numlinebytes / sdata->THREADS;
	range -= (range % 32);
	lastid = 0;

    // divvy up the line bytes
    for (i = 0; i < sdata->THREADS; i++)
    {
        thread_soedata_t *t = thread_data + i;

        t->sdata = *sdata;
        t->startid = lastid;
        t->stopid = t->startid + range;
        lastid = t->stopid;

        if (sdata->VFLAG > 2)
        {
            printf("thread %d finding primes from byte offset %u to %u\n",
                (int)i, t->startid, t->stopid);
        }
    }

    // allocate a temporary array for each thread's primes
    if (sdata->THREADS > 1)
    {
        uint64_t memchunk;

        if (sdata->sieve_range)
        {
            // then just split the overall range into equal parts
            memchunk = (sdata->orig_hlimit - sdata->orig_llimit) / sdata->THREADS + sdata->THREADS;

            for (i = 0; i < sdata->THREADS; i++)
            {
                thread_soedata_t *t = thread_data + i;

                t->ddata.primes = (uint64_t *)malloc(memchunk * sizeof(uint64_t));
            }
        }
        else
        {
            // then estimate the number of primes we'll find in each chunk.
            // it's important to do this chunk by chunk, because in some cases
            // the number of primes changes rapidly as a function of offset
            // from the start of the range (i.e., when start is 0)
            uint64_t hi_est, lo_est;
            uint64_t tmplo = sdata->orig_llimit;
            uint64_t tmphi;
            uint64_t chunk = 8 * sdata->numlinebytes / sdata->THREADS;

            chunk *= sdata->prodN;
            tmphi = tmplo + chunk;
            for (i = 0; i < sdata->THREADS; i++)
            {
                thread_soedata_t *t = thread_data + i;

                hi_est = (uint64_t)(tmphi / log((double)tmphi));
                if (tmplo > 1)
                    lo_est = (uint64_t)(tmplo / log((double)tmplo));
                else
                    lo_est = 0;

                memchunk = (uint64_t)((double)(hi_est - lo_est) * 1.25);

                if (sdata->VFLAG > 2)
                {
                    printf("allocating temporary space for %" PRIu64 " primes between %" PRIu64 " and %" PRIu64 "\n",
                        memchunk, tmplo, tmphi);
                }

                t->ddata.primes = (uint64_t *)malloc(memchunk * sizeof(uint64_t));

                tmplo += chunk;
                tmphi += chunk;
            }
        }
    }
    else
    {
        // with just one thread, don't bother with creating a temporary array
        thread_data[0].ddata.primes = primes;
    }

    udata.sdata = sdata;
    udata.ddata = thread_data;
    tpool_data = tpool_setup(sdata->THREADS, NULL, NULL, NULL,
        &compute_primes_dispatch, &udata);

    if (sdata->THREADS == 1)
    {
        thread_data->linecount = pcount;
        compute_primes_work_fcn(tpool_data);
    }
    else
    {
        sdata->sync_count = 0;
        tpool_add_work_fcn(tpool_data, &compute_primes_work_fcn);
        tpool_go(tpool_data);
    }
    free(tpool_data);    

	// now combine all of the temporary arrays, if necessary
	if (sdata->THREADS > 1)
	{
		pcount = start_count;
		for (j = 0; j < sdata->THREADS; j++)
		{
			thread_soedata_t *t = thread_data + j;

			if (t->linecount == 0)
			{
				free(t->ddata.primes);
				continue;
			}

            if (sdata->VFLAG > 2)
            {
                printf("adding %" PRIu64 " primes found in thread %d\n", t->linecount, j);
            }

			memcpy(primes + GLOBAL_OFFSET + pcount, t->ddata.primes, t->linecount * sizeof(uint64_t));

			pcount += t->linecount;
			free(t->ddata.primes);
		}
	}
    else
    {
        pcount = thread_data[0].linecount;
    }

	// and finally, get primes from any residual portion of the line arrays
	// using a direct method
	if (lastid != sdata->numlinebytes)
	{
        if (sdata->VFLAG > 2)
        {
            printf("adding primes from byte offset %u to %u\n",
                lastid, (uint32_t)sdata->numlinebytes);
        }

		for (i = lastid; i < sdata->numlinebytes; i+=8)
		{
			pcount = compute_8_bytes(sdata, pcount, primes, i);		
		}
	}

    if (sdata->VFLAG > 1)
    {
        gettimeofday(&tstop, NULL);

        t = ytools_difftime(&tstart, &tstop);

        if (sdata->VFLAG > 2)
        {
            printf("time to compute primes = %1.4f\n", t);
        }
    }

	return pcount;
}

uint32_t compute_8_bytes(soe_staticdata_t *sdata, 
	uint32_t pcount, uint64_t *primes, uint64_t byte_offset)
{
	uint32_t current_line;
	// re-ordering queues supporting up to 48 residue classes.
    uint64_t pqueues[64][48];
    uint8_t pcounts[64];
    int i, j;
	uint32_t nc = sdata->numclasses;
	uint64_t lowlimit = sdata->lowlimit;
	uint64_t prodN = sdata->prodN;
	uint8_t **lines = sdata->lines;
	uint64_t olow = sdata->orig_llimit;
	uint64_t ohigh = sdata->orig_hlimit;
    int GLOBAL_OFFSET = sdata->GLOBAL_OFFSET;
		
	if ((byte_offset & 32767) == 0)
	{
		if (sdata->VFLAG > 1)
		{
			printf("computing: %d%%\r",(int)
				((double)byte_offset / (double)(sdata->numlinebytes) * 100.0));
			fflush(stdout);
		}
	}

    // Compute the primes using ctz on the 64-bit words but push the results
    // into 64 different queues depending on the bit position.  Then
    // we pull from the queues in order while storing into the primes array.
    // This time the bottleneck is mostly in the queue-based sorting
    // and associated memory operations, so we don't bother with
    // switching between branch-free inner loops or not.
    memset(pcounts, 0, 64);

    lowlimit += byte_offset * 8 * prodN;
    for (current_line = 0; current_line < nc; current_line++)
    {
        uint64_t *line64 = (uint64_t *)lines[current_line];
        uint64_t flags64 = line64[byte_offset / 8];

        while (flags64 > 0)
        {
            uint64_t pos = _trail_zcnt64(flags64);
            uint64_t prime = lowlimit + pos * prodN + sdata->rclass[current_line];

            if ((prime >= olow) && (prime <= ohigh))
            {
                pqueues[pos][pcounts[pos]] = prime;
                pcounts[pos]++;
            }
            flags64 ^= (1ULL << pos);
        }
    }

    for (i = 0; i < 64; i++)
    {
        for (j = 0; j < pcounts[i] / 2; j++)
        {
            __m128i t = _mm_loadu_si128((__m128i *)(&pqueues[i][j * 2]));
            _mm_storeu_si128((__m128i *)(&primes[GLOBAL_OFFSET + pcount]), t);
            pcount += 2;
        }
        for (j *= 2; j < pcounts[i]; j++)
        {
            primes[GLOBAL_OFFSET + pcount++] = pqueues[i][j];
        }
    }

	return pcount;
}


#if defined(USE_BMI2) || defined(USE_AVX512F)

__inline uint64_t interleave_avx2_bmi2_pdep2x32(uint32_t x1, uint32_t x2)
{
    return _pdep_u64(x1, 0x5555555555555555) 
        | _pdep_u64(x2, 0xaaaaaaaaaaaaaaaa);
}

__inline uint64_t interleave_avx2_bmi2_pdep(uint8_t x1,
    uint8_t x2,
    uint8_t x3,
    uint8_t x4,
    uint8_t x5,
    uint8_t x6,
    uint8_t x7,
    uint8_t x8)
{
    return _pdep_u64(x1, 0x0101010101010101ull) |
        _pdep_u64(x2, 0x0202020202020202ull) |
        _pdep_u64(x3, 0x0404040404040404ull) |
        _pdep_u64(x4, 0x0808080808080808ull) |
        _pdep_u64(x5, 0x1010101010101010ull) |
        _pdep_u64(x6, 0x2020202020202020ull) |
        _pdep_u64(x7, 0x4040404040404040ull) |
        _pdep_u64(x8, 0x8080808080808080ull);
}

uint32_t compute_8_bytes_bmi2(soe_staticdata_t *sdata,
    uint32_t pcount, uint64_t *primes, uint64_t byte_offset)
{    
    uint32_t nc = sdata->numclasses;
    uint64_t lowlimit = sdata->lowlimit;
    uint8_t **lines = sdata->lines;
    uint64_t olow = sdata->orig_llimit;
    uint64_t ohigh = sdata->orig_hlimit;
    int GLOBAL_OFFSET = sdata->GLOBAL_OFFSET;

    if ((byte_offset & 32767) == 0)
    {
        if (sdata->VFLAG > 1)
        {
            printf("computing: %d%%\r", (int)
                ((double)byte_offset / (double)(sdata->numlinebytes) * 100.0));
            fflush(stdout);
        }
    }

    // AVX2 version, new instructions help quite a bit:
    // use _pdep_u64 to align/interleave bits from multiple bytes, 
    // _blsr_u64 to clear the last set bit, and depending on the 
    // number of residue classes, AVX2 vector load/store operations.

    // here is the 2 line version
    if (nc == 2)
    {
        int i;
        uint64_t plow, phigh;
        uint32_t *lines32a = (uint32_t *)lines[0];
        uint32_t *lines32b = (uint32_t *)lines[1];

        // compute the minimum/maximum prime we could encounter in this range
        // and execute either a branch-free innermost loop or not.
        plow = (byte_offset + 0) * 8 * sdata->prodN + 0 * sdata->prodN + 
            sdata->rclass[0] + lowlimit;
        phigh = (byte_offset + 7) * 8 * sdata->prodN + 7 * sdata->prodN + 
            sdata->rclass[sdata->numclasses-1] + lowlimit;

        // align the current bytes in all residue classes
        if ((plow < olow) || (phigh > ohigh))
        {                
            // align the current bytes in next 2 residue classes
            lowlimit += byte_offset * 8 * sdata->prodN;
            for (i = 0; i < 2; i++)
            {
                uint64_t aligned_flags;

                aligned_flags = interleave_avx2_bmi2_pdep2x32(
                    lines32a[byte_offset/4+i],
                    lines32b[byte_offset/4+i]);

                while (aligned_flags > 0)
                {
                    uint64_t pos = _trail_zcnt64(aligned_flags);
                    uint64_t prime = lowlimit + (pos / 2) * 6 + sdata->rclass[pos % 2];

                    if ((prime >= olow) && (prime <= ohigh))
                        primes[GLOBAL_OFFSET + pcount++] = prime;

                    aligned_flags = _reset_lsb64(aligned_flags);
                }
                lowlimit += 32 * sdata->prodN;
            }
        }
        else
        {
            // align the current bytes in next 2 residue classes
            lowlimit += byte_offset * 8 * sdata->prodN;
            for (i = 0; i < 2; i++)
            {
                uint64_t aligned_flags;

                aligned_flags = interleave_avx2_bmi2_pdep2x32(
                    lines32a[byte_offset/4+i],
                    lines32b[byte_offset/4+i]);

                // then compute primes in order for flags that are set.
                while (aligned_flags > 0)
                {
                    uint64_t pos = _trail_zcnt64(aligned_flags);
                    uint64_t prime = lowlimit + (pos / 2) * 6 + sdata->rclass[pos % 2];

                    primes[GLOBAL_OFFSET + pcount++] = prime;
                    aligned_flags = _reset_lsb64(aligned_flags);
                }
                lowlimit += 32 * sdata->prodN;
            }
        }
    }
    else if (nc == 8)
    {
        int i;
        uint64_t plow, phigh;

        // compute the minimum/maximum prime we could encounter in this range
        // and execute either a branch-free innermost loop or not.
        plow = (byte_offset + 0) * 8 * sdata->prodN + 0 * sdata->prodN + 
            sdata->rclass[0] + lowlimit;
        phigh = (byte_offset + 7) * 8 * sdata->prodN + 7 * sdata->prodN + 
            sdata->rclass[sdata->numclasses-1] + lowlimit;

        // align the current bytes in all 8 residue classes
        if ((plow < olow) || (phigh > ohigh))
        {                
            // align the current bytes in next 8 residue classes
            lowlimit += byte_offset * 8 * sdata->prodN;
            for (i = 0; i < 8; i++)
            {
                uint64_t aligned_flags;

                aligned_flags = interleave_avx2_bmi2_pdep(lines[0][byte_offset+i],
                    lines[1][byte_offset+i],
                    lines[2][byte_offset+i],
                    lines[3][byte_offset+i],
                    lines[4][byte_offset+i],
                    lines[5][byte_offset+i],
                    lines[6][byte_offset+i],
                    lines[7][byte_offset+i]);

                while (aligned_flags > 0)
                {
                    uint64_t pos = _trail_zcnt64(aligned_flags);
                    uint64_t prime = lowlimit + (pos / 8) * 30 + sdata->rclass[pos % 8];

                    if ((prime >= olow) && (prime <= ohigh))
                        primes[GLOBAL_OFFSET + pcount++] = prime;

                    aligned_flags = _reset_lsb64(aligned_flags);
                }
                lowlimit += 8 * sdata->prodN;
            }
        }
        else
        {
            // align the current bytes in next 8 residue classes
            lowlimit += byte_offset * 8 * sdata->prodN;
            for (i = 0; i < 8; i++)
            {
                uint64_t aligned_flags;

                aligned_flags = interleave_avx2_bmi2_pdep(lines[0][byte_offset+i],
                    lines[1][byte_offset+i],
                    lines[2][byte_offset+i],
                    lines[3][byte_offset+i],
                    lines[4][byte_offset+i],
                    lines[5][byte_offset+i],
                    lines[6][byte_offset+i],
                    lines[7][byte_offset+i]);

                // then compute primes in order for flags that are set.
                while (aligned_flags > 0)
                {
                    uint64_t pos = _trail_zcnt64(aligned_flags);
                    uint64_t prime = lowlimit + (pos / 8) * 30 + sdata->rclass[pos % 8];

                    primes[GLOBAL_OFFSET + pcount++] = prime;
                    aligned_flags = _reset_lsb64(aligned_flags);
                }
                lowlimit += 8 * sdata->prodN;
            }
        }

    }
    else
    {
        // ordering the bits becomes inefficient with 48 lines because
        // they would need to be dispersed over too great a distance.
        // instead we compute the primes as before but push the results
        // into 64 different queues depending on the bit position.  Then
        // we pull from the queues in order while storing into the primes array.
        // This time the bottleneck is mostly in the queue-based sorting
        // and associated memory operations, so we don't bother with
        // switching between branch-free inner loops or not.
        uint64_t pqueues[64][48];
        uint8_t pcounts[64];
        int i,j;
        uint32_t current_line;

        memset(pcounts, 0, 64);
           
        lowlimit += byte_offset * 8 * 210;
        for (current_line = 0; current_line < nc; current_line++)
        {
            uint64_t *line64 = (uint64_t *)lines[current_line];
            uint64_t flags64 = line64[byte_offset/8];

            while (flags64 > 0)
            {
                uint64_t pos = _trail_zcnt64(flags64);
                uint64_t prime = lowlimit + pos * 210 + sdata->rclass[current_line];

                if ((prime >= olow) && (prime <= ohigh))
                {
                    pqueues[pos][pcounts[pos]] = prime;
                    pcounts[pos]++;
                }
                flags64 = _reset_lsb64(flags64);
            }
        }

        for (i = 0; i < 64; i++)
        {
            for (j = 0; j < pcounts[i] / 4; j++)
            {
                __m256i t = _mm256_loadu_si256((__m256i *)(&pqueues[i][j*4]));
                _mm256_storeu_si256((__m256i *)(&primes[GLOBAL_OFFSET + pcount]), t);
                pcount += 4;
            }
            for (j *= 4; j < pcounts[i]; j++)
            {
                primes[GLOBAL_OFFSET + pcount++] = pqueues[i][j];
            }
        }
    }

    return pcount;
}

#endif

