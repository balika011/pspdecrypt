#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>

extern "C" {
#include "libkirk/kirk_engine.h"
#include "kl4e.h"
#include "libLZR.h"
}
#include "pspdecrypt_lib.h"
#include "PrxDecrypter.h"

////////// SignCheck //////////

u8 check_keys0[0x10] =
{
	0x71, 0xF6, 0xA8, 0x31, 0x1E, 0xE0, 0xFF, 0x1E,
	0x50, 0xBA, 0x6C, 0xD2, 0x98, 0x2D, 0xD6, 0x2D
};

u8 check_keys1[0x10] =
{
	0xAA, 0x85, 0x4D, 0xB0, 0xFF, 0xCA, 0x47, 0xEB,
	0x38, 0x7F, 0xD7, 0xE4, 0x3D, 0x62, 0xB0, 0x10
};

static int Encrypt(u32 *buf, int size)
{
	buf[0] = 4;
	buf[1] = buf[2] = 0;
	buf[3] = 0x100;
	buf[4] = size;

	/* Note: this encryption returns different data in each psp,
	   But it always returns the same in a specific psp (even if it has two nands) */
	if (sceUtilsBufferCopyWithRange((u8*)buf, size+0x14, (u8*)buf, size+0x14, 5) != 0)
		return -1;

	return 0;
}

int pspSignCheck(u8 *buf)
{
	u8 enc[0xD0+0x14];
	int iXOR, res;

	memcpy(enc+0x14, buf+0x110, 0x40);
	memcpy(enc+0x14+0x40, buf+0x80, 0x90);
	
	for (iXOR = 0; iXOR < 0xD0; iXOR++)
	{
		enc[0x14+iXOR] ^= check_keys0[iXOR&0xF];
	}

	if ((res = Encrypt((u32 *)enc, 0xD0)) != 0)
	{
		printf("Encrypt failed.\n");
		return -1;
	}

	for (iXOR = 0; iXOR < 0xD0; iXOR++)
	{
		enc[0x14+iXOR] ^= check_keys1[iXOR&0xF];
	}

	memcpy(buf+0x80, enc+0x14, 0xD0);
	
	return 0;
}

int pspIsSignChecked(u8 *buf)
{
	int i, res = 0;

	for (i = 0; i < 0x58; i++)
	{
		if (buf[0xD4+i] != 0)
		{
			res = 1;
			break;
		}
	}

	return res;
}

////////// UnsignCheck //////////

static int Decrypt(u32 *buf, int size)
{
	buf[0] = 5;
	buf[1] = buf[2] = 0;
	buf[3] = 0x100;
	buf[4] = size;

	if (sceUtilsBufferCopyWithRange((u8*)buf, size+0x14, (u8*)buf, size+0x14, 8) != 0)
		return -1;
	
	return 0;
}

int pspUnsignCheck(u8 *buf)
{
	u8 enc[0xD0+0x14];
	int iXOR, res;

	memcpy(enc+0x14, buf+0x80, 0xD0);

	for (iXOR = 0; iXOR < 0xD0; iXOR++)
	{
		enc[iXOR+0x14] ^= check_keys1[iXOR&0xF]; 
	}

	if ((res = Decrypt((u32 *)enc, 0xD0)) < 0)
	{
		printf("Decrypt failed.\n");
		return res;
	}

	for (iXOR = 0; iXOR < 0xD0; iXOR++)
	{
		enc[iXOR] ^= check_keys0[iXOR&0xF];
	}

	memcpy(buf+0x80, enc+0x40, 0x90);
	memcpy(buf+0x110, enc, 0x40);

	return 0;
}

