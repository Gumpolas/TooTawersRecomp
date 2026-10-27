/* Subtítulos SRT comunitarios sobre Bink. No cambia la lógica generada.
 * Solo se activa si existe un SRT, con reloj del HBINK (no reloj de pared)
 * y el MISMO byte de opción que lee 0xDB830: 0x4ACF98.
 * UTF-8; el texto se dibuja con fuentes de Windows, como las del port.
 */
typedef struct { unsigned start, end; wchar_t *text; } FilmSub;
static FilmSub *s_fs;
static int s_nfs, s_fs_line = -1;
static char s_fs_name[80];
static uint32_t s_fs_frames, s_fs_rate, s_fs_div;
static volatile LONG s_fs_active = -1;
static uint32_t s_fs_pixels[2][1200 * 160];
static unsigned s_fs_version;
static unsigned s_fs_generation;

static char *fs_line(char **at)
{
    char *p = *at, *e;
    if (!p || !*p) return NULL;
    e = strchr(p, '\n'); if (e) *e++ = 0;
    *at = e;
    if (p[0] && p[strlen(p)-1] == '\r') p[strlen(p)-1] = 0;
    return p;
}
static int fs_time(const char *p, unsigned *start, unsigned *end)
{
    unsigned h,m,s,ms,H,M,S,MS;
    if (sscanf(p,"%u:%u:%u,%u --> %u:%u:%u,%u",&h,&m,&s,&ms,&H,&M,&S,&MS)!=8 ||
        h>99 || H>99 || m>59 || M>59 || s>59 || S>59 || ms>999 || MS>999) return 0;
    *start=((h*60+m)*60+s)*1000+ms; *end=((H*60+M)*60+S)*1000+MS;
    return *end>*start;
}
static void fs_clear(void)
{
    int i;
    InterlockedExchange(&s_fs_active,-1);
    for(i=0;i<s_nfs;i++) free(s_fs[i].text);
    free(s_fs); s_fs=NULL; s_nfs=0; s_fs_line=-1;
}
static void fs_parse(char *data)
{
    char *at=data, *line;
    while ((line=fs_line(&at)) && s_nfs<4096) {
        unsigned start,end;
        char text[8192]; size_t usedn=0;
        if (!strstr(line,"-->")) continue;
        if (!fs_time(line,&start,&end)) continue;
        text[0]=0;
        while ((line=fs_line(&at)) && *line) {
            size_t n=strlen(line);
            if (usedn+n+2>=sizeof text) { usedn=0; break; }
            if (usedn) text[usedn++]='\n';
            memcpy(text+usedn,line,n); usedn+=n; text[usedn]=0;
        }
        if (usedn) {
            #ifdef _WIN32
            int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,NULL,0);
#else
            int n=0;   /* (1.9.9) film subtitles drawn with Windows' fonts: not on Linux / macOS yet */
#endif
            if (n>0) {
                wchar_t *w=(wchar_t *)malloc((size_t)n*sizeof *w);
                if (!w) continue;
#ifdef _WIN32
                MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,w,n);
#endif
                FilmSub *p=(FilmSub *)realloc(s_fs,(size_t)(s_nfs+1)*sizeof *s_fs);
                if(!p) {free(w);continue;} s_fs=p;
                s_fs[s_nfs++]=(FilmSub){start,end,w};
            }
        }
    }
}
static void lang_fmv_open(const char *xp, const char *name)
{
    char p[900], src[900], base[80]; size_t n; unsigned char *data;
    unsigned char hdr[36]; FILE *f;
    /* Los extras ya tienen OBST oficiales; evitar dos subtítulos a la vez. */
    static const char *extras[]={"ewint.bik","imint.bik","jrdint.bik","obint.bik","pjboint.bik","vmint.bik","makingof.bik"};
    size_t i;
    fs_clear(); s_fs_name[0]=0;
    s_fs_generation++;
    for(i=0;i<sizeof extras/sizeof extras[0];i++) if(!_stricmp(name,extras[i])) return;
    if (strlen(name)>=sizeof base || strlen(name)<5) return;
    snprintf(base,sizeof base,"%s",name); base[strlen(base)-4]=0;
    snprintf(p,sizeof p,"%s/lang/%s/fmv_subs/%s.srt",s_dir,s_sub_fallback?s_code:s_subs,base);
    if(!have(p)) return;
    data=read_all(p,&n);
    if(!data || n>8*1024*1024) {free(data);tt_dbg("[LANG] SRT inválido o demasiado grande: %s\n",p);return;}
    fs_parse((char *)data); free(data);
    if(!s_nfs) return;
    if(!host_path(xp,src,sizeof src) || !(f=fopen(src,"rb"))) {fs_clear();return;}
    n=fread(hdr,1,sizeof hdr,f); fclose(f);
    if(n<sizeof hdr || memcmp(hdr,"BIKi",4)) {fs_clear();return;}
    s_fs_frames=rd32(hdr,8); s_fs_rate=rd32(hdr,28); s_fs_div=rd32(hdr,32);
    snprintf(s_fs_name,sizeof s_fs_name,"%s",base);
    tt_dbg("[LANG] SRT comunitario no oficial %s: %d entradas (%s)\n",s_fs_name,s_nfs,p);
}
int tt_lang_srt_ready(void) { return s_nfs>0; }
unsigned tt_lang_srt_generation(void) { return s_fs_generation; }

