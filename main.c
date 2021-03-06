/*
Copyright (C) 2012 Paul Gardner-Stephen
 
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
 
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
 
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include<sys/types.h>
#include<sys/time.h>

#include "arithmetic.h"
#include "method_stats3.h"

int processFile(FILE *f);

int lines=0;
double worstPercent=0,bestPercent=100;
long long total_compressed_bits=0;
long long total_uncompressed_bits=0;
long long total_alpha_bits=0;
long long total_nonalpha_bits=0;
long long total_case_bits=0;
long long total_model_bits=0;
long long total_length_bits=0;
long long total_finalisation_bits=0;

long long total_messages=0;

long long total_stats3_bytes=0;
long long total_smaz_bytes=0;

long long stats3_compress_us=0;
long long stats3_decompress_us=0;
long long smaz_compress_us=0;
long long smaz_decompress_us=0;

int main(int argc,char *argv[])
{
  if (!argv[1]) {
    fprintf(stderr,"You didn't provide me any messages to test, so I'll make some up.\n");
    range_coder *c=range_new_coder(2048);
    while(1)
      {
	int i;
	for(i=0;i<2048;i++) c->bit_stream[i]=random()&0xff;
	c->bit_stream[0]|=0x80; // make sure it gets treated as a compressed message.
	c->low=0; c->high=0xffffffff;
	c->bit_stream_length=8192;
	c->bits_used=0;
	range_decode_prefetch(c);
	char out[2048];
	int lenout;
	stats3_decompress_bits(c,out,&lenout);
	printf("%s\n",out);
      }

    exit(-1);
  }
  
  FILE *f;

  int argn=1;

  for(argn=1;argn<argc;argn++) {
    if (strcmp(argv[1],"-")) f=fopen(argv[argn],"r"); else f=stdin;
    if (!f) {
      fprintf(stderr,"Failed to open `%s' for input.\n",argv[1]);
      exit(-1);
    } else {
      processFile(f);
      fclose(f);
    }
  }

  printf("Summary:\n");
  printf("         compressed size: %f%% (bit oriented)\n",
	 total_compressed_bits*100.0/total_uncompressed_bits);
  printf("         compressed size: %f%% (byte oriented)\n",
	 total_stats3_bytes*100.0/(total_uncompressed_bits/8.0));
  printf("       uncompressed bits: %lld\n",total_uncompressed_bits);
  printf("        compressed bytes: %lld\n",total_stats3_bytes);
  printf("         compressed bits: %lld\n",total_compressed_bits);
  printf("    length-encoding bits: %lld\n",total_length_bits);
  printf("     model-encoding bits: %lld\n",total_model_bits);
  printf("      case-encoding bits: %lld\n",total_case_bits);
  printf("     alpha-encoding bits: %lld\n",total_alpha_bits);
  printf("  nonalpha-encoding bits: %lld\n",total_nonalpha_bits);
  printf("   SMAZ compressed bytes: %lld (for comparison)\n",total_smaz_bytes);
  printf("    SMAZ compressed size: %f%% (byte oriented; for comparison)\n",
	 total_smaz_bytes*100.0/(total_uncompressed_bits/8.0));
  printf("\n");
  printf("stats3 compression time: %lld usecs (%.1f messages/sec, %f MB/sec)\n",
	 stats3_compress_us,1000000.0/(stats3_compress_us*1.0/total_messages),total_uncompressed_bits*0.125/stats3_compress_us);
  printf("stats3 decompression time: %lld usecs (%.1f messages/sec, %f MB/sec)\n",
	 stats3_decompress_us,1000000.0/(stats3_decompress_us*1.0/total_messages),total_uncompressed_bits*0.125/stats3_decompress_us);
  printf("SMAZ compression time: %lld usecs (%.1f messages/sec, %f MB/sec)\n",
	 smaz_compress_us,1000000.0/(smaz_compress_us*1.0/total_messages),total_uncompressed_bits*0.125/smaz_compress_us);
  printf("SMAZ decompression time: %lld usecs (%.1f messages/sec, %f MB/sec)\n",
	 smaz_decompress_us,1000000.0/(smaz_decompress_us*1.0/total_messages),total_uncompressed_bits*0.125/smaz_decompress_us);

  return 0;
}

long long current_time_us()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_usec+tv.tv_sec*1000000LL;
}

int processFile(FILE *f)
{
  char m[1024]; // raw message, no pre-processing
  long long now;

  m[0]=0; fgets(m,1024,f);
  
  while(m[0]) {    
    /* chop newline */
    m[strlen(m)-1]=0;

    total_messages++;

    range_coder *c=range_new_coder(1024);
    now = current_time_us();
    stats3_compress_bits(c,(unsigned char *)m);
    stats3_compress_us+=current_time_us()-now;

    total_compressed_bits+=c->bits_used;
    total_uncompressed_bits+=strlen(m)*8;

    /* Also count whole bytes for comparison with SMAZ etc */
    total_stats3_bytes+=c->bits_used>>3;
    if (c->bits_used&7) total_stats3_bytes++;

    /* Compare with SMAZ original algorithm */
    {
      unsigned char out[2048];
      now=current_time_us();
      int outlen=smaz_compress(m,strlen(m),out,2048);
      smaz_compress_us+=current_time_us()-now;   
      total_smaz_bytes+=outlen;

      unsigned char out2[2048];
      now=current_time_us();
      outlen=smaz_decompress(out,outlen,out2,2048);
      smaz_decompress_us+=current_time_us()-now;   
      
    }

    double percent=c->bits_used*100.0/(strlen(m)*8);
    if (percent<bestPercent) bestPercent=percent;
    if (percent>worstPercent) worstPercent=percent;

    /* Verify that compression worked */
    {
      int lenout=0;
      char mout[1025];
      range_coder *d=range_coder_dup(c);
      d->bit_stream_length=d->bits_used;
      d->bits_used=0;
      d->low=0; d->high=0xffffffff;
      
      now=current_time_us();
      range_decode_prefetch(d);
      stats3_decompress_bits(d,mout,&lenout);
      stats3_decompress_us+=current_time_us()-now;

      if (lenout!=strlen(m)) {	
	printf("Verify error: length mismatch: decoded = %d, original = %d\n",lenout,(int)strlen(m));
	printf("   Input: [%s]\n  Output: [%s]\n",m,mout);
      	exit(-1);
      } else if (strcasecmp(m,mout)) {
	printf("Verify error: even ignoring case, the messages do not match.\n");
	printf("   Input: [%s]\n  Output: [%s]\n",m,mout);
	int i;
	for(i=0;i<strlen(m);i++)
	  printf("<%02x>",m[i]);
	printf("\n");
	exit(-1);
      } else if (strcmp(m,mout)) {
	printf("Verify error: messages differ in case only.\n");
	printf("   Input: [%s]\n  Output: [%s]\n",m,mout);
	exit(-1);
      }

      range_coder_free(d);
    }

    range_coder_free(c);

    lines++;
    m[0]=0; fgets(m,1024,f);
  }
  return 0;
}