////////// IPL Decryption /////////
int pspDecryptIPL1(const u8* pbIn, u8* pbOut, int cbIn)
{
	
	// 0x1000 pages
    static u8 g_dataTmp[0x1040] __attribute__((aligned(0x40)));
    int cbOut = 0;
    while (cbIn >= 0x1000)
    {
	    memcpy(g_dataTmp+0x40, pbIn, 0x1000);
        pbIn += 0x1000;
        cbIn -= 0x1000;
				//logbuffer(g_dataTmp+0x40,0x500);
        int ret = sceUtilsBufferCopyWithRange(g_dataTmp, 0x1040, g_dataTmp+0x40, 0x500, 1);
	    if (ret != 0)
        {
	        printf("Decrypt IPL 1 failed 0x%08X, WTF!\n", ret);
            break; // stop, save what we can
        }
        memcpy(pbOut, g_dataTmp, 0x1000);
        pbOut += 0x1000;
        cbOut += 0x1000;
    }

    return cbOut;
}

int pspLinearizeIPL2(const u8* pbIn, u8* pbOut, int cbIn)
{
	
	u32 nextAddr = 0;
    int cbOut = 0;
    while (cbIn > 0)
    {
        u32* pl = (u32*)pbIn;
        u32 addr = pl[0];
        
		if (addr != nextAddr && nextAddr != 0)
		{
			return 0;   // error
		}

        u32 count = pl[1];
        nextAddr = addr + count;
        memcpy(pbOut, pbIn+0x10, count);
        pbOut += count;
        cbOut += count;
        pbIn += 0x1000;
        cbIn -= 0x1000;
    }

    return cbOut;
}

int pspDecryptIPL3(const u8* pbIn, u8* pbOut, int cbIn)
{
	int ret;
	
	// all together now (pbIn/pbOut must be aligned)
    pbIn += 0x10000;
    cbIn -= 0x10000;
	memcpy(pbOut+0x40, pbIn, cbIn);
	//logbuffer(pbOut+0x40, cbIn);
	ret = sceUtilsBufferCopyWithRange(pbOut, cbIn+0x40, pbOut+0x40, cbIn, 1);
	if (ret != 0)
    {
		printf("mangle#1 returned $%x\n", ret);
        return 0;
    }

	ret = *(u32*)&pbIn[0x70];  // true size
    
	return ret;
}

////////// Decompression //////////

int pspIsCompressed(u8 *buf)
{
	int res = 0;

	if (buf[0] == 0x1F && buf[1] == 0x8B)
		res = 1;
	else if (memcmp(buf, "2RLZ", 4) == 0)
		res = 1;

	return res;
}

int pspDecompress(u8 *inbuf, u32 insize, u8 *outbuf, u32 outcapacity)
{
	int retsize;
	
	if (inbuf[0] == 0x1F && inbuf[1] == 0x8B) 
	{
		//retsize = sceKernelGzipDecompress(outbuf, outcapacity, inbuf, NULL);
    	z_stream infstream;
    	infstream.zalloc = Z_NULL;
    	infstream.zfree = Z_NULL;
    	infstream.opaque = Z_NULL;
    	// setup "b" as the input and "c" as the compressed output
    	infstream.avail_in = insize; // size of input
    	infstream.next_in = inbuf; // input
    	infstream.avail_out = outcapacity; // size of output
    	infstream.next_out = outbuf; // output char array
     	 
    	//inflateInit(&infstream);
    	inflateInit2(&infstream, 16+MAX_WBITS);
    	int x = inflate(&infstream, Z_NO_FLUSH);
    	inflateEnd(&infstream);

        retsize = infstream.total_out;
        //printf("ret %d outsize %d vs expected %d", x, retsize, outcapacity);
	}
	else if (memcmp(inbuf, "2RLZ", 4) == 0) 
	{
	    retsize = LZRDecompress(outbuf, outcapacity, inbuf+4, NULL);
		printf(",lzrc");
	}
	else if (memcmp(inbuf, "KL4E", 4) == 0)
	{
		retsize = decompress_kle(outbuf, outcapacity, inbuf+4, NULL, 1);
		printf(",kl4e");
	}
	else if (memcmp(inbuf, "KL3E", 4) == 0) 
	{
		retsize = decompress_kle(outbuf, outcapacity, inbuf+4, NULL, 0);
		printf(",kl3e");
	}
	else
	{
		retsize = -1;
	}

	return retsize;
}

////////// 3.70+ Table Insanity //////////