#ifdef _WIN32
static void fs_render(int index)
{
    BITMAPINFO bi; HDC dc; HBITMAP bm; HGDIOBJ oldbm,oldfont;
    HFONT font; void *pixels=NULL; RECT r={18,4,1182,154};
    wchar_t face[80]=L"Segoe UI";
    int target=(int)((s_fs_version+1)&1),i;
    if(index<0) {InterlockedExchange(&s_fs_active,-1);return;}
    const char *code=_strnicmp(s_subs,"pal-",4)?s_subs:s_subs+4;
    if(!strcmp(code,"ja")) wcscpy(face,L"Meiryo UI");
    else if(!strcmp(code,"ko")) wcscpy(face,L"Malgun Gothic");
    else if(!strcmp(code,"zh-Hans")) wcscpy(face,L"Microsoft YaHei UI");
    else if(!strcmp(code,"zh-Hant")) wcscpy(face,L"Microsoft JhengHei UI");
    else if(!strcmp(code,"hi")) wcscpy(face,L"Nirmala UI");
    else if(!strcmp(code,"th")) wcscpy(face,L"Leelawadee UI");
    memset(&bi,0,sizeof bi);bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=1200;bi.bmiHeader.biHeight=-160;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    dc=CreateCompatibleDC(NULL); bm=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,NULL,0);
    if(!dc || !bm || !pixels) {if(bm)DeleteObject(bm);if(dc)DeleteDC(dc);return;}
    oldbm=SelectObject(dc,bm);
    memset(pixels,0,1200*160*4);
    font=CreateFontW(-38,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,face);
    oldfont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    DrawTextW(dc,s_fs[index].text,-1,&r,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX|(s_subrtl?DT_RTLREADING:0));
    GdiFlush();
    for(i=0;i<1200*160;i++) {
        uint32_t v=((uint32_t *)pixels)[i]; unsigned a=v&255;
        /* Una banda oscura translúcida sostiene la lectura sobre el vídeo. */
        s_fs_pixels[target][i]=a?((uint32_t)a<<24)|0xFFFFFFu:0x80000000u;
    }
    SelectObject(dc,oldfont);SelectObject(dc,oldbm);DeleteObject(font);DeleteObject(bm);DeleteDC(dc);
    s_fs_version++;InterlockedExchange(&s_fs_active,target);
}
#else
static void fs_render(int index) { (void)index; }
#endif
void tt_lang_srt_tick(uint32_t bink)
{
    int index=-1,i;
    if(s_nfs && bink && *(volatile uint8_t *)(xbox_GetMemoryOffset()+0x4ACF98u)) {
        const volatile uint32_t *h=(const volatile uint32_t *)(xbox_GetMemoryOffset()+bink);
        if(h[2]==s_fs_frames && h[5]==s_fs_rate && h[6]==s_fs_div && h[3]>0 && h[3]<=h[2] && h[5]) {
            uint64_t ms=(uint64_t)(h[3]-1)*h[6]*1000/h[5];
            for(i=0;i<s_nfs;i++) if(ms>=s_fs[i].start && ms<s_fs[i].end) {index=i;break;}
        }
    }
    if(index!=s_fs_line) {s_fs_line=index;fs_render(index);}
}
int tt_lang_srt_overlay(void *quads,int max,int n)
{
    typedef struct {const uint32_t *px;int w,h;unsigned ver;float x,y,dw,dh;} Quad;
    Quad *q=(Quad *)quads;
    LONG active=InterlockedCompareExchange(&s_fs_active,-1,-1);
    if(active>=0 && n<max) {
        q[n].px=s_fs_pixels[active];q[n].w=1200;q[n].h=160;q[n].ver=s_fs_version;
        q[n].x=20;q[n].y=388;q[n].dw=600;q[n].dh=80;n++;
    }
    return n;
}
