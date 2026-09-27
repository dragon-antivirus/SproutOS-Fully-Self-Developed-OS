

#include "browser_osdep.h"


#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define CLAMP(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#define COLOR_UNSET 0xFFFFFFFFu


static int G_W, G_H;
static int CONTENT_TOP, CONTENT_BOTTOM, CONTENT_LEFT, CONTENT_RIGHT;
static const int TOOLBAR_H   = 56;
static const int STATUS_H    = 22;
static const int SCROLL_W    = 12;


#define C_TOOLBAR 0xD8DCE3u
#define C_CONTENT 0xFFFFFFu
#define C_STATUS  0xE8E8E8u
#define C_TEXT    0x1A1A1Au
#define C_LINK    0x0000EEu
#define C_LINK_HO 0xEE0000u
#define C_BTN     0xF4F5F7u
#define C_BTN_BD  0x8A8F98u
#define C_ACCENT  0x3A7AFEu
#define C_SCROLL  0xB8BCC4u
#define C_SCROLL_T 0x7E8794u


typedef struct {
    uint32_t color;   
    uint32_t bg;      
    int      font;    
    int      align;   
    int      bold;    
    int      line_h;  
    int      margin;  
} style_t;


typedef struct html_node {
    int            is_text;
    char           tag[16];
    char*          text;     
    char*          href;     
    char*          src;      
    style_t        style;
    struct html_node* child;
    struct html_node* next;
} html_node_t;


typedef struct {
    int   x, y, w, h;        
    char  href[256];
} link_t;
#define MAX_LINKS 512
static link_t g_links[MAX_LINKS];
static int    g_link_count;


#define MAX_HIST 64
static char g_history[MAX_HIST][256];
static int  g_hist_count;
static int  g_hist_pos;      


static html_node_t* g_root;
static char  g_cur_url[256];
static char  g_status[160];
static char  g_addr[256];
static int   g_addr_cur;
static int   g_addr_focus;
static int   g_scroll_y, g_scroll_x;
static int   g_content_h, g_content_w;
static int   g_loading;
static int   g_stop;
static int   g_engine_idx;
static int   g_dropdown;     
static int   g_hover_link;   
static int   g_quit;
static char  g_search_url[512]; 


static const char* ENG_CN[6] = {
    "百度", "必应", "谷歌", "搜狗", "360 搜索", "DuckDuckGo"
};
static const char* ENG_URL[6] = {
    "http://www.baidu.com/s?wd=",
    "http://www.bing.com/search?q=",
    "http://www.google.com/search?q=",
    "http://www.sogou.com/web?query=",
    "http://www.so.com/s?q=",
    "http://duckduckgo.com/?q="
};


struct rctx;
static void   render_node(html_node_t* n, struct rctx* rc);
static int    bmp_load(const char* path, uint8_t** out, int* w, int* h);
static void   draw_frame(void);
static void   load_url(const char* url);
static void   set_status(const char* s);