u8 key_C[56] = 
{
	0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37, 0x3F, 0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E, 
	0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D, 0x04, 0x0C, 0x14, 0x1C, 0x01, 0x09, 0x11, 0x19, 
	0x21, 0x29, 0x31, 0x39, 0x02, 0x0A, 0x12, 0x1A, 0x22, 0x2A, 0x32, 0x3A, 0x03, 0x0B, 0x13, 0x1B, 
	0x23, 0x2B, 0x33, 0x3B, 0x24, 0x2C, 0x34, 0x3C
};

u8 key_Z[48] = 
{
	0x32, 0x2F, 0x35, 0x28, 0x3F, 0x3B, 0x3D, 0x24, 0x31, 0x3A, 0x2B, 0x36, 0x29, 0x2D, 0x34, 0x3C, 
	0x26, 0x38, 0x30, 0x39, 0x25, 0x2C, 0x33, 0x3E, 0x17, 0x0C, 0x21, 0x1B, 0x11, 0x09, 0x22, 0x18, 
	0x0D, 0x13, 0x1F, 0x10, 0x14, 0x0F, 0x19, 0x08, 0x1E, 0x0B, 0x12, 0x16, 0x0E, 0x1C, 0x23, 0x20
};

u8 key_M[512] = 
{
	0x0D, 0x01, 0x02, 0x0F, 0x08, 0x0D, 0x04, 0x08, 0x06, 0x0A, 0x0F, 0x03, 0x0B, 0x07, 0x01, 0x04, 
	0x0A, 0x0C, 0x09, 0x05, 0x03, 0x06, 0x0E, 0x0B, 0x05, 0x00, 0x00, 0x0E, 0x0C, 0x09, 0x07, 0x02, 
	0x07, 0x02, 0x0B, 0x01, 0x04, 0x0E, 0x01, 0x07, 0x09, 0x04, 0x0C, 0x0A, 0x0E, 0x08, 0x02, 0x0D, 
	0x00, 0x0F, 0x06, 0x0C, 0x0A, 0x09, 0x0D, 0x00, 0x0F, 0x03, 0x03, 0x05, 0x05, 0x06, 0x08, 0x0B, 
	0x04, 0x0D, 0x0B, 0x00, 0x02, 0x0B, 0x0E, 0x07, 0x0F, 0x04, 0x00, 0x09, 0x08, 0x01, 0x0D, 0x0A, 
	0x03, 0x0E, 0x0C, 0x03, 0x09, 0x05, 0x07, 0x0C, 0x05, 0x02, 0x0A, 0x0F, 0x06, 0x08, 0x01, 0x06, 
	0x01, 0x06, 0x04, 0x0B, 0x0B, 0x0D, 0x0D, 0x08, 0x0C, 0x01, 0x03, 0x04, 0x07, 0x0A, 0x0E, 0x07, 
	0x0A, 0x09, 0x0F, 0x05, 0x06, 0x00, 0x08, 0x0F, 0x00, 0x0E, 0x05, 0x02, 0x09, 0x03, 0x02, 0x0C, 
	0x0C, 0x0A, 0x01, 0x0F, 0x0A, 0x04, 0x0F, 0x02, 0x09, 0x07, 0x02, 0x0C, 0x06, 0x09, 0x08, 0x05, 
	0x00, 0x06, 0x0D, 0x01, 0x03, 0x0D, 0x04, 0x0E, 0x0E, 0x00, 0x07, 0x0B, 0x05, 0x03, 0x0B, 0x08, 
	0x09, 0x04, 0x0E, 0x03, 0x0F, 0x02, 0x05, 0x0C, 0x02, 0x09, 0x08, 0x05, 0x0C, 0x0F, 0x03, 0x0A, 
	0x07, 0x0B, 0x00, 0x0E, 0x04, 0x01, 0x0A, 0x07, 0x01, 0x06, 0x0D, 0x00, 0x0B, 0x08, 0x06, 0x0D, 
	0x02, 0x0E, 0x0C, 0x0B, 0x04, 0x02, 0x01, 0x0C, 0x07, 0x04, 0x0A, 0x07, 0x0B, 0x0D, 0x06, 0x01, 
	0x08, 0x05, 0x05, 0x00, 0x03, 0x0F, 0x0F, 0x0A, 0x0D, 0x03, 0x00, 0x09, 0x0E, 0x08, 0x09, 0x06, 
	0x04, 0x0B, 0x02, 0x08, 0x01, 0x0C, 0x0B, 0x07, 0x0A, 0x01, 0x0D, 0x0E, 0x07, 0x02, 0x08, 0x0D, 
	0x0F, 0x06, 0x09, 0x0F, 0x0C, 0x00, 0x05, 0x09, 0x06, 0x0A, 0x03, 0x04, 0x00, 0x05, 0x0E, 0x03, 
	0x07, 0x0D, 0x0D, 0x08, 0x0E, 0x0B, 0x03, 0x05, 0x00, 0x06, 0x06, 0x0F, 0x09, 0x00, 0x0A, 0x03, 
	0x01, 0x04, 0x02, 0x07, 0x08, 0x02, 0x05, 0x0C, 0x0B, 0x01, 0x0C, 0x0A, 0x04, 0x0E, 0x0F, 0x09, 
	0x0A, 0x03, 0x06, 0x0F, 0x09, 0x00, 0x00, 0x06, 0x0C, 0x0A, 0x0B, 0x01, 0x07, 0x0D, 0x0D, 0x08, 
	0x0F, 0x09, 0x01, 0x04, 0x03, 0x05, 0x0E, 0x0B, 0x05, 0x0C, 0x02, 0x07, 0x08, 0x02, 0x04, 0x0E, 
	0x0A, 0x0D, 0x00, 0x07, 0x09, 0x00, 0x0E, 0x09, 0x06, 0x03, 0x03, 0x04, 0x0F, 0x06, 0x05, 0x0A, 
	0x01, 0x02, 0x0D, 0x08, 0x0C, 0x05, 0x07, 0x0E, 0x0B, 0x0C, 0x04, 0x0B, 0x02, 0x0F, 0x08, 0x01, 
	0x0D, 0x01, 0x06, 0x0A, 0x04, 0x0D, 0x09, 0x00, 0x08, 0x06, 0x0F, 0x09, 0x03, 0x08, 0x00, 0x07, 
	0x0B, 0x04, 0x01, 0x0F, 0x02, 0x0E, 0x0C, 0x03, 0x05, 0x0B, 0x0A, 0x05, 0x0E, 0x02, 0x07, 0x0C, 
	0x0F, 0x03, 0x01, 0x0D, 0x08, 0x04, 0x0E, 0x07, 0x06, 0x0F, 0x0B, 0x02, 0x03, 0x08, 0x04, 0x0E, 
	0x09, 0x0C, 0x07, 0x00, 0x02, 0x01, 0x0D, 0x0A, 0x0C, 0x06, 0x00, 0x09, 0x05, 0x0B, 0x0A, 0x05, 
	0x00, 0x0D, 0x0E, 0x08, 0x07, 0x0A, 0x0B, 0x01, 0x0A, 0x03, 0x04, 0x0F, 0x0D, 0x04, 0x01, 0x02, 
	0x05, 0x0B, 0x08, 0x06, 0x0C, 0x07, 0x06, 0x0C, 0x09, 0x00, 0x03, 0x05, 0x02, 0x0E, 0x0F, 0x09, 
	0x0E, 0x00, 0x04, 0x0F, 0x0D, 0x07, 0x01, 0x04, 0x02, 0x0E, 0x0F, 0x02, 0x0B, 0x0D, 0x08, 0x01, 
	0x03, 0x0A, 0x0A, 0x06, 0x06, 0x0C, 0x0C, 0x0B, 0x05, 0x09, 0x09, 0x05, 0x00, 0x03, 0x07, 0x08, 
	0x04, 0x0F, 0x01, 0x0C, 0x0E, 0x08, 0x08, 0x02, 0x0D, 0x04, 0x06, 0x09, 0x02, 0x01, 0x0B, 0x07, 
	0x0F, 0x05, 0x0C, 0x0B, 0x09, 0x03, 0x07, 0x0E, 0x03, 0x0A, 0x0A, 0x00, 0x05, 0x06, 0x00, 0x0D
};

