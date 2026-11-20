#include "home_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

bool home_ui_test_preview(unsigned page,uint8_t *frame,size_t n)
{
    if(page==6)return home_ui_render_empty_art(frame,n);
    home_ui_t u;home_ui_init(&u);u.active=true;u.page=(home_ui_page_t)page;u.draft=70;
    return home_ui_render(frame,n,&u);
}
static void shown(home_ui_t *u,int64_t now)
{ home_ui_presented(u,u->active,u->revision,now); }
static bool tap(home_ui_t *u,int x,int y,int64_t now)
{
    home_ui_touch(u,1,x,y,0,now);
    return home_ui_touch(u,0,-1,-1,0,now+1000);
}
int main(void)
{
    home_ui_t u;home_ui_init(&u);
    assert(!u.active&&u.light_on[0]);
    assert(home_ui_touch(&u,1,100,200,0,1000)&&u.active&&u.light_on[0]);
    shown(&u,2000);home_ui_touch(&u,1,100,200,0,3000);
    assert(!home_ui_touch(&u,0,-1,-1,0,4000)&&u.light_on[0]);
    assert(tap(&u,100,200,5000)&&!u.light_on[0]);
    assert(!tap(&u,100,200,6000)&&!u.light_on[0]); /* Unpresented revision. */
    shown(&u,7000);
    assert(tap(&u,100,200,8000)&&u.light_on[0]);shown(&u,9000);
    home_ui_touch(&u,1,100,200,0,10000);home_ui_touch(&u,1,790,100,0,11000);
    assert(!home_ui_touch(&u,0,-1,-1,0,12000)&&u.light_on[0]);
    home_ui_touch(&u,1,100,200,0,13000);home_ui_touch(&u,2,100,200,0,14000);
    assert(!home_ui_touch(&u,0,-1,-1,0,15000)&&u.light_on[0]);
    home_ui_touch(&u,1,100,200,0,16000);home_ui_touch(&u,1,100,200,1,17000);
    assert(!home_ui_touch(&u,0,-1,-1,1,18000)&&u.light_on[0]);
    home_ui_touch(&u,1,100,200,0,19000);home_ui_cancel_touch(&u);
    assert(!home_ui_touch(&u,0,-1,-1,0,20000)&&u.light_on[0]);
    assert(tap(&u,300,450,21000)&&u.page==HOME_PAGE_ROOMS);shown(&u,22000);
    assert(tap(&u,100,150,23000)&&u.page==HOME_PAGE_ROOM);shown(&u,24000);
    assert(tap(&u,70,130,25000)&&u.page==HOME_PAGE_DEVICE);shown(&u,26000);
    assert(tap(&u,380,310,27000)&&u.draft==80&&u.brightness[0]==70);shown(&u,28000);
    assert(tap(&u,600,310,29000)&&u.brightness[0]==80);shown(&u,30000);
    assert(tap(&u,700,450,31000)&&!u.active);shown(&u,32000);
    /* Wake at the Art tab must open HOME, never execute that underlying tab. */
    assert(home_ui_touch(&u,1,700,450,0,33000)&&u.active&&u.page==HOME_PAGE_HOME);
    shown(&u,34000);assert(!home_ui_touch(&u,0,-1,-1,0,35000)&&u.active);
    shown(&u,40000);
    assert(!home_ui_tick(&u,40000+HOME_UI_IDLE_US-1));
    assert(home_ui_tick(&u,40000+HOME_UI_IDLE_US)&&!u.active);
    home_ui_open_menu(&u,50000);shown(&u,50000);
    home_ui_touch(&u,1,100,200,0,60000);
    assert(!home_ui_tick(&u,60000+HOME_UI_IDLE_US)); /* Held finger. */

    static uint8_t guarded[HOME_UI_BYTES+2],before[HOME_UI_BYTES];
    for(unsigned page=0;page<7;++page){
        memset(guarded,0xa5,sizeof(guarded));
        assert(home_ui_test_preview(page,guarded+1,HOME_UI_BYTES));
        assert(guarded[0]==0xa5&&guarded[sizeof(guarded)-1]==0xa5);
        if(page<6){u.page=(home_ui_page_t)page;home_ui_control_t controls[20];
            unsigned n=home_ui_controls(&u,controls,20);assert(n>=5&&n<=20);
            for(unsigned i=0;i<n;++i){const home_ui_control_t *c=&controls[i];
                assert(c->x>=0&&c->y>=0&&c->x+c->w<=800&&c->y+c->h<=480&&c->h>=44);
            }
        }
    }
    home_ui_region_t r;memset(before,0,sizeof(before));memset(guarded,0,sizeof(guarded));
    assert(!home_ui_changed_region(before,guarded,HOME_UI_BYTES,&r));
    guarded[HOME_UI_BYTES-1]=1;
    assert(home_ui_changed_region(before,guarded,HOME_UI_BYTES,&r)&&r.x==792&&r.y==479&&r.width==8&&r.height==1);
    for(unsigned m=0;m<8;++m)for(unsigned corner=0;corner<4;++corner){
        int x,y;assert(home_ui_map_touch(corner&1?799:0,corner&2?479:0,800,480,m,&x,&y));
        assert(x>=0&&x<800&&y>=0&&y<480);
    }
    puts("PASS: UI wake swallowing, rendered-revision gating, release/drag/multitouch, navigation, apply, 45s timeout, glyph/layout bounds and edge regions.");
    return 0;
}
