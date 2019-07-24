
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef char s8;
typedef short s16;
typedef int s32;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

static int d_debug = 0;
static int c_debug = 0;

/*************************************************************/

int tlz_decompress(u8 *dst, u8 *src)
{
	int i, sp, dp;
	int flag, offset, len;
	u8 *pbuf;

	sp = 0;
	dp = 0;
	flag = 0x0100;

	while(1){
		if(flag&0x0100){
			flag = src[sp++];
			if(d_debug) printf("flag %02x at %04x\n", flag, sp-1);
			flag |= 0x00010000;
		}

		if(flag&1){
			/* raw byte */
			if(d_debug) printf("%04x raw: %02x\n", dp, src[sp]);
			dst[dp++] = src[sp++];
			flag >>= 1;
		}else{
			flag >>= 1;
			if(flag&0x0100){
				flag = src[sp++];
				if(d_debug) printf("flag %02x at %04x\n", flag, sp-1);
				flag |= 0x00010000;
			}
			offset = src[sp++];
			if(flag&1){
				/* 01: long format */
				len = offset>>4;
				len += 3;
				offset <<= 8;
				offset |= src[sp++];
				offset &= 0x0fff;
				if(d_debug) printf("%04x  long: pos=%4d len=%2d\n", dp, offset+1, len);
			}else{
				/* 00: short format */
				if(offset==0xff)
					break;
				len = (offset>>6);
				len += 2;
				offset &= 0x3f;
				if(d_debug) printf("%04x short: pos=%4d len=%2d\n", dp, offset+1, len);
			}
			flag >>= 1;
			pbuf = &dst[dp-offset-1];
			for(i=0; i<len; i++){
				dst[dp++] = pbuf[i];
			}
		}
	}

	return dp;
}

/*************************************************************/

static u8 *dst, *src;
static int dp, sp, fp;
static int flag, bits;

void put_flag(int bit)
{
	if(bits==8){
		if(c_debug) printf("flag %02x at %04x\n", flag, fp);
		dst[fp] = flag;
		fp = dp;
		dp += 1;
		bits = 0;
		flag = 0;
	}

	flag >>= 1;
	if(bit)
		flag |= 0x80;
	bits += 1;
}

int tlz_compress(u8 *dst_buf, u8 *src_buf, int src_len)
{
	int i, j, pp;
	int match_len, match_pos;

	dst = dst_buf;
	src = src_buf;
	sp = 0;
	fp = 0;
	dp = 1;
	bits = 0;
	flag = 0;


	while(sp<src_len){
		/* find match
		 * max windows: 1-4095 1-63
		 * max length: 3-18/2-5
		 */
		match_len = 1;
		match_pos = 1;
		pp = sp-4095;
		if(pp<0)
			pp = 0;
		for(i=pp; i<sp; i++){
			for(j=0; (j<18 && sp+j<src_len); j++){
				if(src[i+j]!=src[sp+j])
					break;
			}
			if(j>=match_len){
				match_len = j;
				match_pos = sp-i;
			}
		}

		if(match_len==1 || (match_len==2 && match_pos>63)){
			/* raw byte */
			put_flag(1);
			if(c_debug) printf("%04x raw: %02x\n", sp, src[sp]);
			dst[dp++] = src[sp++];
		}else{
			put_flag(0);

			if(match_len<6 && match_pos<64){
				/* short match */
				put_flag(0);

				if(c_debug) printf("%04x short: pos=%4d len=%2d\n", sp, match_pos, match_len);
				sp += match_len;

				match_pos -= 1;
				match_len -= 2;
				match_pos |= (match_len<<6);

				dst[dp++] = match_pos;

			}else{
				/* long match */
				put_flag(1);

				if(c_debug) printf("%04x  long: pos=%4d len=%2d\n", sp, match_pos, match_len);
				sp += match_len;

				match_pos -= 1;
				match_len -= 3;
				match_pos |= (match_len<<12);

				dst[dp++] = (match_pos>>8);
				dst[dp++] = (match_pos&0xff);
			}
		}

	}

	/* end of stream */
	put_flag(0);
	put_flag(0);
	dst[dp++] = 0xff;

	if(bits){
		flag >>= (8-bits);
		if(c_debug) printf("flag %02x at %04x\n", flag, fp);
		dst[fp] = flag;
	}

	return dp;
}

/*************************************************************/


int main(int argc, char *argv[])
{
	FILE *fp;
	int size, tlz_size, tlz_addr;
	u8 *ibuf, *obuf;
	u32 *hd;

	if(argc<4){
		printf("Usage: mktlz  <input file> <output file> <load address>\n");
		return 0;
	}

	tlz_addr = strtol(argv[3], NULL, 0);
	if(tlz_addr==0){
		printf("Wrong tlz_addr! %d\n", tlz_addr);
		return -1;
	}

	/* load input file */
	fp = fopen(argv[1], "rb");
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	ibuf = malloc(size*2);
	memset(ibuf, 0, size*2);

	fread(ibuf, size, 1, fp);
	fclose(fp);

	/* output buf */
	obuf = malloc(size*2);
	memset(obuf, 0, size*2);

	/* compress it! */
	tlz_size = tlz_compress(obuf+0x10, ibuf, size);
	printf("tlz_compress: %d -> %d\n", size, tlz_size);

	/* header space */
	hd = (u32*)obuf;
	hd[0] = 0x445a4c54; // "TLZD"
	hd[1] = tlz_size;
	hd[2] = tlz_addr;
	hd[3] = size;

	/* write output */
	fp = fopen(argv[2], "wb");
	if(fp==NULL){
		printf("Open output file faield! %s\n", argv[3]);
		return -1;
	}

	fwrite(obuf, 0x10+tlz_size, 1, fp);
	fclose(fp);
	printf("Write %s\n", argv[2]);

	free(ibuf);
	free(obuf);

	return 0;
}