u8 key_S[8] = 
{
	0x9E, 0xA4, 0x33, 0x81, 0x86, 0x0C, 0x52, 0x85
};

u8 key_S2[8] = 
{
	0xB2, 0xFE, 0xD9, 0x79, 0x8A, 0x02, 0xB1, 0x87
};

u8 key_S3[8] = 
{
	0x81, 0x08, 0xC1, 0xF2, 0x35, 0x98, 0x69, 0xB0 
};

u8 key_S4[8] =
{
	0x6D, 0x52, 0x1B, 0xA3, 0xC2, 0x36, 0xF9, 0x2B
};

u8 key_S5[8] =
{
	0xDB, 0x4E, 0x79, 0x41, 0xF5, 0x97, 0x30, 0xAD
};

u8 key_S6[8] =
{
	0xA6, 0x83, 0x0C, 0x2F, 0x63, 0x0B, 0x96, 0x29
};

u8 table_40[128] = 
{
	0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 
	0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 
	0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 
	0x00, 0x00, 0x00, 0x20, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01, 
	0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x40, 0x00, 0x00, 0x10, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 
	0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 
	0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00
};


typedef struct
{
	u32 mklow;
	u32 mkhigh;
	u8 *key_S;
} TABLE_KEYS;

TABLE_KEYS table_keys[] =
{
	{ 0xb730e5c7, 0x95620b49, key_S  },
	{ 0x45c9dc95, 0x5a7b3d9d, key_S2 },
	{ 0x6F20585A, 0x4CCE495B, key_S3 },
	{ 0x620BF15A, 0x73F45262, key_S4 },
	{ 0xFD9D4498, 0xA664C8F8, key_S5 },
    { 0x3D6426E7, 0xD7BD7481, key_S6 }
};

