/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "../core/framebuffer.h"

int main(void)
{
    uint8_t data[8 * 40 + 32], reference[sizeof(data)], before[sizeof(data)];
    RP_SURFACE s = {data + 16, 320, 8, 8, 40};
    RP_RECT r;
    size_t bytes = 0, tests = 0;
    int sx, sy, dx, dy, w, h, y, x;
    assert(rp_layout(1920,1080,7680,&bytes) && bytes == 8294400);
    assert(!rp_layout(0,1,4,&bytes)); assert(!rp_layout(UINT32_MAX,1,4,&bytes));
    assert(!rp_layout(1,UINT32_MAX,4,&bytes)); assert(!rp_layout(10,2,36,&bytes));
    assert(!rp_layout(1,1,5,&bytes)); assert(!rp_layout(8192,8192,32768,&bytes));
    assert(!rp_layout(1,1,4,NULL));
    r = (RP_RECT){-1,0,1,1}; assert(!rp_move(&s,r,0,0));
    r = (RP_RECT){0,0,INT32_MAX,1}; assert(!rp_move(&s,r,0,0));
    r = (RP_RECT){1,0,0,1}; assert(!rp_move(&s,r,0,0));
    r = (RP_RECT){0,0,1,1}; assert(!rp_move(&s,r,INT32_MAX,0));
    /* Exhaust all in-bounds rectangle moves on 8x8, including every overlap direction.
       Compare to an independent snapshot algorithm; padding and both guards must survive. */
    for (w=1; w<=8; ++w) for (h=1; h<=8; ++h)
    for (sy=0; sy+h<=8; ++sy) for (sx=0; sx+w<=8; ++sx)
    for (dy=0; dy+h<=8; ++dy) for (dx=0; dx+w<=8; ++dx) {
        size_t i;
        for (i=0; i<sizeof(data); ++i) data[i]=(uint8_t)((i*13u+tests)%251u);
        memcpy(before,data,sizeof(data)); memcpy(reference,data,sizeof(data));
        for (y=0;y<h;++y) for (x=0;x<w*4;++x)
            reference[16+(dy+y)*40+dx*4+x]=before[16+(sy+y)*40+sx*4+x];
        r=(RP_RECT){dx,dy,dx+w,dy+h};
        assert(rp_move(&s,r,sx,sy)); assert(memcmp(data,reference,sizeof(data))==0);
        ++tests;
    }
    {
        uint8_t source[8*36], target[8*40];
        RP_SURFACE a={target,sizeof(target),8,8,40}, b={source,sizeof(source),8,8,36};
        memset(source,0x73,sizeof(source)); memset(target,0xAD,sizeof(target));
        r=(RP_RECT){2,1,7,6}; assert(rp_copy(&a,&b,r));
        for(y=0;y<8;++y) for(x=0;x<40;++x)
            assert(target[y*40+x] == (uint8_t)((y>=1&&y<6&&x>=8&&x<28)?0x73:0xAD));
        a.size=1; assert(!rp_copy(&a,&b,r));
    }
    printf("PASS: %zu exhaustive overlap moves, layout rejection and stride/guard tests\n",tests);
    return 0;
}
