/* Read-only asset audit codec, preserved from the verified 2026-09-09
 * target_title_graphics.py evidence. Tables/key come from the hash-checked EXE.
 * This is not linked into the game server or client. */
#include <stdint.h>
#include <stddef.h>
static uint32_t P[18],S[1024];
static uint32_t F(uint32_t x) {
 uint32_t y=((S[x&255]^S[256+((x>>8)&255)])+S[512+((x>>16)&255)])^S[768+(x>>24)];
 return ((y<<3)|(y>>29))^y;
}
static void enc(uint32_t *a,uint32_t *b) {
 uint32_t l=*a,r=*b,t;
 for(int i=0;i<16;i++){r^=P[i];l^=F(r);t=l;l=r;r=t;}
 t=l;l=r;r=t;l^=P[16];r^=P[17];*a=l;*b=r;
}
void init(const uint32_t *p,const uint32_t *s,const unsigned char *key,size_t n){
 for(int i=0;i<1024;i++)S[i]=s[i]^0x9b33cc17u;
 size_t j=0;
 for(int i=0;i<18;i++){uint32_t d=0;for(int k=0;k<4;k++){d=(d<<8)|key[j];j=(j+1)%n;}P[i]=p[i]^d^0x1f2a374cu;}
 uint32_t l=0,r=0;
 for(int i=0;i<18;i+=2){enc(&l,&r);l^=0x5a6b7c8du;r^=0x1a2b3c4du;P[i]=l;P[i+1]=r;}
 for(int i=0;i<1024;i+=2){enc(&l,&r);l^=0x98765432u;r^=0x12345678u;S[i]=l;S[i+1]=r;}
}
void decode(uint32_t *v,size_t count){
 for(size_t k=0;k<count;k+=2){
  uint32_t l=v[k]^P[16],r=v[k+1]^P[17],t;
  t=l;l=r;r=t;
  for(int i=15;i>=0;i--){t=l;l=r;r=t;l^=F(r);r^=P[i];}
  v[k]=l;v[k+1]=r;
 }
}
void encode(uint32_t *v,size_t count){for(size_t k=0;k<count;k+=2)enc(v+k,v+k+1);}