static void GenerateSeed(void *out, int unused, u32 c1, u32 c2)
{
	u32 i, j, val1, val2, bit_insert, r1, r2, shr, base, wpar1, wpar2;
	u32 *out32 = (u32 *)out;
	
	bit_insert = 0x80000000;
	r1 = r2 = base = 0;

	for (i = 0; i < 0x38; i++)
	{
		val1 = key_C[i];
		
		if (val1 & 0x20)
		{
			val2 = (1 << val1);
			val1 = 0;			
		}
		else
		{
			val1 = (1 << val1);
			val2 = 0;
		}		

		val1 = (c1 & val1);
		val2 = (c2 & val2);
		
		if ((val1 | val2))
		{
			val1 = base;
			val2 = bit_insert;
		}
		else
		{
			val1 = 0;
			val2 = 0;
		}

		r1 = (r1 | val1);
		r2 = (r2 | val2);
		base = (base >> 1);
		base = (base & 0x7FFFFFFF) | ((bit_insert & 1) << 31); 
		bit_insert = (bit_insert >> 1);
	}

	wpar1 = (r2 >> 4);
	wpar2 = (r1 >> 8) & 0x00FFFFFF; 
	wpar2 = (wpar2  & 0xF0FFFFFF) | ((r2 & 0xF) << 24); 

	for (i = 0x10; i != 0; i--)
	{	
		r1 = 0x7efc;
		val1 = (wpar1 << 4);
		r1 = (r1 >> i);
		val2 = (wpar2 << 4);
		r1 = (r1 & 1);
		shr = (r1 ^ 0x1F);
		r1++;
		val1 = (val1 >> shr);
		val2 = (val2 >> shr);
		wpar1 = (wpar1 << r1);
		wpar2 = (wpar2 << r1);
		wpar1 = (wpar1 | val1);
		wpar2 = (wpar2 | val2);
		wpar1 = (wpar1 & 0x0FFFFFFF);
		wpar2 = (wpar2 & 0x0FFFFFFF);
		c2 = (wpar2 >> 24);
		c2 = (c2&0xF) | ((wpar1 & 0x0FFFFFFF) << 4);
		c1 = (wpar2 << 8);

		base = r1 = r2 = 0;
		bit_insert = 0x80000000;
		
		for (j = 0; j < 0x30; j++)
		{
			val1 = key_Z[j];
			
			if (val1 & 0x20)
			{
				val2 = (1 << val1);
				val1 = 0;				
			}
			else
			{
				val1 = (1 << val1);
				val2 = 0;
			}

			val1 = (c1 & val1);
			val2 = (c2 & val2);
			
			if ((val1 |val2))
			{
				val1 = base;
				val2 = bit_insert;
			}
			else
			{
				val1 = 0;
				val2 = 0;
			}

			r1 = (r1 | val1);
			r2 = (r2 | val2);
			base = (base >> 1);
			base = (base & 0x7FFFFFFF) | ((bit_insert & 1) << 31); 
			bit_insert = (bit_insert >> 1);
		}

		out32[0] = r1;
		out32[1] = r2;
		out32 += 2;
	}
}

