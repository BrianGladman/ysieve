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

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "soe.h"
#include "ytools.h"
#include "threadpool.h"
#include "soe_impl.h"

#define USE_NEW_ROOTCALC


static __inline uint32_t redc_loc(uint64_t x, uint32_t pinv, uint32_t p)
{
    uint32_t m = (uint32_t)x * pinv;
    x += (uint64_t)m * (uint64_t)p;
    m = x >> 32;
    if (m >= p) m -= p;
    return m;
}

static __inline uint32_t to_monty_loc(uint32_t x, uint32_t r2, uint32_t pinv, uint32_t p)
{
    uint64_t t = (uint64_t)x * (uint64_t)r2;
    return redc_loc(t, pinv, p);
}


void compute_roots_dispatch(void *vptr)
{
    tpool_t *tdata = (tpool_t *)vptr;
    soe_userdata_t *t = (soe_userdata_t *)tdata->user_data;
    soe_staticdata_t *sdata = t->sdata;

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



//printf("-c^-1 mod prodN: ");
//for (i = 0; i < sdata->numclasses; i++)
//{
//    class_inv[i] = pn - modinv_1(sdata->rclass[i], pn);
//    printf("%u, ", class_inv[i]);
//}
//printf("\n");

static uint32_t class_inv6[8] = { 5, 1 };
static uint32_t class_inv30[8] = { 29, 17, 19, 23, 7, 11, 13, 1 };
static uint32_t class_inv210[48] = {
    209, 19, 113, 37, 11,
    73, 181, 149, 17, 169,
    83, 67, 103, 121, 179,
    47, 139, 23, 101, 43,
    151, 197, 79, 53, 157,
    131, 13, 59, 167, 109,
    187, 71, 163, 31, 89,
    107, 143, 127, 41, 193,
    61, 29, 137, 199, 173,
    97, 191, 1 };
static uint32_t class_inv2310[480] = {
    2309, 533, 1087, 851, 703, 2071, 149, 437,
    169, 1343, 1327, 523, 1801, 1439, 1517, 1399,
    443, 731, 2143, 571, 1667, 709, 2153, 367,
    551, 1063, 1637, 529, 607, 1961, 31, 719, 2207,
    1403, 2227, 41, 2083, 271, 1289, 1669, 383,
    727, 1451, 1259, 439, 953, 2137, 1271, 1963,
    2281, 1409, 1487, 589, 1537, 1783, 541, 179,
    467, 559, 653, 1151, 883, 1831, 617, 1129,
    893, 787, 1273, 1319, 377, 2077, 2171, 373,
    1081, 1139, 107, 353, 337, 1091, 193, 481,
    449, 1187, 409, 1433, 401, 1891, 1679, 1699,
    1163, 37, 221, 73, 391, 647, 1849, 923, 277,
    2011, 389, 887, 1609, 1073, 1361, 1093, 2251,
    1339, 53, 2047, 131, 13, 269, 587, 109, 817,
    281, 1633, 1291, 89, 1157, 547, 251, 1453,
    1531, 1499, 137, 1879, 1643, 307, 821, 841,
    1889, 2119, 2213, 457, 283, 1651, 989, 1697,
    2183, 1747, 1363, 751, 809, 1097, 1819, 23,
    101, 1513, 151, 197, 79, 1103, 1391, 1483,
    689, 1007, 1369, 1027, 1541, 2053, 1711,
    317, 2243, 1597, 1301, 691, 1709, 557, 619,
    2063, 1567, 1031, 1261, 629, 19, 1373, 1717,
    2111, 1333, 2249, 1907, 1429, 293, 487, 313,
    961, 2069, 2147, 2239, 1571, 463, 1201, 1457,
    1549, 1313, 1627, 1811, 223, 899, 2267, 1789,
    1447, 2263, 661, 1979, 1367, 1193, 757, 1511,
    1033, 1741, 2129, 767, 829, 1013, 1987, 2081,
    1471, 2099, 1069, 113, 1061, 493, 1231, 359,
    17, 379, 503, 697, 733, 1381, 1307, 1189,
    1913, 311, 361, 1877, 289, 1733, 577, 2021,
    433, 1949, 1999, 397, 1121, 1003, 929, 1577,
    1613, 1807, 1931, 2293, 1951, 1079, 1817,
    1249, 2197, 1241, 211, 839, 229, 323, 1297,
    1481, 1543, 181, 569, 1277, 799, 1553, 1117,
    943, 331, 1649, 47, 863, 521, 43, 1411, 2087,
    499, 683, 997, 761, 853, 1109, 1847, 739,
    71, 163, 241, 1349, 1997, 1823, 2017, 881,
    403, 61, 977, 199, 593, 937, 2291, 1681,
    1049, 1279, 743, 247, 1691, 1753, 601, 1619,
    1009, 713, 67, 1993, 599, 257, 769, 1283,
    941, 1303, 1621, 827, 919, 1207, 2231, 2113,
    2159, 797, 2209, 2287, 491, 1213, 1501, 1559,
    947, 563, 127, 613, 1321, 659, 2027, 1853,
    97, 191, 421, 1469, 1489, 2003, 667, 431,
    2173, 811, 779, 857, 2059, 1763, 1153, 2221,
    1019, 677, 2029, 1493, 2201, 1723, 2041,
    2297, 2179, 263, 2257, 971, 59, 1217, 949,
    1237, 701, 1423, 1921, 299, 2033, 1387,
    461, 1663, 1919, 2237, 2089, 2273, 1147,
    611, 631, 419, 1909, 877, 1901, 1123, 1861,
    1829, 2117, 1219, 1973, 1957, 2203, 1171,
    1229, 1937, 139, 233, 1933, 991, 1037, 1523,
    1417, 1181, 1693, 479, 1427, 1159, 1657,
    1751, 1843, 2131, 1769, 527, 773, 1721, 823,
    901, 29, 347, 1039, 173, 1357, 1871, 1051,
    859, 1583, 1927, 641, 1021, 2039, 227, 2269,
    83, 907, 103, 1591, 2279, 349, 1703, 1781,
    673, 1247, 1759, 1943, 157, 1601, 643, 1739,
    167, 1579, 1867, 911, 793, 871, 509, 1787,
    983, 967, 2141, 1873, 2161, 239, 1607, 1459,
    1223, 1777, 1 };


uint32_t div_by_6(uint32_t in)
{
    return in / 6;
}

uint32_t div_by_30(uint32_t in)
{
    return in / 30;
}

uint32_t div_by_210(uint32_t in)
{
    return in / 210;
}

uint32_t div_by_2310(uint32_t in)
{
    return in / 2310;
}

uint32_t mod_by_6(uint32_t in)
{
    return in % 6;
}

uint32_t mod_by_30(uint32_t in)
{
    return in % 30;
}

uint32_t mod_by_210(uint32_t in)
{
    return in % 210;
}

uint32_t mod_by_2310(uint32_t in)
{
    return in % 2310;
}

uint32_t(*mod_fcn)(uint32_t);
uint32_t(*div_fcn)(uint32_t);



void compute_roots_work_fcn(void *vptr)
{
    tpool_t *tdata = (tpool_t *)vptr;
    soe_userdata_t *udata = (soe_userdata_t *)tdata->user_data;
    soe_staticdata_t *sdata = udata->sdata;
    thread_soedata_t *t = &udata->ddata[tdata->tindex];
    uint32_t *last_root;
    uint32_t *last_p;
    uint32_t *class_inv;
    int *res_table;
    int i;
    uint32_t pn = sdata->prodN;    
    uint32_t start_res;
    uint32_t pdiff;
    uint32_t last_prime;
    

    if (sdata->VFLAG > 2)
    {
        printf("starting root computation over %u to %u\n", t->startid, t->stopid);
    }

    res_table = malloc(sdata->prodN * sizeof(int));
    memset(res_table, -1, sizeof(int));
    for (i = 0; i < sdata->numclasses; i++)
        res_table[sdata->rclass[i]] = i;

    last_root = (uint32_t *)calloc(sdata->numclasses, sizeof(uint32_t));
    last_p = (uint32_t *)calloc(sdata->numclasses, sizeof(uint32_t));

    if (t->sdata.prodN == 6)
    {
        div_fcn = &div_by_6;
        mod_fcn = &mod_by_6;
        class_inv = class_inv6;
    }
    else if (t->sdata.prodN == 30)
    {
        div_fcn = &div_by_30;
        mod_fcn = &mod_by_30;
        class_inv = class_inv30;
    }
    else if (t->sdata.prodN == 210)
    {
        div_fcn = &div_by_210;
        mod_fcn = &mod_by_210;
        class_inv = class_inv210;
    }
    else if (t->sdata.prodN == 2310)
    {
        div_fcn = &div_by_2310;
        mod_fcn = &mod_by_2310;
        class_inv = class_inv2310;
    }
    else
    {
        printf("invalid num_classes\n");
        exit(1);
    }

    start_res = sdata->sieve_p[t->startid] % pn;
    last_prime = sdata->sieve_p[t->startid];

    if (t->sdata.sieve_range == 0)
    {

#if defined(USE_AVX2) || defined(_WIN64)
        //int j = 0;

        if (t->sdata.use_monty)
        {            
            for (i = t->startid;  i < t->stopid; i++)
            {
                uint32_t inv;
                uint32_t prime = t->sdata.sieve_p[i];
                uint32_t x;
#ifdef USE_NEW_ROOTCALC
                uint32_t classnum;

                pdiff = prime - last_prime;
                // mul-by-inverse division.
                classnum = mod_fcn(start_res + pdiff);
                start_res = classnum;
                classnum = res_table[classnum];

                last_prime = prime;

                if (last_root[classnum] == 0)
                {
                    // start the process with the first prime in this class
                    inv = modinv3(pn, prime);
                    t->sdata.root[i] = prime - inv;
                    last_root[classnum] = inv;
                    last_p[classnum] = prime;

                    //if ((i - t->startid) < 100)
                    //    printf("first prime for class %d = %u, inv = %u, root = %u\n",
                    //    start_res, prime, inv, t->sdata.root[i]);
                }
                else
                {
                    // compute the root using the last prime in this class
                    inv = last_root[classnum];
                    x = div_fcn(prime - last_p[classnum]);
                    last_root[classnum] = inv + x * class_inv[classnum];
                    t->sdata.root[i] = prime - last_root[classnum];

                    //if ((i - t->startid) < 100)
                    //    printf("last root,prime for class %d = %u,%u, this prime = %u, "
                    //    "class inv = %u, this root = %u\n",
                    //    start_res, inv, last_p[classnum], prime, 
                    //    class_inv[classnum], t->sdata.root[i]);

                    last_p[classnum] = prime;                   
                }

#else
                // slightly optimized modinv when prime >> prodN
                inv = modinv3(t->sdata.prodN, prime);
                t->sdata.root[i] = prime - inv;
#endif

                t->sdata.lower_mod_prime[i] =
                    (t->sdata.lowlimit + 1) % prime;

                //if ((i - t->startid) < 100)
                //    printf("lower_mod_p = %u\n", t->sdata.lower_mod_prime[i]);

                x = (((prime + 2) & 4) << 1) + prime; // here x*a==1 mod 2**4
                x *= 2 - prime * x;               // here x*a==1 mod 2**8
                x *= 2 - prime * x;               // here x*a==1 mod 2**16
                x *= 2 - prime * x;               // here x*a==1 mod 2**32
                // rho = -1/m mod b
                t->sdata.pinv[i] = (uint32_t)0 - (uint32_t)x;
                t->sdata.r2modp[i] = (uint32_t)(0x100000000ULL % (uint64_t)prime);
                t->sdata.r2modp[i] =
                    ((uint64_t)t->sdata.r2modp[i] * (uint64_t)t->sdata.r2modp[i]) % (uint64_t)prime;

                // root into monty
                t->sdata.root[i] = to_monty_loc(t->sdata.root[i], t->sdata.r2modp[i],
                    t->sdata.pinv[i], prime);
            }            
            
        }
        else
        {
            for (i = t->startid;  i < t->stopid; i++)
            {
                uint32_t inv;
                uint32_t prime = t->sdata.sieve_p[i];
#ifdef USE_NEW_ROOTCALC
                uint32_t x;
                uint32_t classnum;

                // when we are not using Monty, numclasses = 6;  It helps
                // a lot to hardcode the divisor as the compiler will
                // replace with multiplication by a constant inverse.
                pdiff = prime - last_prime;
                classnum = mod_fcn(start_res + pdiff);
                start_res = classnum;
                classnum = res_table[classnum];

                last_prime = prime;

                if (last_root[classnum] == 0)
                {
                    // start the process with the first prime in this class
                    inv = modinv3(pn, prime);
                    t->sdata.root[i] = prime - inv;
                    last_root[classnum] = inv;
                    last_p[classnum] = prime;
                }
                else
                {
                    // compute the root using the last prime in this class
                    inv = last_root[classnum];
                    x = div_fcn(prime - last_p[classnum]);
                    last_root[classnum] = inv + x * class_inv[classnum];
                    t->sdata.root[i] = prime - last_root[classnum];
                    last_p[classnum] = prime;
                }

#else
                // slightly optimized modinv when prime >> prodN
                inv = modinv3(t->sdata.prodN, prime);
                t->sdata.root[i] = prime - inv;
#endif

                t->sdata.lower_mod_prime[i] =
                    (t->sdata.lowlimit + 1) % prime;
            }
        }

#else

        // in this new approach, we no longer need to compute modinv(pn, prime)
        // for every prime.  Instead, we only compute it for the first prime
        // in each residue class; subsequent primes can compute their root
        // using the result from the previous prime in the same class along
        // with a few precomputed constants and a small division (likely 
        // accelerated by compiler multiply-by-inverse tricks).
        for (i = t->startid; i < t->stopid; i++)
        {
            uint32_t inv;            
            uint32_t prime = t->sdata.sieve_p[i];
#ifdef USE_NEW_ROOTCALC
            uint32_t x;
            uint32_t classnum;

            // It helps to hardcode the divisor as the compiler will
            // replace with multiplication by a constant inverse.
            pdiff = prime - last_prime;
            classnum = mod_fcn(start_res + pdiff);
            start_res = classnum;
            classnum = res_table[classnum];

            last_prime = prime;

            if (last_root[classnum] == 0)
            {
                // start the process with the first prime in this class
                // by using modinv.
                inv = modinv3(pn, prime);
                t->sdata.root[i] = prime - inv;
                last_root[classnum] = inv;
                last_p[classnum] = prime;
            }
            else
            {
                // compute the root using the last prime in this class.
                // if the root of the previous prime in this class is R,
                // and the difference between this prime and last is dP, 
                // then the root for this prime is P - (R + dP / PW * cn^-1),
                // where PW is the product of wheel primes and cn^-1 is the
                // inverse of this classnum mod PW.
                inv = last_root[classnum];
                x = div_fcn(prime - last_p[classnum]);
                last_root[classnum] = inv + x * class_inv[classnum];
                t->sdata.root[i] = prime - last_root[classnum];
                last_p[classnum] = prime;
            }
#else

            // slightly optimized modinv when prime >> prodN
            inv = modinv3(t->sdata.prodN, prime);
            t->sdata.root[i] = prime - inv;
#endif

            t->sdata.lower_mod_prime[i] =
                (t->sdata.lowlimit + 1) % prime;
        }

#endif
    }
    else
    {
        mpz_t tmpz;
        mpz_init(tmpz);

        mpz_add_ui(tmpz, *t->sdata.offset, t->sdata.lowlimit + 1);
        for (i = t->startid; i < t->stopid; i++)
        {
            uint32_t inv;
            uint32_t prime = t->sdata.sieve_p[i];

            // slightly optimized modinv when prime >> prodN
            inv = modinv3(t->sdata.prodN, prime);
            t->sdata.root[i] = prime - inv;

            t->sdata.lower_mod_prime[i] =
                mpz_tdiv_ui(tmpz, prime);
        }
    }

    free(last_root);
    free(last_p );
    free(res_table);

    return;
}


void getRoots(soe_staticdata_t *sdata, thread_soedata_t *thread_data)
{
    int prime, prodN;
    uint64_t startprime;
    uint64_t lblk_b, ublk_b, blk_b_sqrt;
    uint64_t i;
    int j;
    uint32_t range, lastid;

    // timing
    double t;
    struct timeval tstart, tstop;

    // threading structures
    tpool_t *tpool_data;
    soe_userdata_t udata;

    prodN = (int)sdata->prodN;
    startprime = sdata->startprime;
    
    if (sdata->VFLAG > 1)
    {
        gettimeofday(&tstart, NULL);
    }
    
    lblk_b = sdata->lowlimit;
    ublk_b = sdata->blk_r + lblk_b - sdata->prodN;
    blk_b_sqrt = (uint32_t)(sqrt(ublk_b + sdata->prodN)) + 1;
    
    for (i = startprime; i < sdata->bucket_start_id; i++)
    {
        uint32_t inv;
        prime = sdata->sieve_p[i];

        // sieving requires that we find the offset of each sieve prime in each block 
        // that we sieve.  We are more restricted in choice of offset because we
        // sieve residue classes.  A good way to find the offset is the extended 
        // euclidean algorithm, which reads ax + by = gcd(a,b),
        // where a = prime, b = prodN, and therefore gcd = 1.  
        // since a and b are coprime, y is the multiplicative inverse of prodN modulo prime.
        // This value is a constant, so compute it here in order to facilitate 
        // finding offsets later.

        if (sdata->sieve_p[i] > blk_b_sqrt)
        {
            lblk_b = ublk_b + prodN;
            ublk_b += sdata->blk_r;
            blk_b_sqrt = (uint64_t)(sqrt((int64_t)(ublk_b + prodN))) + 1;
        }

        //solve prodN ^ -1 % p 
        inv = modinv1(prodN, prime);
        sdata->root[i] = prime - inv;
    }

    if (sdata->VFLAG > 1)
    {
        gettimeofday(&tstop, NULL);
        t = ytools_difftime(&tstart, &tstop);

        if (sdata->VFLAG > 2)
        {
            printf("time to compute linear sieve roots = %1.6f\n", t);
        }

        gettimeofday(&tstart, NULL);
    }

    range = (sdata->bitmap_start_id - sdata->bucket_start_id) / sdata->THREADS;
    //range = (sdata->pboundi - sdata->bucket_start_id) / sdata->THREADS;
    lastid = sdata->bucket_start_id;

    if (range > 0)
    {
        // divvy up the primes left to compute
        for (j = 0; j < sdata->THREADS; j++)
        {
            thread_soedata_t *t = thread_data + j;

            t->sdata = *sdata;
            t->startid = lastid;
            t->stopid = t->startid + range;
            lastid = t->stopid;

            if (sdata->VFLAG > 2)
            {
                printf("bucket start id = %u, bitmap start id = %u\n",
                    sdata->bucket_start_id, sdata->bitmap_start_id);
                printf("assiging thread %d root computation over %u to %u\n",
                    j, t->startid, t->stopid); fflush(stdout);
            }
        }

        // the last one gets any leftover
        if (thread_data[sdata->THREADS - 1].stopid != sdata->bitmap_start_id)
        {
            thread_data[sdata->THREADS - 1].stopid = sdata->bitmap_start_id;
            //thread_data[sdata->THREADS - 1].stopid = sdata->pboundi;
        }

        udata.sdata = sdata;
        udata.ddata = thread_data;
        tpool_data = tpool_setup(sdata->THREADS, NULL, NULL, NULL,
            &compute_roots_dispatch, &udata);

        if (sdata->THREADS == 1)
        {
            compute_roots_work_fcn(tpool_data);
        }
        else
        {
            sdata->sync_count = 0;
            tpool_add_work_fcn(tpool_data, &compute_roots_work_fcn);
            tpool_go(tpool_data);
        }
        free(tpool_data);
    }

    if (sdata->VFLAG > 1)
    {
        gettimeofday(&tstop, NULL);

        t = ytools_difftime(&tstart, &tstop);

        if (sdata->VFLAG > 2)
        {
            printf("time to compute bucket sieve roots = %1.6f\n", t);
        }
    }

	return;
}


