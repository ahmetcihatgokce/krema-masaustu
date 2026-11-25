#include "pet.h"
#include "art.h"
#include <math.h>
#include <string.h>

static float bound(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float soft(float x) { x=bound(x,0,1); return x*x*(3-2*x); }
static float absv(float x) { return x<0 ? -x : x; }
static float sine(double phase) { return (float)sin(phase); }
static float ease(float dt,float rate) { return (float)(1-exp(-dt*rate)); }
enum { BOARD_X=25, BOARD_Y=203, MOUSE_X=202, MOUSE_Y=210 };

void pet_wake(Pet *p,double now)
{
    p->last_input=now;
    p->last_mouse=now;
}
void pet_home(Pet *p,WorkArea a)
{
    float s=p->dpi_scale;
    p->x=bound(a.right-(PET_WIDTH+12)*s,a.left,(float)a.right);
    p->y=bound(a.bottom-(PET_HEIGHT+6)*s,a.top,(float)a.bottom);
    p->walking=0;
}
void pet_init(Pet *p,double now,WorkArea a,float scale)
{
    memset(p,0,sizeof *p);
    p->now=p->last_tick=now;
    p->dpi_scale=scale;
    p->keycap=-1;
    p->last_stroke=p->last_key=p->last_click=p->last_hiss=now-10;
    p->next_stroke_sound=now;
    p->facing=1;
    pet_wake(p,now);
    pet_home(p,a);
}
void pet_key(Pet *p,int scan_code,int extended,double now)
{
    if (extended && scan_code==56) scan_code=100;
    int i;
    for (i=0;i<KEY_COUNT;i++) if (keycaps[i].scan==scan_code) break;
    p->keycap=i<KEY_COUNT ? i : -1;
    float x=i<KEY_COUNT ? BOARD_X+keycaps[i].x+keycaps[i].w*.5f : 120;
    p->tap_x=bound((x-152)*.39f,-28,9);
    p->last_key=p->last_input=now;
}
void pet_button(Pet *p,int button,int pressed,double now)
{
    int bit=1<<(button-1);
    if (pressed) {
        p->last_click=now;
        p->mouse_button=button;
        if (button<=3) p->held_buttons |= bit;
    } else {
        p->held_buttons &= ~bit;
    }
    p->last_input=now;
}
void pet_hiss(Pet *p,double now)
{
    p->last_hiss=p->last_input=now;
    p->last_stroke=now-10;
    p->walking=0;
    p->cue=PLAY_HISS;
}
SoundCue pet_take_sound(Pet *p)
{
    SoundCue cue=p->cue;
    p->cue=QUIET;
    return cue;
}
void pet_touch(Pet *p,TouchZone zone,float dx,float dy,double now,int tapped)
{
    int entered=zone!=p->contact;
    if (entered) p->stroke_distance=0;
    p->contact=zone;
    if (zone==FLANK_TOUCH) {
        if ((entered || tapped) && now-p->last_hiss>=1.45) pet_hiss(p,now);
        return;
    }
    if (zone!=HEAD_TOUCH || now-p->last_hiss<HISS_SECONDS) return;
    if (!entered) p->stroke_distance+=bound(absv(dx)+absv(dy),0,16);
    if (!tapped && p->stroke_distance<6) return;
    p->stroke_distance=0;
    p->lean=bound(p->lean*.65f+bound(dx/7,-1,1)*.35f,-1,1);
    p->last_input=p->last_stroke=now;
    if (now>=p->next_stroke_sound) {
        p->cue=PLAY_STROKE;
        p->next_stroke_sound=now+STROKE_SECONDS+3.0;
    }
}
static void follow_pointer(Pet *p,WorkArea a,int px,int py,float dt,int hold)
{
    float s=p->dpi_scale;
    float w=PET_WIDTH*s,h=PET_HEIGHT*s;
    int under_pet=px>=p->x && px<p->x+w && py>=p->y && py<p->y+h;
    if (!p->following || hold || under_pet || p->now<p->hold_position_until || p->now-p->last_hiss<HISS_SECONDS) {
        p->walking=0;
        return;
    }
    /* Aim for the near side, leaving the actual cursor free for clicking. */
    float pad=17*s;
    int left_room=px-a.left > a.right-px;
    float tx=left_room ? px-w-pad : px+pad;
    float ty=py-h*.76f;
    tx=bound(tx,(float)a.left,(float)(a.right-w < a.left ? a.left : a.right-w));
    ty=bound(ty,(float)a.top,(float)(a.bottom-h < a.top ? a.top : a.bottom-h));
    float dx=tx-p->x,dy=ty-p->y;
    float distance=(float)sqrt((double)dx*dx+(double)dy*dy);
    float stop=(p->walking ? 3.0f : 48.0f)*s;
    if (distance<=stop) { p->walking=0; return; }
    float step=bound(distance*4.3f,68*s,380*s)*dt;
    if (step>distance) step=distance;
    p->x+=dx*step/distance;
    p->y+=dy*step/distance;
    if (absv(dx)>5*s) p->facing=dx>0 ? 1 : -1;
    p->walk_phase+=step/(12*s);
    p->walking=1;
}
void pet_step(Pet *p,double now,int px,int py,WorkArea area,int hold)
{
    float dt=bound((float)(now-p->last_tick),.001f,.08f);
    p->now=p->last_tick=now;
    float s=p->dpi_scale;
    if (p->have_pointer && (px!=p->cursor_x || py!=p->cursor_y)) {
        p->mouse_target_x=bound(p->mouse_target_x+(px-p->cursor_x)*.27f/s,-19,19);
        p->mouse_target_y=bound(p->mouse_target_y+(py-p->cursor_y)*.22f/s,-10,10);
        p->last_input=p->last_mouse=now;
    }
    p->cursor_x=px; p->cursor_y=py; p->have_pointer=1;
    if (now-p->last_mouse>.85) {
        float relax=ease(dt,1.55f);
        p->mouse_target_x*=1-relax;
        p->mouse_target_y*=1-relax;
    }
    float respond=ease(dt,22);
    p->mouse_x+=(p->mouse_target_x-p->mouse_x)*respond;
    p->mouse_y+=(p->mouse_target_y-p->mouse_y)*respond;
    float hx=(px-p->x)/s-170,hy=(py-p->y)/s-65;
    float look=ease(dt,9.5f);
    p->gaze_x+=(hx/(absv(hx)+155)-p->gaze_x)*look;
    p->gaze_y+=(hy/(absv(hy)+130)-p->gaze_y)*look;
    if (p->held_buttons) p->last_input=now;
    if (now-p->last_key>.26) p->keycap=-1;
    follow_pointer(p,area,px,py,dt,hold);
    Pose previous=p->pose;
    float target=1-soft((float)(now-p->last_stroke-.3)/.9f);
    p->affection+=(target-p->affection)*ease(dt,target>p->affection ? 9.f : 5.f);
    if (now-p->last_hiss<HISS_SECONDS) p->pose=HISSING;
    else if (p->walking) p->pose=WALKING;
    else if (now-p->last_input>=25) p->pose=ASLEEP;
    else p->pose=AT_DESK;
    if (previous!=ASLEEP && p->pose==ASLEEP) p->cue=PLAY_SLEEP;
    if (previous==ASLEEP && p->pose!=ASLEEP && p->cue==QUIET) p->cue=STOP_AUDIO;
}

/* The same textured mesh is used for the desktop window and the preview. */
typedef struct { float x,y,u,v; } Vertex;
typedef struct { uint32_t *pixels; uint8_t *touch; int w,h; float sx,sy; } Surface;
typedef struct { const uint32_t *pixels; int w,h; } Texture;
static Texture texture(int id)
{
    const SpriteInfo *s=&sprites[id];
    Texture t={(const uint32_t *)(const void *)(krema_asset_bytes+s->offset),s->width,s->height};
    return t;
}
static inline uint32_t interpolate(uint32_t a,uint32_t b,unsigned f)
{
    uint32_t rb=(((a&0xff00ffu)*(256-f)+(b&0xff00ffu)*f)>>8)&0xff00ffu;
    uint32_t ag=((((a>>8)&0xff00ffu)*(256-f)+((b>>8)&0xff00ffu)*f)>>8)&0xff00ffu;
    return rb|(ag<<8);
}
static inline uint32_t texel(const Texture *t,float u,float v)
{
    u=bound(u,0,(float)t->w-1.001f); v=bound(v,0,(float)t->h-1.001f);
    int i=(int)u,j=(int)v;
    unsigned fx=(unsigned)((u-i)*256), fy=(unsigned)((v-j)*256);
    const uint32_t *a=t->pixels+j*t->w+i;
    return interpolate(interpolate(a[0],a[1],fx),interpolate(a[t->w],a[t->w+1],fx),fy);
}
static inline uint32_t over(uint32_t s,uint32_t d)
{
    unsigned a=s>>24;
    if (a==255 || !d) return s;
    if (!a) return d;
    unsigned inv=256-a;
    uint32_t rb=(((d&0xff00ffu)*inv)>>8)&0xff00ffu;
    uint32_t ag=((((d>>8)&0xff00ffu)*inv)>>8)&0xff00ffu;
    return s+rb+(ag<<8);
}
static int ceil_i(float x) { int i=(int)x; return i+(i<x); }
/* Use source coordinates from the visible, deformed mesh so the contact
   regions follow the fur at every display scale and in both walk directions. */
static TouchZone fur_region(int sprite,float u,float v)
{
    if (sprite==WALK) {
        if (u>.69f && v<.57f) return HEAD_TOUCH;
        return u<.73f && v<.80f ? FLANK_TOUCH : OTHER_TOUCH;
    }
    if (sprite==SLEEP) {
        if (u>.50f && v>.23f && v<.85f) return HEAD_TOUCH;
        return FLANK_TOUCH;
    }
    if (v<.405f) return HEAD_TOUCH;
    if (u<.40f || u>.80f) return FLANK_TOUCH;
    return OTHER_TOUCH;
}
static void triangle(Surface *s,const Texture *t,Vertex a,Vertex b,Vertex c,int sprite)
{
    int cat=sprite<=WALK;
    int desk_depth=sprite==AWAKE || sprite==HISS;
    /* Half-open scanlines give neighbouring mesh triangles an identical seam. */
    Vertex tmp;
    if (a.y>b.y) {tmp=a;a=b;b=tmp;}
    if (b.y>c.y) {tmp=b;b=c;c=tmp;}
    if (a.y>b.y) {tmp=a;a=b;b=tmp;}
    float height=c.y-a.y;
    if (height<.0001f) return;
    int end=ceil_i(c.y-.5f),first=ceil_i(a.y-.5f);
    if (first<0)first=0;
    if (end>s->h)end=s->h;
    for (int y=first;y<end;y++) {
        float yp=y+.5f;
        Vertex e=yp<b.y ? a : b, f=yp<b.y ? b : c;
        float short_h=f.y-e.y;
        if (short_h<.0001f)continue;
        float long_t=(yp-a.y)/height,short_t=(yp-e.y)/short_h;
        float x0=a.x+long_t*(c.x-a.x), x1=e.x+short_t*(f.x-e.x);
        float u0=a.u+long_t*(c.u-a.u), u1=e.u+short_t*(f.u-e.u);
        float v0=a.v+long_t*(c.v-a.v), v1=e.v+short_t*(f.v-e.v);
        if (x0>x1) {
            float swap=x0; x0=x1; x1=swap;
            swap=u0; u0=u1; u1=swap;
            swap=v0; v0=v1; v1=swap;
        }
        int left=ceil_i(x0-.5f),right=ceil_i(x1-.5f);
        if (left<0)left=0;
        if (right>s->w)right=s->w;
        if (left>=right || x1-x0<.0001f)continue;
        float du=(u1-u0)/(x1-x0),dv=(v1-v0)/(x1-x0);
        float u=u0+(left+.5f-x0)*du,v=v0+(left+.5f-x0)*dv;
        uint32_t *destination=s->pixels+y*s->w+left;
        for (int x=left;x<right;x++,u+=du,v+=dv,destination++) {
            uint32_t color=texel(t,u,v);
            uint32_t behind=*destination;
            uint32_t result=over(color,behind);
            int visible_cat=cat && (color>>24)>=80;
            if (desk_depth && behind && color) {
                /* Accessories cover the tail; the two forepaws reach over them. */
                float xnorm=u/(t->w-1),ynorm=v/(t->h-1);
                float lp=soft((xnorm-.393f)/.055f)*(1-soft((xnorm-.567f)/.060f));
                float rp=soft((xnorm-.628f)/.054f)*(1-soft((xnorm-.811f)/.062f));
                float front=(lp+rp)*soft((ynorm-.73f)/.09f);
                result=interpolate(over(behind,color),result,(unsigned)(front*256));
                if ((behind>>24)>128 && front<.5f) visible_cat=0;
            }
            *destination=result;
            if (visible_cat && s->touch) {
                s->touch[y*s->w+x]=(uint8_t)fur_region(sprite,u/(t->w-1),v/(t->h-1));
            }
        }
    }
}
static void draw_flat(Surface *s,int id,float x,float y,float w,float h)
{
    Texture t=texture(id);
    Vertex a={x*s->sx,y*s->sy,0,0}, b={(x+w)*s->sx,y*s->sy,t.w-1.f,0};
    Vertex c={x*s->sx,(y+h)*s->sy,0,t.h-1.f},d={b.x,c.y,b.u,c.v};
    triangle(s,&t,a,b,c,id); triangle(s,&t,b,d,c,id);
}
static void light_key(Surface *s,int id,float intensity)
{
    if (id<0 || id>=KEY_COUNT || intensity<=0)return;
    const Keycap *k=&keycaps[id];
    float left=(BOARD_X+k->x)*s->sx,top=(BOARD_Y+k->y)*s->sy;
    float right=left+k->w*s->sx,bottom=top+k->h*s->sy;
    unsigned a=(unsigned)(185*intensity);
    uint32_t color=(a<<24)|((228*a/255)<<16)|((125*a/255)<<8)|(127*a/255);
    for (int y=ceil_i(top);y<bottom && y<s->h;y++) {
        for (int x=ceil_i(left);x<right && x<s->w;x++) {
            uint32_t *pixel=s->pixels+y*s->w+x;
            *pixel=over(color,*pixel);
        }
    }
}
#define COLS 48
#define ROWS 58
typedef struct {
    float head,eye,eye_mid,left_paw,right_paw,tail,chest,legs[4];
} Weight;
static Weight rig[(ROWS+1)*(COLS+1)];
static Vertex mesh[(ROWS+1)*(COLS+1)];
static int rig_ready;
static float lobe(float n,float center,float radius)
{ return 1-soft(absv(n-center)/radius); }
static void bind_rig(void)
{
    const float feet[]={.195f,.44f,.603f,.84f};
    for (int j=0;j<=ROWS;j++) for (int i=0;i<=COLS;i++) {
        float u=(float)i/COLS,v=(float)j/ROWS;
        Weight *a=&rig[j*(COLS+1)+i];
        a->head=(1-soft((v-.34f)/.25f))*soft((u-.12f)/.17f);
        float eye1=lobe(u,.523f,.072f)*lobe(v,.214f,.044f);
        float eye2=lobe(u,.681f,.073f)*lobe(v,.197f,.044f);
        a->eye=eye1+eye2;
        a->eye_mid=eye1>=eye2 ? .214f : .197f;
        float distal=soft((v-.56f)/.38f);
        a->left_paw=lobe(u,.507f,.152f)*distal;
        a->right_paw=lobe(u,.756f,.157f)*distal;
        a->tail=(1-soft((u-.10f)/.28f))*soft((v-.61f)/.19f);
        a->chest=lobe(u,.61f,.44f)*lobe(v,.57f,.58f);
        for (int k=0;k<4;k++) a->legs[k]=lobe(u,feet[k],.15f)*soft((v-.56f)/.38f);
    }
    rig_ready=1;
}
void pet_render(const Pet *p,uint32_t *pixels,uint8_t *touch,int width,int height)
{
    if (!rig_ready)bind_rig();
    Surface s={pixels,touch,width,height,(float)width/PET_WIDTH,(float)height/PET_HEIGHT};
    memset(pixels,0,(size_t)width*(size_t)height*4);
    if (touch) memset(touch,0,(size_t)width*(size_t)height);
    int id=p->pose==HISSING ? HISS : (p->pose==WALKING ? WALK : (p->pose==ASLEEP ? SLEEP : AWAKE));
    Texture t=texture(id);
    float x=44,y=4,w=216,h=229;
    float breath=sine(p->now*2.7);
    float tail=sine(p->now*2.4+.4)+.29f*sine(p->now*4.7);
    float click=p->held_buttons ? 1.f : 1-soft((float)(p->now-p->last_click)/.18f);
    float tap_age=(float)(p->now-p->last_key);
    float tap=tap_age>=0 && tap_age<.23f ? sine(3.14159265*bound((tap_age+.025f)/.255f,0,1)) : 0;
    double blink_phase=fmod(p->now+2.03,4.93);
    float blink=blink_phase<.24 ? sine(3.14159265*blink_phase/.24) : 0;
    float cuddle=p->pose==AT_DESK ? p->affection : 0;
    float nuzzle=sine(p->now*4.5);
    blink=bound(blink+cuddle*(.71f+.21f*nuzzle),0,1);
    float gait=sine(p->walk_phase);
    float lift1=bound(sine(p->walk_phase+1.5708),0,1),lift2=bound(-sine(p->walk_phase+1.5708),0,1);
    if (p->pose==ASLEEP) {x=26;y=111;w=224;h=143;}
    else if (p->pose==WALKING) {x=14;y=84;w=251;h=168;}
    else {
        draw_flat(&s,BOARD,BOARD_X,BOARD_Y,157,49);
        light_key(&s,p->keycap,1-soft(tap_age/.25f));
        int mouse_id=(click>.15f && p->mouse_button!=0) ? (p->mouse_button==2 ? MOUSE_RIGHT : MOUSE_LEFT) : MOUSE;
        draw_flat(&s,mouse_id,MOUSE_X+p->mouse_x,MOUSE_Y+p->mouse_y+click*1.8f,40,43-click*2.3f);
    }
    for (int j=0;j<=ROWS;j++) for (int i=0;i<=COLS;i++) {
        int idx=j*(COLS+1)+i;
        float u=(float)i/COLS,v=(float)j/ROWS;
        Weight *a=&rig[idx];
        float xx=x+u*w, yy=y+v*h;
        if (p->pose==ASLEEP) {
            xx+=(u-.50f)*2.8f*breath; yy-=(1-v)*2.9f*breath;
        } else if (p->pose==WALKING) {
            float shift=a->legs[0]-a->legs[1]-a->legs[2]+a->legs[3];
            xx+=gait*shift*13.f+(1-v)*gait*1.6f;
            yy-=5.8f*((a->legs[0]+a->legs[3])*lift1+(a->legs[1]+a->legs[2])*lift2);
            yy+=1.8f*sine(2*p->walk_phase)*(1-v*.55f);
            yy+=5.f*(1-soft(u/.34f))*tail;
            if (p->facing<0)xx=x+w-(xx-x);
        } else {
            float head=a->head;
            xx+=head*(p->gaze_x*(6.5f+(v-.23f)*9.f)+.72f*sine(p->now*1.31));
            yy+=head*(p->gaze_y*4.5f+p->gaze_x*(u-.60f)*7.f-breath*1.35f);
            xx+=head*cuddle*(p->lean*7.f+4.8f*nuzzle);
            yy+=head*cuddle*(-6.7f+1.9f*nuzzle+(u-.59f)*(18.f*p->lean+10.f*nuzzle));
            yy-=a->tail*cuddle*(4.9f+3.9f*nuzzle);
            xx+=a->eye*p->gaze_x*1.8f;
            yy+=a->eye*(p->gaze_y*1.1f-(v-a->eye_mid)*h*blink*1.35f);
            xx+=(u-.60f)*breath*2.6f*a->chest;
            yy-=breath*1.4f*a->chest;
            xx+=a->tail*3.1f*tail;
            yy+=a->tail*5.8f*tail;
            /* Left foreleg reaches down to keys. Right foreleg stays on its mouse. */
            xx+=a->left_paw*(-6.0f+p->tap_x*tap);
            yy+=a->left_paw*(-3.0f+12.0f*tap);
            xx+=a->right_paw*(11.5f+p->mouse_x);
            yy+=a->right_paw*(p->mouse_y+click*3.0f-1.0f);
            if (p->pose==HISSING) {
                float tense=(float)(1-soft((float)(p->now-p->last_hiss)/HISS_SECONDS));
                yy-=a->head*3.0f*tense;
            }
        }
        mesh[idx]=(Vertex){xx*s.sx,yy*s.sy,u*(t.w-1),v*(t.h-1)};
    }
    for (int j=0;j<ROWS;j++) for (int i=0;i<COLS;i++) {
        Vertex *a=&mesh[j*(COLS+1)+i],*b=a+1,*c=a+COLS+1,*d=c+1;
        triangle(&s,&t,*a,*b,*c,id); triangle(&s,&t,*b,*d,*c,id);
    }
}