static void Sce_Insanity_1(u32 x1, u32 x2, u32 *r1, u32 *r2)
{
	u32 temp;
	
	temp = ((x2 >> 4) ^ x1) & 0x0F0F0F0F;
	x2 = x2 ^ (temp << 4);
	x1 = x1 ^ temp;
	temp = ((x2 >> 16) ^ x1) & 0xFFFF;
	x1 = x1 ^ temp;
	x2 = x2 ^ (temp << 16);
	temp = ((x1 >> 2) ^ x2) & 0x33333333;
	x1 = x1 ^ (temp << 2);
	x2 = x2 ^ temp;
	temp = ((x1 >> 8) ^ x2) & 0x00FF00FF;
	x2 = (x2 ^ temp);
	x1 = x1 ^ (temp << 8);
	temp = ((x2 >> 1) ^ x1) & 0x55555555;
	*r2 = x2 ^ (temp << 1);
	*r1 = x1 ^ temp;
}

static void Sce_Insanity_2(u32 x1, u32 x2, u32 *r1, u32 *r2)
{
	u32 h1, h2, h3, h4;

	h1 = (x2 & 1);
	h2 = (x2 >> 27) & 0x1F;
	h3 = (x2 >> 23) & 0x3F;
	h4 = (x2 >> 19) & 0x3F;
	*r2 = (x2 >> 15) & 0x3F;
	*r2 = (*r2 & 0xFF7FFFFF) | ((h1 & 1) << 23);
	*r2 = (*r2 & 0xFF83FFFF) | ((h2 & 0x1F) << 18);
	*r2 = (*r2 & 0xFFFC0FFF) | ((h3 & 0x3F) << 12);
	*r2 = (*r2 & 0xFFFFF03F) | ((h4 & 0x3F) << 6);
	h1 = (x2 >> 11) & 0x3F;
	h2 = (x2 >> 7) & 0x3F;
	h3 = (x2 >> 3) & 0x3F;
	h4 = (x2 & 0x1F);
	*r1 = (x2 >> 31) & 1;
	*r1 = (*r1 & 0xFF03FFFF) | ((h1 & 0x3F) << 18);
	*r1 = (*r1 & 0xFFFC0FFF) | ((h2 & 0x3F) << 12);
	*r1 = (*r1 & 0xFFFFF03F) | ((h3 & 0x3F) << 6);
	*r1 = (*r1 & 0xFFFFFFC1) | ((h4 & 0x1F) << 1);
	
	*r2 = ((*r2 << 8) | ((*r1 >> 16) & 0xFF));
	*r1 = (*r1 << 16);
}

static void Sce_Insanity_3(u32 x1, u32 x2, u32 *r1, u32 *r2)
{
	int i;
	u32 shifter = 0, val;
	u8 *p = key_M;

	*r2 = 0;

	for (i = 0; i < 8; i++)
	{
		val = p[x1&0x3F];
		p += 0x40;
		x1 = (x1 >> 6);
		x1 = (x1 & 0x03FFFFFF) | ((x2 & 0x3F) << 26);
		x2 = (x2 >> 6);
		*r2 |= (val << shifter);
		shifter += 4;
	}

	*r1 = 0;
}