static int m_strlen(const char* s){ int n=0; while(s&&s[n]) n++; return n; }
static int m_strcmp(const char* a, const char* b){
    if(!a||!b) return (a==b)?0:((a)?1:-1);
    while(*a && *a==*b){ a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static int m_strncmp(const char* a, const char* b, int n){
    int i=0; for(; i<n && a[i] && b[i]; i++) if(a[i]!=b[i]) return (unsigned char)a[i]-(unsigned char)b[i];
    if(i==n) return 0;
    if(!a[i] && !b[i]) return 0;
    return (unsigned char)a[i]-(unsigned char)b[i];
}
static char* m_strcpy(char* d, const char* s){ int i=0; if(!d||!s) return d; while(s[i]){ d[i]=s[i]; i++; } d[i]=0; return d; }

static char* m_strncpy(char* d, const char* s, int n){
    int i=0; if(!d) return d; if(n<=0) return d;
    for(; i<n-1 && s && s[i]; i++) d[i]=s[i];
    d[i]=0; return d;
}
static void m_memcpy(void* d, const void* s, int n){ unsigned char* p=(unsigned char*)d; const unsigned char* q=(const unsigned char*)s; int i; for(i=0;i<n;i++) p[i]=q[i]; }
static void m_memset(void* d, int v, int n){ unsigned char* p=(unsigned char*)d; int i; for(i=0;i<n;i++) p[i]=(unsigned char)v; }
static char* m_strchr(const char* s, char c){ while(*s){ if(*s==c) return (char*)s; s++; } return 0; }
static int m_atoi(const char* s){ int v=0, sign=1; if(!s) return 0; while(*s==' '||*s=='\t') s++; if(*s=='-'){ sign=-1; s++; } else if(*s=='+') s++; while(*s>='0'&&*s<='9'){ v=v*10+(*s-'0'); s++; } return sign*v; }
static char* m_strdup(const char* s){
    int n=m_strlen(s); char* p=(char*)kmalloc((size_t)n+1); if(!p) return 0;
    m_strcpy(p,s); return p;
}
static void m_tolower_buf(const char* s, char* out, int cap){
    int i=0; for(; s[i] && i<cap-1; i++){ char c=s[i]; if(c>='A'&&c<='Z') c=(char)(c+32); out[i]=c; } out[i]=0;
}


static int normalize_ws(const char* in, char* out, int cap){
    int i=0, o=0, sp=1; 
    while(in[i] && o<cap-1){
        char c=in[i];
        if(c==' '||c=='\t'||c=='\n'||c=='\r'){
            if(!sp){ out[o++]=' '; sp=1; }
            i++; continue;
        }
        out[o++]=c; sp=0; i++;
    }
    if(o>0 && out[o-1]==' ') o--; 
    out[o]=0; return o;
}


static int hexv(char c); 
static uint32_t parse_color(const char* v){
    if(!v) return COLOR_UNSET;
    if(v[0]=='#'){
        int r=0,g=0,b=0; const char* p=v+1; int n=0;
        while((p[n]>='0'&&p[n]<='9')||(p[n]>='a'&&p[n]<='f')||(p[n]>='A'&&p[n]<='F')) n++;
        if(n==3){
            r=(hexv(p[0])<<4)|hexv(p[0]);
            g=(hexv(p[1])<<4)|hexv(p[1]);
            b=(hexv(p[2])<<4)|hexv(p[2]);
            return (uint32_t)((r<<16)|(g<<8)|b);
        } else if(n>=6){
            r=(hexv(p[0])<<4)|hexv(p[1]);
            g=(hexv(p[2])<<4)|hexv(p[3]);
            b=(hexv(p[4])<<4)|hexv(p[5]);
            return (uint32_t)((r<<16)|(g<<8)|b);
        }
        return COLOR_UNSET;
    }
    
    if(m_strcmp(v,"red")==0) return 0xFF0000u;
    if(m_strcmp(v,"green")==0) return 0x008000u;
    if(m_strcmp(v,"blue")==0) return 0x0000FFu;
    if(m_strcmp(v,"black")==0) return 0x000000u;
    if(m_strcmp(v,"white")==0) return 0xFFFFFFu;
    if(m_strcmp(v,"gray")==0||m_strcmp(v,"grey")==0) return 0x808080u;
    if(m_strcmp(v,"yellow")==0) return 0xFFFF00u;
    if(m_strcmp(v,"orange")==0) return 0xFFA500u;
    if(m_strcmp(v,"purple")==0) return 0x800080u;
    if(m_strcmp(v,"cyan")==0) return 0x00FFFFu;
    if(m_strcmp(v,"pink")==0) return 0xFFC0CBu;
    if(m_strcmp(v,"brown")==0) return 0xA52A2Au;
    return COLOR_UNSET;
}

static int hexv(char c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return 0;
}



static int url_encode(const char* src, uint8_t* buf, int buf_len){
    static const char* HEX="0123456789ABCDEF";
    int o=0; int i=0;
    if(!src||!buf||buf_len<=0) return 0;
    while(src[i] && o < buf_len-1){
        unsigned char c=(unsigned char)src[i];
        int unreserved = (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')
                         || c=='-'||c=='_'||c=='.'||c=='~';
        if(unreserved){
            buf[o++]=c; i++;
        } else if(c==' '){
            buf[o++]='+'; i++;   
        } else {
            if(o+3 > buf_len-1) break;
            buf[o++]='%'; buf[o++]=HEX[(c>>4)&0xF]; buf[o++]=HEX[c&0xF]; i++;
        }
    }
    buf[o]=0; return o;
}


#define FILE_CAP (4*1024*1024)
static int read_file(const char* path, uint8_t** out_buf, int* out_len){
    int fd=vfs_open(path, VFS_RD);
    if(fd<0) return -1;
    int total=0, cap=4096;
    uint8_t* buf=(uint8_t*)kmalloc((size_t)cap);
    if(!buf){ vfs_close(fd); return -1; }
    for(;;){
        if(total+4096 > cap){
            int ncap=cap*2; if(ncap>FILE_CAP){ break; }
            uint8_t* nb=(uint8_t*)kmalloc((size_t)ncap);
            if(!nb) break;
            m_memcpy(nb,buf,total); kfree(buf); buf=nb; cap=ncap;
        }
        int r=vfs_read(fd, buf+total, 4096);
        if(r<=0) break;
        total+=r;
        if(total>=FILE_CAP) break;
    }
    vfs_close(fd);
    if(total==0){ kfree(buf); return -1; }
    *out_buf=buf; *out_len=total; return 0;
}


static void load_config(void){
    uint8_t* buf; int len;
    g_engine_idx=0; 
    if(read_file("/browser/srchcfg.txt",&buf,&len)!=0) return;
    
    int i=0; while(i<len && (buf[i]==' '||buf[i]=='\n'||buf[i]=='\r'||buf[i]=='\t')) i++;
    if(buf[i]>='0'&&buf[i]<='9'){
        int v=m_atoi((const char*)(buf+i));
        if(v>=0 && v<6) g_engine_idx=v;
    }
    kfree(buf);
}


static uint32_t rd_u32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int32_t rd_i32(const uint8_t* p){ return (int32_t)rd_u32(p); }
static uint16_t rd_u16(const uint8_t* p){ return (uint16_t)((uint32_t)p[0]|((uint32_t)p[1]<<8)); }

static int bmp_load(const char* path, uint8_t** out, int* w, int* h){
    uint8_t* fb; int len;
    if(read_file(path,&fb,&len)!=0) return 0;
    if(len<54 || fb[0]!='B' || fb[1]!='M'){ kfree(fb); return 0; }
    int off   = (int)rd_u32(fb+10);
    int width = (int)rd_i32(fb+18);
    int height= (int)rd_i32(fb+22);
    int bpp   = (int)rd_u16(fb+28);
    if(width<=0||height==0||(bpp!=24&&bpp!=32)){ kfree(fb); return 0; }
    int topdown = (height<0); if(topdown) height=-height;
    int rowbytes = ((width*bpp/8 + 3) & ~3);
    int stride = bpp/8;
    uint8_t* rgba=(uint8_t*)kmalloc((size_t)(width*height*4));
    if(!rgba){ kfree(fb); return 0; }
    int y;
    for(y=0; y<height; y++){
        int sy = topdown ? y : (height-1-y);
        const uint8_t* row = fb + off + (size_t)sy*rowbytes;
        uint8_t* dst = rgba + (size_t)y*width*4;
        int x;
        for(x=0; x<width; x++){
            const uint8_t* p = row + (size_t)x*stride;
            uint8_t b=p[0], g=p[1], r=p[2], a=(bpp==32?p[3]:255);
            dst[0]=r; dst[1]=g; dst[2]=b; dst[3]=a;
            dst+=4;
        }
    }
    kfree(fb);
    *out=rgba; *w=width; *h=height; return 1;
}


#define TAG_STACK 96

static html_node_t* node_new(int is_text){
    html_node_t* n=(html_node_t*)kmalloc(sizeof(html_node_t));
    if(!n) return 0;
    m_memset(n,0,sizeof(*n));
    n->is_text=is_text;
    n->style.color=COLOR_UNSET; n->style.bg=COLOR_UNSET;
    n->style.font=0; n->style.align=-1; n->style.bold=-1; n->style.line_h=0; n->style.margin=-1;
    return n;
}
static void node_free(html_node_t* n){
    while(n){
        html_node_t* nx=n->next;
        if(n->text) kfree(n->text);
        if(n->href) kfree(n->href);
        if(n->src)  kfree(n->src);
        if(n->child) node_free(n->child);
        kfree(n);
        n=nx;
    }
}

static int is_block_tag(const char* t){
    return m_strcmp(t,"div")==0||m_strcmp(t,"p")==0||m_strcmp(t,"h1")==0
        ||m_strcmp(t,"h2")==0||m_strcmp(t,"h3")==0||m_strcmp(t,"h4")==0
        ||m_strcmp(t,"h5")==0||m_strcmp(t,"h6")==0||m_strcmp(t,"blockquote")==0
        ||m_strcmp(t,"center")==0||m_strcmp(t,"fieldset")==0||m_strcmp(t,"ul")==0
        ||m_strcmp(t,"ol")==0||m_strcmp(t,"li")==0||m_strcmp(t,"hr")==0
        ||m_strcmp(t,"pre")==0;
}
static int is_void_tag(const char* t){
    return m_strcmp(t,"br")==0||m_strcmp(t,"img")==0||m_strcmp(t,"hr")==0
        ||m_strcmp(t,"meta")==0||m_strcmp(t,"link")==0||m_strcmp(t,"input")==0;
}
static int is_skip_tag(const char* t){ 
    return m_strcmp(t,"head")==0||m_strcmp(t,"script")==0||m_strcmp(t,"style")==0
        ||m_strcmp(t,"title")==0||m_strcmp(t,"meta")==0;
}


static void parse_style_attr(const char* s, style_t* st){
    
    char buf[128]; int bi=0;
    int i=0;
    while(s[i] && bi<(int)sizeof(buf)-1){
        if(s[i]==';'||s[i]==0){
            buf[bi]=0;
            
            char* colon=m_strchr(buf,':');
            if(colon){
                *colon=0;
                char* prop=buf; char* val=colon+1;
                
                while(*prop==' ') prop++;
                char* e=val+m_strlen(val)-1; while(e>val&&*e==' '){*e=0;e--;}
                char lp[32]; m_tolower_buf(prop,lp,sizeof(lp));
                if(m_strcmp(lp,"color")==0){ st->color=parse_color(val); }
                else if(m_strcmp(lp,"background-color")==0){ st->bg=parse_color(val); }
                else if(m_strcmp(lp,"font-size")==0){
                    int v=m_atoi(val); if(v>=6&&v<=96) st->font=v;
                }
                else if(m_strcmp(lp,"text-align")==0){
                    if(m_strcmp(val,"center")==0) st->align=1;
                    else if(m_strcmp(val,"right")==0) st->align=2;
                    else st->align=0;
                }
                else if(m_strcmp(lp,"font-weight")==0){
                    if(m_strcmp(val,"bold")==0||m_strcmp(val,"700")==0||m_strcmp(val,"600")==0) st->bold=1; else st->bold=0;
                }
                else if(m_strcmp(lp,"line-height")==0){ int v=m_atoi(val); if(v>=8) st->line_h=v; }
                else if(m_strcmp(lp,"margin")==0||m_strcmp(lp,"margin-left")==0){ int v=m_atoi(val); if(v>=0) st->margin=v; }
            }
            bi=0;
            if(s[i]==0) break;
        } else { buf[bi++]=s[i]; }
        i++;
    }
    buf[bi]=0; 
}


static int read_attr_token(const char* p, char* out, int cap, int is_val){
    int i=0, o=0; char q=0;
    if(is_val && (*p=='"'||*p=='\'')){ q=*p; p++; }
    while(p[i]){
        char c=p[i];
        if(q){ if(c==q) { i++; break; } }
        else { if(c==' '||c=='\t'||c=='\n'||c=='>'||c=='/'||c=='=') break; }
        if(o<cap-1) out[o++]=c;
        i++;
    }
    out[o]=0; return i;
}

static void apply_attr(html_node_t* n, const char* name, const char* val){
    char ln[32]; m_tolower_buf(name,ln,sizeof(ln));
    if(m_strcmp(ln,"href")==0){ if(n->href) kfree(n->href); n->href=m_strdup(val); }
    else if(m_strcmp(ln,"src")==0){ if(n->src) kfree(n->src); n->src=m_strdup(val); }
    else if(m_strcmp(ln,"style")==0){ parse_style_attr(val,&n->style); }
    else if(m_strcmp(ln,"color")==0){ n->style.color=parse_color(val); }
    else if(m_strcmp(ln,"align")==0){
        if(m_strcmp(val,"center")==0) n->style.align=1;
        else if(m_strcmp(val,"right")==0) n->style.align=2;
        else n->style.align=0;
    }
    else if(m_strcmp(ln,"size")==0){ int v=m_atoi(val); if(v>=1&&v<=7) n->style.font=12+v*2; }
}


static int html_parse(const char* html, html_node_t** out_root){
    html_node_t* root=node_new(0);
    if(!root) return -1;
    m_strcpy(root->tag,"html");
    html_node_t* stack[TAG_STACK];
    int sp=0;
    stack[sp++]=root;
    int i=0, guard=0;
    const int GUARD=200000; 
    while(html[i] && guard++<GUARD){
        if(html[i]=='<'){
            if(html[i+1]=='!' || (html[i+1]=='?' )){
                
                int j=i+1; while(html[j] && html[j]!='>') j++;
                i=(html[j]=='>')?j+1:j;
                continue;
            }
            if(html[i+1]=='/'){
                
                int j=i+2; char tname[32]; int ti=0;
                while(html[j] && html[j]!='>' && ti<31){ if(html[j]==' '){j++;continue;} tname[ti++]=html[j++]; }
                tname[ti]=0; if(html[j]=='>') j++;
                
                int k=sp-1; int found=-1;
                for(; k>=0; k--){ char lt[32]; m_tolower_buf(stack[k]->tag,lt,sizeof(lt)); if(m_strcmp(lt,tname)==0){ found=k; break; } }
                if(found>=0){ sp=found; } 
                i=j; continue;
            }
            
            int j=i+1; char tname[32]; int ti=0;
            while(html[j] && html[j]!=' ' && html[j]!='>' && html[j]!='/' && html[j]!='\t' && ti<31){ tname[ti++]=html[j++]; }
            tname[ti]=0;
            html_node_t* n=node_new(0);
            if(!n){ i=j; continue; }
            m_tolower_buf(tname,n->tag,sizeof(n->tag));
            
            while(html[j] && html[j]!='>'){
                while(html[j]==' '||html[j]=='\t'||html[j]=='\n'||html[j]=='\r') j++;
                if(html[j]=='>'||html[j]=='/') break;
                char an[32]; int ai=0;
                while(html[j] && html[j]!='=' && html[j]!=' ' && html[j]!='>' && html[j]!='/' && html[j]!='\t' && ai<31){ an[ai++]=html[j++]; }
                an[ai]=0;
                if(html[j]=='='){
                    j++;
                    while(html[j]==' '||html[j]=='\t') j++;
                    char av[256]; int used=read_attr_token(html+j, av, sizeof(av), 1);
                    apply_attr(n, an, av);
                    j+=used;
                }
            }
            if(html[j]=='>') j++;
            
            html_node_t* top=stack[sp-1];
            n->next=top->child; top->child=n;
            if(!is_void_tag(n->tag)){
                if(sp<TAG_STACK) stack[sp++]=n;
            }
            i=j; continue;
        } else {
            
            int j=i; char txt[1024]; int ti=0;
            while(html[j] && html[j]!='<' && ti<1023){ txt[ti++]=html[j++]; }
            txt[ti]=0;
            char norm[1024];
            if(normalize_ws(txt,norm,sizeof(norm))>0){
                html_node_t* t=node_new(1);
                if(t){ t->text=m_strdup(norm); t->next=stack[sp-1]->child; stack[sp-1]->child=t; }
            }
            i=j; continue;
        }
    }
    
    *out_root=root;
    return 0;
}


typedef struct rctx {
    int x, y;          
    int draw;          
    int max_y, max_x;  
    style_t cur;       
    int scroll_y, scroll_x;
    int in_link;       
    int link_idx;      
    const char* link_href;
    int lx, ly, lw, lh;
} rctx_t;

static int glyph_advance(const char* s){
    unsigned char c=(unsigned char)s[0];
    if(c<0x80) return fb_ascii_w();
    return fb_cn_w(); 
}
static int utf8_len(const char* s){
    unsigned char c=(unsigned char)s[0];
    if(c<0x80) return 1;
    if((c&0xE0)==0xC0) return 2;
    if((c&0xF0)==0xE0) return 3;
    if((c&0xF8)==0xF0) return 4;
    return 1;
}

static int eff_line_h(style_t* st){ return st->line_h? st->line_h : fb_line_h(); }


static void merge_style(style_t parent, style_t node, style_t* out){
    *out=parent;
    if(node.color!=COLOR_UNSET) out->color=node.color;
    if(node.bg!=COLOR_UNSET) out->bg=node.bg;
    if(node.font) out->font=node.font;
    if(node.align>=0) out->align=node.align;
    if(node.bold>=0) out->bold=node.bold;
    if(node.line_h) out->line_h=node.line_h;
    if(node.margin>=0) out->margin=node.margin;
}


static void draw_glyphs(rctx_t* rc, const char* s, uint32_t color, int bold){
    int i=0; int lh=eff_line_h(&rc->cur);
    int x0 = CONTENT_LEFT + (rc->cur.margin>=0?rc->cur.margin:0);
    while(s[i]){
        int adv=glyph_advance(s+i);
        int glen=utf8_len(s+i);
        
        if(rc->x + adv > CONTENT_RIGHT && rc->x > x0){
            rc->x = x0; rc->y += lh;
        }
        int sx = rc->x - rc->scroll_x;
        int sy = rc->y - rc->scroll_y;
        if(rc->draw){
            int visible = (sy+ lh >= CONTENT_TOP) && (sy <= CONTENT_BOTTOM) && (sx+adv >= CONTENT_LEFT) && (sx <= CONTENT_RIGHT);
            if(visible){
                int k;
                for(k=0;k<glen;k++){
                    char c=s[i+k];
                    if((unsigned char)c < 0x80){
                        if(c!=' ') fb_draw_ascii(sx, sy, c, color);
                    } else {
                        char tmp[5]; int t; for(t=0;t<glen;t++) tmp[t]=s[i+t]; tmp[glen]=0;
                        fb_draw_cn(sx, sy, tmp, color);
                        break;
                    }
                }
                if(bold){ 
                    if((unsigned char)s[i] < 0x80 && s[i]!=' ')
                        fb_draw_ascii(sx+1, sy, s[i], color);
                }
            }
        }
        
        if(rc->in_link){
            if(rc->lx<0){ rc->lx=rc->x; rc->ly=rc->y; rc->lw=adv; rc->lh=lh; }
            else { rc->lx=MIN(rc->lx,rc->x); rc->ly=MIN(rc->ly,rc->y);
                   rc->lw=MAX(rc->lw, rc->x+adv - rc->lx); rc->lh=MAX(rc->lh, rc->y+lh - rc->ly); }
        }
        rc->max_x=MAX(rc->max_x, rc->x+adv);
        rc->max_y=MAX(rc->max_y, rc->y+lh);
        rc->x += adv;
        i+=glen;
    }
}


static void render_node(html_node_t* n, rctx_t* rc){
    while(n){
        if(n->is_text){
            uint32_t col = (rc->cur.color!=COLOR_UNSET)? rc->cur.color : C_TEXT;
            if(rc->in_link) col = (rc->link_idx==g_hover_link)? C_LINK_HO : C_LINK;
            draw_glyphs(rc, n->text, col, (rc->cur.bold==1));
            n=n->next; continue;
        }
        char tag[16]; m_tolower_buf(n->tag, tag, sizeof(tag));
        if(is_skip_tag(tag)){ n=n->next; continue; }

        if(m_strcmp(tag,"br")==0){
            rc->x = CONTENT_LEFT + (rc->cur.margin>=0?rc->cur.margin:0);
            rc->y += eff_line_h(&rc->cur);
            n=n->next; continue;
        }
        if(m_strcmp(tag,"hr")==0){
            rc->y += 4;
            if(rc->draw){
                int sy=rc->y - rc->scroll_y;
                if(sy>=CONTENT_TOP && sy<=CONTENT_BOTTOM)
                    fb_fill_rect(CONTENT_LEFT, sy, CONTENT_RIGHT-CONTENT_LEFT, 2, C_BTN_BD);
            }
            rc->y += 10; n=n->next; continue;
        }
        if(m_strcmp(tag,"img")==0){
            int iw=0, ih=0; uint8_t* px=0;
            int ok = n->src ? bmp_load(n->src,&px,&iw,&ih) : 0;
            if(!ok){
                
                int pw=120, ph=90;
                if(rc->draw){
                    int sx=rc->x-rc->scroll_x, sy=rc->y-rc->scroll_y;
                    if(sy+ph>=CONTENT_TOP && sy<=CONTENT_BOTTOM){
                        fb_fill_rect(sx,sy,pw,ph,0xCCCCCCu);
                        fb_draw_rect(sx,sy,pw,ph,0x888888u);
                        const char* ph_txt="图";
                        fb_draw_cn(sx+pw/2-fb_cn_w()/2, sy+ph/2-fb_line_h()/2, ph_txt, 0x555555u);
                    }
                }
                rc->max_x=MAX(rc->max_x, rc->x+pw);
                rc->max_y=MAX(rc->max_y, rc->y+ph);
                rc->x = CONTENT_LEFT; rc->y += ph+8;
            } else {
                
                int dw=MIN(iw,400), dh=(iw>0)? (int)((long)ih*400/iw) : ih;
                if(dh>300) dh=300;
                if(rc->draw){
                    int sx=rc->x-rc->scroll_x, sy=rc->y-rc->scroll_y;
                    if(sy+dh>=CONTENT_TOP && sy<=CONTENT_BOTTOM)
                        fb_draw_rgba(sx, sy, dw, dh, px);
                }
                rc->max_x=MAX(rc->max_x, rc->x+dw);
                rc->max_y=MAX(rc->max_y, rc->y+dh);
                rc->x = CONTENT_LEFT; rc->y += dh+8;
                kfree(px);
            }
            n=n->next; continue;
        }
        if(m_strcmp(tag,"a")==0){
            int we_set=0;
            if(!rc->in_link && g_link_count<MAX_LINKS){
                rc->in_link=1; we_set=1;
                rc->link_idx=g_link_count;
                rc->link_href = n->href? n->href : "";
                rc->lx=rc->ly=rc->lw=rc->lh=-1;
            }
            style_t merged; merge_style(rc->cur, n->style, &merged);
            style_t saved=rc->cur; rc->cur=merged;
            
            render_node(n->child, rc);
            if(we_set && rc->lx>=0){
                link_t* L=&g_links[rc->link_idx];
                L->x=rc->lx; L->y=rc->ly; L->w=rc->lw; L->h=rc->lh;
                int hl=m_strlen(rc->link_href); if(hl>255) hl=255;
                m_strncpy(L->href, rc->link_href, hl<255?hl+1:255);
                g_link_count++;
            }
            if(we_set) rc->in_link=0;
            rc->cur=saved;
            n=n->next; continue;
        }

        
        if(is_block_tag(tag)){
            
            (void)eff_line_h(&rc->cur);
            rc->x = CONTENT_LEFT + (rc->cur.margin>=0?rc->cur.margin:0);
            rc->y += (m_strcmp(tag,"li")==0?2:6);

            style_t merged; merge_style(rc->cur, n->style, &merged);
            if(m_strcmp(tag,"h1")==0||m_strcmp(tag,"h2")==0||m_strcmp(tag,"h3")==0
               ||m_strcmp(tag,"h4")==0||m_strcmp(tag,"h5")==0||m_strcmp(tag,"h6")==0){
                int lvl=tag[1]-'0';
                merged.font = 28 - (lvl-1)*3; if(merged.font<14) merged.font=14;
                merged.bold = 1; merged.line_h = merged.font+8;
                rc->y += 6;
            }
            if(m_strcmp(tag,"blockquote")==0||m_strcmp(tag,"fieldset")==0){
                merged.margin = (merged.margin>=0? merged.margin:0) + 24;
            }
            if(m_strcmp(tag,"li")==0){
                
                if(rc->draw){
                    int sy=rc->y - rc->scroll_y;
                    if(sy>=CONTENT_TOP && sy<=CONTENT_BOTTOM){
                        fb_fill_rect(CONTENT_LEFT+merged.margin-12, sy+eff_line_h(&merged)/2-2, 4,4, C_TEXT);
                    }
                }
            }
            style_t saved=rc->cur; rc->cur=merged;
            rc->x = CONTENT_LEFT + (merged.margin>=0?merged.margin:0);
            render_node(n->child, rc);
            rc->cur=saved;
            rc->x = CONTENT_LEFT + (rc->cur.margin>=0?rc->cur.margin:0);
            rc->y += (m_strcmp(tag,"hr")==0?0:8);
            n=n->next; continue;
        }

        
        style_t merged; merge_style(rc->cur, n->style, &merged);
        int dy=0;
        if(m_strcmp(tag,"small")==0 && merged.font==0) merged.font=rc->cur.font? rc->cur.font-2 : 12;
        if(m_strcmp(tag,"sup")==0) dy=-6;
        if(m_strcmp(tag,"sub")==0) dy=6;
        if(m_strcmp(tag,"b")==0||m_strcmp(tag,"strong")==0) merged.bold=1;
        if(m_strcmp(tag,"i")==0||m_strcmp(tag,"em")==0){  }
        if(m_strcmp(tag,"u")==0){  }
        style_t saved=rc->cur; rc->cur=merged;
        int sy0=rc->y;
        rc->y += dy;
        render_node(n->child, rc);
        rc->y = sy0; 
        rc->cur=saved;
        n=n->next; continue;
    }
}


static void render_pass(int draw){
    rctx_t rc;
    m_memset(&rc,0,sizeof(rc));
    rc.x=CONTENT_LEFT; rc.y=CONTENT_TOP; rc.draw=draw;
    rc.max_y=CONTENT_TOP; rc.max_x=CONTENT_LEFT;
    rc.cur.color=C_TEXT; rc.cur.bg=COLOR_UNSET; rc.cur.font=16; rc.cur.align=0;
    rc.cur.bold=0; rc.cur.line_h=fb_line_h(); rc.cur.margin=0;
    rc.scroll_y=g_scroll_y; rc.scroll_x=g_scroll_x;
    rc.in_link=0; rc.lx=rc.ly=rc.lw=rc.lh=-1;
    if(draw) g_link_count=0;
    render_node(g_root, &rc);
    if(!draw){
        g_content_h = rc.max_y + 24;
        g_content_w = rc.max_x + 24;
    }
}


static void set_status(const char* s){ m_strncpy(g_status, s, (int)sizeof(g_status)-1); }

static void push_history(const char* url){
    if(g_hist_pos>=0 && m_strcmp(g_history[g_hist_pos], url)==0) return;
    g_hist_pos++;
    if(g_hist_pos>=MAX_HIST) g_hist_pos=0;
    m_strncpy(g_history[g_hist_pos], url, 255);
    g_hist_count = MIN(g_hist_count+1, MAX_HIST);
}

static void load_url(const char* url){
    if(g_loading) g_stop=1;
    g_loading=1; g_stop=0;
    m_strncpy(g_cur_url, url, 255);
    m_strncpy(g_addr, url, 255);
    g_addr_cur=m_strlen(g_addr);
    g_scroll_y=0; g_scroll_x=0;
    if(g_root){ node_free(g_root); g_root=0; }
    set_status("正在加载…");
    if(url[0]!='/'){

        if(m_strncmp(url,"http://",7)==0 || m_strncmp(url,"https://",8)==0){
            uint8_t* rbuf; int rlen;
            if(net_http_get(url,&rbuf,&rlen)==0 && rbuf && rlen>0){
                uint8_t* term=(uint8_t*)kmalloc((size_t)rlen+1);
                if(term){
                    m_memcpy(term,rbuf,(uint32_t)rlen); term[rlen]=0;
                    kfree(rbuf);
                    html_node_t* root=0;
                    int prc=html_parse((const char*)term, &root);
                    kfree(term);
                    if(prc!=0 || !root){
                        set_status("页面解析失败（已尽量容错渲染）");
                        g_root=root; g_loading=0; return;
                    }
                    g_root=root;
                    set_status("加载完成");
                    push_history(url);
                    g_loading=0; return;
                }
                kfree(rbuf);
                set_status("内存不足");
            } else {
                set_status("网络请求失败（无法连接或DNS解析失败）");
            }
            g_loading=0; return;
        } else {
            uint8_t enc[512];
            int n = url_encode(g_addr, enc, (int)sizeof(enc));
            m_strncpy(g_search_url, ENG_URL[g_engine_idx], 400);
            int base=m_strlen(g_search_url), j;
            for(j=0; j<n && base+j<500; j++) g_search_url[base+j]=(char)enc[j];
            g_search_url[base+n]=0;

            uint8_t* rbuf; int rlen;
            set_status("正在搜索…");
            if(net_http_get(g_search_url,&rbuf,&rlen)==0 && rbuf && rlen>0){
                uint8_t* term=(uint8_t*)kmalloc((size_t)rlen+1);
                if(term){
                    m_memcpy(term,rbuf,(uint32_t)rlen); term[rlen]=0;
                    kfree(rbuf);
                    html_node_t* root=0;
                    int prc=html_parse((const char*)term, &root);
                    kfree(term);
                    if(prc!=0 || !root){
                        set_status("搜索结果解析失败");
                        g_root=root; g_loading=0; return;
                    }
                    g_root=root;
                    m_strncpy(g_cur_url, g_search_url, 255);
                    set_status("搜索完成");
                    push_history(g_search_url);
                    g_loading=0; return;
                }
                kfree(rbuf);
                set_status("内存不足");
            } else {
                set_status("搜索失败（网络不可用）");
            }
            g_loading=0; return;
        }
    }
    uint8_t* buf; int len;
    if(read_file(url,&buf,&len)!=0){
        set_status("文件不存在或无法读取");
        g_loading=0; return;
    }

    uint8_t* term=(uint8_t*)kmalloc((size_t)len+1);
    if(!term){ kfree(buf); set_status("内存不足"); g_loading=0; return; }
    m_memcpy(term,buf,len); term[len]=0;
    html_node_t* root=0;
    int prc=html_parse((const char*)term, &root);
    kfree(term); kfree(buf);
    if(prc!=0 || !root){
        set_status("页面解析失败（已尽量容错渲染）");
        g_root=root; g_loading=0; return;
    }
    g_root=root;
    set_status(g_stop? "已停止加载" : "加载完成");
    push_history(url);
    g_loading=0;
}


typedef struct { int x,y,w,h; } rect_t;
static rect_t btn_rect(int i){ 
    int x = 6 + i*42;
    return (rect_t){ x, 10, 38, 34 };
}
static rect_t addr_rect(void){ return (rect_t){ 222, 10, G_W-222-150, 34 }; }
static rect_t engine_rect(void){ return (rect_t){ G_W-140, 10, 130, 34 }; }
static void draw_btn_label(int i){
    static const char* LBL[5]={"⬅","➡","⟳","⌂","✕"};
    rect_t r=btn_rect(i);
    fb_fill_rect(r.x,r.y,r.w,r.h,C_BTN);
    fb_draw_rect(r.x,r.y,r.w,r.h,C_BTN_BD);
    fb_draw_cn(r.x+r.w/2-fb_cn_w()/2, r.y+r.h/2-fb_line_h()/2, LBL[i], C_TEXT);
}


static int in_rect(rect_t r, int x, int y){ return x>=r.x && x<=r.x+r.w && y>=r.y && y<=r.y+r.h; }
static int hit_link(int mx, int my){
    int wx=mx+g_scroll_x, wy=my+g_scroll_y;
    int i;
    for(i=0;i<g_link_count;i++){
        link_t* L=&g_links[i];
        if(wx>=L->x && wx<=L->x+L->w && wy>=L->y && wy<=L->y+L->h) return i;
    }
    return -1;
}

static void do_navigate(void){

    if(g_addr[0]=='/'){
        load_url(g_addr);
    } else if(m_strncmp(g_addr,"http://",7)==0 || m_strncmp(g_addr,"https://",8)==0){
        load_url(g_addr);
    } else {
        load_url(g_addr);
    }
}

static void handle_click(int mx, int my){
    int i;
    if(g_dropdown){
        
        for(i=0;i<6;i++){
            rect_t ir={ engine_rect().x, engine_rect().y+36 + i*28, engine_rect().w, 28 };
            if(in_rect(ir,mx,my)){ g_engine_idx=i; g_dropdown=0; set_status("已切换搜索引擎"); return; }
        }
        g_dropdown=0; return;
    }
    for(i=0;i<5;i++){ if(in_rect(btn_rect(i),mx,my)) break; }
    if(i<5){
        if(i==0){ if(g_hist_pos>0){ g_hist_pos--; load_url(g_history[g_hist_pos]); } }
        else if(i==1){ if(g_hist_pos<g_hist_count-1){ g_hist_pos++; load_url(g_history[g_hist_pos]); } }
        else if(i==2){ load_url(g_cur_url); }
        else if(i==3){ load_url("/browser/home.html"); }
        else if(i==4){ g_quit=1; set_status("已关闭"); }
        return;
    }
    if(in_rect(addr_rect(),mx,my)){ g_addr_focus=1; return; }
    if(in_rect(engine_rect(),mx,my)){ g_dropdown=1; return; }
    
    int li=hit_link(mx,my);
    if(li>=0){
        const char* href=g_links[li].href;
        if(m_strncmp(href,"engine:",7)==0){
            
            int e=m_atoi(href+7);
            if(e>=0 && e<6){ g_engine_idx=e; set_status("已选择引擎"); }
        } else if(m_strncmp(href,"http://",7)==0 || m_strncmp(href,"https://",8)==0){
            load_url(href);
        } else if(href[0]=='/'){
            load_url(href);
        } else {
            set_status("链接地址无效");
        }
        return;
    }
    
    if(mx >= G_W-SCROLL_W && my>=CONTENT_TOP && my<=CONTENT_BOTTOM){
        int vp=CONTENT_BOTTOM-CONTENT_TOP;
        if(g_content_h>vp){
            int span=g_content_h-vp;
            int ratio=((my-CONTENT_TOP)*(span))/(CONTENT_BOTTOM-CONTENT_TOP);
            g_scroll_y=CLAMP(ratio,0,span);
        }
        return;
    }
    
    if(my>TOOLBAR_H && my<CONTENT_BOTTOM) g_addr_focus=0;
}

static void handle_key(int key){
    if(g_addr_focus){
        int len=m_strlen(g_addr);
        if(key==KEY_ENTER){ g_addr_focus=0; do_navigate(); return; }
        if(key==KEY_BACK){ if(g_addr_cur>0){ int i; for(i=g_addr_cur;i<len;i++) g_addr[i-1]=g_addr[i]; g_addr[len-1]=0; g_addr_cur--; } return; }
        if(key==KEY_LEFT){ if(g_addr_cur>0) g_addr_cur--; return; }
        if(key==KEY_RIGHT){ if(g_addr_cur<len) g_addr_cur++; return; }
        if(key>=32 && key<0x100){ 
            if(len<255){
                int i; for(i=len;i>g_addr_cur;i--) g_addr[i]=g_addr[i-1];
                g_addr[g_addr_cur]=(char)key; g_addr[len+1]=0; g_addr_cur++;
            }
            return;
        }
        return;
    }
    
    int vp=CONTENT_BOTTOM-CONTENT_TOP;
    if(key==KEY_UP){ g_scroll_y=MAX(0,g_scroll_y-24); }
    else if(key==KEY_DOWN){ if(g_content_h>vp) g_scroll_y=MIN(g_content_h-vp, g_scroll_y+24); }
    else if(key==KEY_LEFT){ g_scroll_x=MAX(0,g_scroll_x-24); }
    else if(key==KEY_RIGHT){ if(g_content_w>(G_W)) g_scroll_x=MIN(g_content_w-(G_W), g_scroll_x+24); }
}


static void draw_frame(void){
    
    fb_fill_rect(0,0,G_W,G_H, C_TOOLBAR);
    
    render_pass(0);
    
    fb_fill_rect(CONTENT_LEFT, CONTENT_TOP, CONTENT_RIGHT-CONTENT_LEFT, CONTENT_BOTTOM-CONTENT_TOP, C_CONTENT);
    
    render_pass(1);
    
    int i;
    for(i=0;i<5;i++) draw_btn_label(i);
    
    rect_t ar=addr_rect();
    fb_fill_rect(ar.x,ar.y,ar.w,ar.h, C_CONTENT);
    fb_draw_rect(ar.x,ar.y,ar.w,ar.h, g_addr_focus?C_ACCENT:C_BTN_BD);
    
    {
        int tx=ar.x+6, ty=ar.y+ar.h/2-fb_line_h()/2;
        int cx=tx; int k=0; int cx_cursor=tx;
        while(g_addr[k] && cx<ar.x+ar.w-8){
            int ul = utf8_len(g_addr+k);
            if(ul==1){ fb_draw_ascii(cx,ty,g_addr[k],C_TEXT); cx+=fb_ascii_w(); }
            else { char tmp[5]; int t; for(t=0;t<ul;t++) tmp[t]=g_addr[k+t]; tmp[t]=0; fb_draw_cn(cx,ty,tmp,C_TEXT); cx+=fb_cn_w(); }
            if(k + ul >= g_addr_cur) cx_cursor = cx;
            k += ul;
        }
        if(g_addr_cur>=k) cx_cursor=cx; 
        if(g_addr_focus){ fb_draw_rect(cx_cursor, ty-2, 2, fb_line_h()+4, C_ACCENT); }
    }
    
    rect_t er=engine_rect();
    fb_fill_rect(er.x,er.y,er.w,er.h, C_CONTENT);
    fb_draw_rect(er.x,er.y,er.w,er.h, C_ACCENT);
    fb_draw_cn(er.x+6, er.y+er.h/2-fb_line_h()/2, ENG_CN[g_engine_idx], C_ACCENT);
    fb_draw_cn(er.x+er.w-22, er.y+er.h/2-fb_line_h()/2, "▼", C_ACCENT);
    if(g_dropdown){
        int yy=er.y+er.h+2;
        for(i=0;i<6;i++){
            fb_fill_rect(er.x, yy+i*28, er.w, 28, (i==g_engine_idx)?0xDFE9FFu:C_CONTENT);
            fb_draw_rect(er.x, yy+i*28, er.w, 28, C_BTN_BD);
            fb_draw_cn(er.x+6, yy+i*28+14-fb_line_h()/2, ENG_CN[i], C_TEXT);
        }
    }
    
    int vp=CONTENT_BOTTOM-CONTENT_TOP;
    if(g_content_h>vp){
        int maxscroll=g_content_h-vp;
        int th=MAX(24, (vp*vp)/g_content_h);   
        if(th>vp) th=vp;
        int ty=CONTENT_TOP + (maxscroll>0? (g_scroll_y*(vp-th))/maxscroll : 0);
        fb_fill_rect(G_W-SCROLL_W, CONTENT_TOP, SCROLL_W, vp, C_SCROLL);
        fb_fill_rect(G_W-SCROLL_W, ty, SCROLL_W, th, C_SCROLL_T);
    }
    if(g_content_w>G_W){
        int track_w=G_W; int tw=MAX(24, (G_W*G_W)/g_content_w);
        int maxs=g_content_w-G_W;
        int tx=CONTENT_LEFT + (maxs>0? (g_scroll_x*(G_W-tw))/maxs : 0);
        fb_fill_rect(CONTENT_LEFT, CONTENT_BOTTOM, track_w, SCROLL_W, C_SCROLL);
        fb_fill_rect(tx, CONTENT_BOTTOM, tw, SCROLL_W, C_SCROLL_T);
    }
    
    fb_fill_rect(0, CONTENT_BOTTOM, G_W, STATUS_H, C_STATUS);
    fb_draw_rect(0, CONTENT_BOTTOM, G_W, STATUS_H, C_BTN_BD);
    {
        char st[200];
        if(g_hover_link>=0){
            
            int hl=m_strlen(g_links[g_hover_link].href);
            if(hl<180) m_strncpy(st, g_links[g_hover_link].href, 179);
            else { m_strncpy(st, g_links[g_hover_link].href, 177); st[177]='.'; st[178]='.'; st[179]=0; }
        } else {
            m_strncpy(st, g_status, 159);
        }
        int sx=6, sy=CONTENT_BOTTOM+STATUS_H/2-fb_line_h()/2;
        int k=0;
        while(st[k] && sx<G_W-8){
            int ul = utf8_len(st+k);
            if(ul==1){ fb_draw_ascii(sx,sy,st[k],C_TEXT); sx+=fb_ascii_w(); }
            else { char tmp[5]; int t; for(t=0;t<ul;t++) tmp[t]=st[k+t]; tmp[t]=0; fb_draw_cn(sx,sy,tmp,C_TEXT); sx+=fb_cn_w(); }
            k += ul;
        }
    }
}


void browser_on_mouse(int x, int y, int down){
    static int prev=0;
    if(down && !prev) handle_click(x,y);
    prev=down;
}
void browser_on_key(int key){
    if(key==KEY_NONE) return;
    if(key==KEY_ESC){
        if(g_addr_focus){ g_addr_focus=0; g_dropdown=0; return; }
        if(g_dropdown){ g_dropdown=0; return; }
        g_quit=1; return;
    }
    handle_key(key);
}

int browser_done(void){ return g_quit; }

void browser_init(void){
    G_W = fb_width(); G_H = fb_height();
    CONTENT_TOP = TOOLBAR_H;
    CONTENT_BOTTOM = G_H - STATUS_H;
    CONTENT_LEFT = 0;
    CONTENT_RIGHT = G_W;

    load_config();
    m_strncpy(g_cur_url, "/browser/home.html", 255);
    m_strncpy(g_addr, "/browser/home.html", 255);
    g_addr_cur=m_strlen(g_addr);
    g_hist_count=0; g_hist_pos=-1;
    g_hover_link=-1; g_quit=0;
    load_url("/browser/home.html");
}

void browser_frame(void){
    int key;
    while(kbd_poll(&key)){ browser_on_key(key); }

    int dx=mouse_x(), dy=mouse_y(), dn=mouse_down();
    browser_on_mouse(dx,dy,dn);

    g_hover_link = hit_link(dx,dy);

    draw_frame();
    gui_draw_mouse();
    fb_present();
}

void browser_start(void){
    browser_init();
    while(!g_quit){ browser_frame(); }
    if(g_root){ node_free(g_root); g_root=0; }
}