static void Sce_Insanity_4(u32 x1, u32 x2, u32 *r1, u32 *r2)
{
	int i;
	u32 *table = (u32 *)table_40;

	*r1 = 0;
	*r2 = 0;

	for (i = 0; i < 0x20; i++)
	{
		if (x2 & 1)
		{
			*r2 |= table[i];
		}		

		x2 = (x2 >> 1);		
	}
}

static void Sce_Insanity_5(u32 x1, u32 x2, u32 *r1, u32 *r2)
{
	u32 temp;

	temp = ((x2 >> 1) ^ x1) & 0x55555555;
	x1 = x1 ^ temp;
	x2 = x2 ^ (temp << 1);
	temp = ((x1 >> 8) ^ x2) & 0x00FF00FF;
	x1 = x1 ^ (temp << 8);
	x2 = x2 ^ temp;
	temp = ((x1 >> 2) ^ x2) & 0x33333333;
	x2 = x2 ^ temp;
	x1 = x1 ^ (temp << 2);
	temp = ((x2 >> 16) ^ x1) & 0xFFFF;
	x2 = x2 ^ (temp << 16);
	x1 = x1 ^ temp;
	temp = ((x2 >> 4) ^ x1) & 0x0F0F0F0F;
	*r1 = x1 ^ temp;
	*r2 = x2 ^ (temp << 4);
}

static void Sce_Paranoia(u8 *buf, u32 unused, u32 *p1, u32 *p2)
{
	u32 x1 = *p1;
	u32 x2 = *p2;
	u32 r1, r2, rot1, rot2, rot3, rot4, ro1, ro2, base;
	int i;
	u8 *p = buf+0x78;

	Sce_Insanity_1(x1, x2, &r1, &r2); 

	rot1 = 0;
	rot2 = 0;
	rot3 = r1;
	rot4 = r2;

	for (i = 0; i < 0x10; i++)
	{
		Sce_Insanity_2(rot1, rot3, &r1, &r2);

		ro1 = r1;
		ro2 = r2;
		r1 = *(u32 *)&p[0];
		r2 = *(u32 *)&p[4];
		p -= 8;
		base = (ro2 ^ r2);
		x1 = (base << 16);
		
		Sce_Insanity_3(((ro1 ^ r1) >> 16) | x1, base >> 16, &r1, &r2);
		Sce_Insanity_4(r1, r2, &r1, &r2);

		x1 = (r1 ^ rot2);
		x2 = (r2 ^ rot4);
		rot2 = rot1;
		rot4 = rot3;
		rot1 = x1;
		rot3 = x2;
	}

	Sce_Insanity_5(x1 | rot4, x2, p1, p2);
}

static void DecryptT(u8 *buf, int size, int mode)
{
	u8 m1[0x400];
	u8 m2[8];
	int i, j;

	memset(m1, 0, sizeof(m1));
	
	GenerateSeed(m1, 0x33333333, table_keys[mode].mklow, table_keys[mode].mkhigh);
	
	memcpy(m1+0x80, table_keys[mode].key_S, 8);

	for (i = 0; i < size; i++)
	{
		for (j = 0; j < 8; j++)
		{
			m2[7-j] = buf[j];
		}

		Sce_Paranoia(m1, 0x33333333, (u32 *)&m2[0], (u32 *)&m2[4]);

		for (j = 0; j < 8; j++)
		{
			m1[0x90+j] = m2[7-j] ^ m1[0x80+j];
			m1[0x80+j] = buf[j];
		}

		*(u32 *)&buf[0] = *(u32 *)&m1[0x90];
		*(u32 *)&buf[4] = *(u32 *)&m1[0x94];

		buf += 8;
	}	
}

int pspDecryptTable(u8 *buf1, u8 *buf2, int size, int mode)
{
	int retsize;

	DecryptT(buf1, size >> 3, mode);

	if (buf1 != buf2) memcpy(buf2, buf1, size);
	
	retsize = pspDecryptPRX(buf2, buf1, size);
	if (retsize < 0)
	{	
	    retsize = -1;
	}

	return retsize;
}

