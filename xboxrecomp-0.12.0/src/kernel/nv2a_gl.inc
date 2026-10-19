/*
 * nv2a_gl.inc -- OpenGL 3.3 backend for the pushbuffer executor (POSIX hosts).
 *
 * The same design as the Direct3D 11 backend (nv2a_d3d11.inc), call for call,
 * so the two draw the same picture: decoded state from nv2a_sw3d.inc, vertex
 * programs on the CPU or as generated shaders, register combiners interpreted
 * by a general fragment shader until a specialized one for the program is
 * ready, every colour and zeta surface the title draws into kept as a GL
 * texture found again by its guest address.
 *
 * Conventions that keep it identical to D3D:
 *   - Images are stored with their first row (the guest's top) at texel row
 *     0, and offscreen drawing puts window row y at framebuffer row y (the
 *     vertex shaders map window y to clip y without D3D's flip). Viewports,
 *     scissors, copies and read-backs then use the D3D numbers unchanged;
 *     only the final picture is flipped onto the window.
 *   - With that, a triangle D3D calls clockwise has positive area in GL's
 *     window coordinates: D3D "front = clockwise" is glFrontFace(GL_CCW).
 *   - Clip z: D3D keeps 0..w, GL -w..w: the shaders emit 2z - w, so depth is
 *     the same z/w.
 *   - The NV2A's blend, compare and stencil enums are OpenGL's own.
 *
 * The context is made by the window thread (fb_present.c) and made current
 * here, on the executor's thread, which is the only one that calls GL.
 */
#include <SDL.h>
#include <SDL_opengl.h>
#include "../video/gfx_menu.h"

extern void *xbox_FramebufferHwnd(void);
extern void *xbox_FramebufferGLContext(int ms);
extern void xbox_FramebufferSetGpuOwned(int on);

#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

/* ---------------- GL entry points (through SDL, no link-time libGL) ------ */

#define GLFUNCS(X) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, Clear, (GLbitfield)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, ClearDepth, (GLdouble)) \
    X(void, ClearStencil, (GLint)) \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, DepthMask, (GLboolean)) \
    X(void, StencilMask, (GLuint)) \
    X(void, DepthFunc, (GLenum)) \
    X(void, StencilFuncSeparate, (GLenum, GLenum, GLint, GLuint)) \
    X(void, StencilOpSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, BlendEquationSeparate, (GLenum, GLenum)) \
    X(void, BlendColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, CullFace, (GLenum)) \
    X(void, FrontFace, (GLenum)) \
    X(GLenum, GetError, (void)) \
    X(void, GetIntegerv, (GLenum, GLint *)) \
    X(void, GetFloatv, (GLenum, GLfloat *)) \
    X(const GLubyte *, GetString, (GLenum)) \
    X(const GLubyte *, GetStringi, (GLenum, GLuint)) \
    X(void, Finish, (void)) \
    X(void, Flush, (void)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void, ReadBuffer, (GLenum)) \
    X(void, DrawBuffer, (GLenum)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, DeleteTextures, (GLsizei, const GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, CompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, GenerateMipmap, (GLenum)) \
    X(void, GenSamplers, (GLsizei, GLuint *)) \
    X(void, DeleteSamplers, (GLsizei, const GLuint *)) \
    X(void, BindSampler, (GLuint, GLuint)) \
    X(void, SamplerParameteri, (GLuint, GLenum, GLint)) \
    X(void, SamplerParameterf, (GLuint, GLenum, GLfloat)) \
    X(void, GenFramebuffers, (GLsizei, GLuint *)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, GenBuffers, (GLsizei, GLuint *)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void *, MapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield)) \
    X(GLboolean, UnmapBuffer, (GLenum)) \
    X(void, BindBufferRange, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr)) \
    X(void, GenVertexArrays, (GLsizei, GLuint *)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, DisableVertexAttribArray, (GLuint)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, DeleteProgram, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(GLuint, GetUniformBlockIndex, (GLuint, const GLchar *)) \
    X(void, UniformBlockBinding, (GLuint, GLuint, GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, DrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void *, GLint)) \
    X(void, GenRenderbuffers, (GLsizei, GLuint *)) \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint *)) \
    X(void, BindRenderbuffer, (GLenum, GLuint)) \
    X(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))

#define GLX_DECL(ret, name, args) static ret (APIENTRY *p_gl##name) args;
GLFUNCS(GLX_DECL)
#undef GLX_DECL
static void (APIENTRY *p_glMaxShaderCompilerThreadsKHR)(GLuint);

#define glViewport p_glViewport
#define glScissor p_glScissor
#define glEnable p_glEnable
#define glDisable p_glDisable
#define glClear p_glClear
#define glClearColor p_glClearColor
#define glClearDepth p_glClearDepth
#define glClearStencil p_glClearStencil
#define glColorMask p_glColorMask
#define glDepthMask p_glDepthMask
#define glStencilMask p_glStencilMask
#define glDepthFunc p_glDepthFunc
#define glStencilFuncSeparate p_glStencilFuncSeparate
#define glStencilOpSeparate p_glStencilOpSeparate
#define glBlendFuncSeparate p_glBlendFuncSeparate
#define glBlendEquationSeparate p_glBlendEquationSeparate
#define glBlendColor p_glBlendColor
#define glCullFace p_glCullFace
#define glFrontFace p_glFrontFace
#define glGetError p_glGetError
#define glGetIntegerv p_glGetIntegerv
#define glGetFloatv p_glGetFloatv
#define glGetString p_glGetString
#define glGetStringi p_glGetStringi
#define glFinish p_glFinish
#define glFlush p_glFlush
#define glPixelStorei p_glPixelStorei
#define glReadPixels p_glReadPixels
#define glReadBuffer p_glReadBuffer
#define glDrawBuffer p_glDrawBuffer
#define glGenTextures p_glGenTextures
#define glDeleteTextures p_glDeleteTextures
#define glBindTexture p_glBindTexture
#define glTexImage2D p_glTexImage2D
#define glTexSubImage2D p_glTexSubImage2D
#define glCompressedTexImage2D p_glCompressedTexImage2D
#define glTexParameteri p_glTexParameteri
#define glActiveTexture p_glActiveTexture
#define glGenerateMipmap p_glGenerateMipmap
#define glGenSamplers p_glGenSamplers
#define glDeleteSamplers p_glDeleteSamplers
#define glBindSampler p_glBindSampler
#define glSamplerParameteri p_glSamplerParameteri
#define glSamplerParameterf p_glSamplerParameterf
#define glGenFramebuffers p_glGenFramebuffers
#define glDeleteFramebuffers p_glDeleteFramebuffers
#define glBindFramebuffer p_glBindFramebuffer
#define glFramebufferTexture2D p_glFramebufferTexture2D
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glBlitFramebuffer p_glBlitFramebuffer
#define glGenBuffers p_glGenBuffers
#define glDeleteBuffers p_glDeleteBuffers
#define glBindBuffer p_glBindBuffer
#define glBufferData p_glBufferData
#define glMapBufferRange p_glMapBufferRange
#define glUnmapBuffer p_glUnmapBuffer
#define glBindBufferRange p_glBindBufferRange
#define glGenVertexArrays p_glGenVertexArrays
#define glBindVertexArray p_glBindVertexArray
#define glVertexAttribPointer p_glVertexAttribPointer
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glDisableVertexAttribArray p_glDisableVertexAttribArray
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glDeleteShader p_glDeleteShader
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glLinkProgram p_glLinkProgram
#define glGetProgramiv p_glGetProgramiv
#define glGetProgramInfoLog p_glGetProgramInfoLog
#define glDeleteProgram p_glDeleteProgram
#define glUseProgram p_glUseProgram
#define glGetUniformBlockIndex p_glGetUniformBlockIndex
#define glUniformBlockBinding p_glUniformBlockBinding
#define glGetUniformLocation p_glGetUniformLocation
#define glUniform1i p_glUniform1i
#define glUniform1f p_glUniform1f
#define glDrawArrays p_glDrawArrays
#define glDrawElementsBaseVertex p_glDrawElementsBaseVertex
#define glGenRenderbuffers p_glGenRenderbuffers
#define glDeleteRenderbuffers p_glDeleteRenderbuffers
#define glBindRenderbuffer p_glBindRenderbuffer
#define glRenderbufferStorageMultisample p_glRenderbufferStorageMultisample
#define glFramebufferRenderbuffer p_glFramebufferRenderbuffer

static int gl_load(void)
{
#define GLX_LOAD(ret, name, args) \
    if (!(*(void **)&p_gl##name = SDL_GL_GetProcAddress("gl" #name))) { \
        fprintf(stderr, "  [GL] the driver lacks gl" #name "\n"); return 0; }
    GLFUNCS(GLX_LOAD)
#undef GLX_LOAD
    return 1;
}

static int gl_has_ext(const char *name)
{
    GLint n = 0, i;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (i = 0; i < n; i++) {
        const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && !strcmp(e, name)) return 1;
    }
    return 0;
}

/* ---------------- shared state ---------------- */

volatile uint64_t g_ws_cam_tick;
static int s_gpu_mode = -1;             /* -1 undecided, 0 software, 1 OpenGL */
static SDL_Window *g_win;
static SDL_GLContext g_glc;
static int g_sc_w, g_sc_h;              /* window drawable size */

/* Internal resolution and picture shape: as in nv2a_d3d11.inc. */
static int g_rs = 1;
static float g_rsf = 1.0f;   /* the scale itself (4.5: 2160 lines) */
extern float xbox_GfxScaleF(void);
static float g_rsx = 1.0f, g_rsy = 1.0f;
static float g_aspect = 4.0f / 3.0f;
/* (1.9) the frame-sized targets within the largest texture the card takes
 * (16384 wide): 12x at 32:9 would be 20480 -- both scales come down */
static int gl_max_tex(void)
{
    static GLint m;
    if (!m) { glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m); if (m < 2048) m = 16384; }
    return (int)m;
}
static void rs_fit(void)
{
    float mx = (float)gl_max_tex(), k = 1.0f;
    if (640.0f * g_rsx > mx) k = mx / (640.0f * g_rsx);
    if (480.0f * g_rsy * k > mx) k = mx / (480.0f * g_rsy);
    if (k < 1.0f) {
        g_rsx *= k; g_rsy *= k;
        fprintf(stderr, "  [GPU] internal resolution held to %.0f x %.0f (the largest texture: %.0f)\n", 640.0f * g_rsx, 480.0f * g_rsy, mx);
        fflush(stderr);
    }
}
static int g_frame_3d, g_wide_prev;
static unsigned g_present_n;
#define GS_SCALED(w, h) ((w) >= 320 && (h) >= 200)
#define RSX_(v, w, h) ((uint32_t)((float)(v) * (GS_SCALED(w, h) ? g_rsx : 1.0f) + 0.5f))
#define RSY_(v, w, h) ((uint32_t)((float)(v) * (GS_SCALED(w, h) ? g_rsy : 1.0f) + 0.5f))
#define SX(s, v) ((uint32_t)((float)(v) * (float)(s)->pw / (float)(s)->w + 0.5f))
#define SY(s, v) ((uint32_t)((float)(v) * (float)(s)->ph / (float)(s)->h + 0.5f))

static int g_aniso = -1, g_mips = -1;
static float g_max_aniso = 1.0f;
static long g_gfx_seen = 1;
static int g_aa_on = -1, g_vsync_on = -1, g_stretch_on = -1, g_hwvp_user = 1;
/* Menus fill the screen: a frame that drew no 3D (menus, the upgrade and
 * results screens, loading screens) is drawn inside the TV safe area, the
 * full picture scaled by 593/640 about its middle with black round it; it
 * is shown that much larger, so the art reaches the screen's edges and the
 * margin falls outside. Movies and play are left as they are.
 * RECOMP_MENU_FILL=0: as before. */
static float menu_fill_zoom(int menu2d)
{
    /* (the F10 menu's "Menus"; [Display] menu_fill, RECOMP_MENU_FILL) */
    return (xbox_GfxGet(GFX_MENUFILL) == 0 && menu2d) ? 640.0f / 593.0f : 1.0f;
}

/* Movies in widescreen (1.7.8): with a wide picture chosen ([Display]
 * aspect wider than 4:3), a movie fills the window's width at its own shape,
 * the top and bottom cut -- the game's films are letterboxed inside 4:3
 * (about 1.85:1), so at 16:9 the picture fills the screen with nothing of it
 * lost and nothing stretched. Off: the films looked zoomed in; as before
 * (4:3, or stretched with "Stretch to fill") unless RECOMP_MOVIE_FILL=1. */
static int movie_fill(int movie, float ww, float wh, float *pw, float *ph)
{
    /* (the F10 menu's "Movies in widescreen", off by default: the films look
     * a little close; [Display] movie_fill, RECOMP_MOVIE_FILL) */
    if (xbox_GfxGet(GFX_MOVIEFILL) != 1 || !movie || g_aspect <= 1.34f || ww / wh <= 4.0f / 3.0f + 0.01f) return 0;
    *pw = ww; *ph = ww * 0.75f;
    return 1;
}

static int g_vsync_applied = -2;
static void gpu_filter_opts(void)
{
    if (g_aniso < 0) {
        const char *a = getenv("RECOMP_ANISO"), *m = getenv("RECOMP_MIPMAPS");
        g_aniso = a ? atoi(a) : 1;
        if (g_aniso < 1) g_aniso = 1;
        if (g_aniso > 16) g_aniso = 16;
        g_mips = !(m && m[0] == '0');
        fprintf(stderr, "  [GL] texture filtering: mipmaps %s, anisotropic x%d\n", g_mips ? "on" : "off", g_aniso);
    }
}

static float g_post[4] = { 1, 0, 1, 0 };   /* brightness (gamma), sharpening, saturation */

/* Uniform blocks: every program uses the same binding points. */
enum { UB_VSC = 0, UB_PSC = 1, UB_CLC = 2, UB_POST = 3, UB_VPC = 4 };

/* ---------------- streaming buffers ---------------- */

/* One ring per use: appended to without synchronisation, given a fresh
 * store when it wraps -- the GL counterpart of D3D's NO_OVERWRITE/DISCARD
 * scheme in nv2a_d3d11.inc. Returns the byte offset, or 0xFFFFFFFF. */
typedef struct { GLuint buf; GLenum target; uint32_t cap, pos; } GRing;
static GRing g_rvb = { 0, GL_ARRAY_BUFFER, 0, 0 }, g_rvbx = { 0, GL_ARRAY_BUFFER, 0, 0 };
static GRing g_rib = { 0, GL_ELEMENT_ARRAY_BUFFER, 0, 0 }, g_rub = { 0, GL_UNIFORM_BUFFER, 0, 0 };
static GLint g_ubo_align = 256;
static uint32_t gpu_ring(GRing *r, const void *data, uint32_t bytes, uint32_t align)
{
    uint32_t at;
    void *p;
    if (!r->buf || r->cap < bytes) {
        if (!r->buf) glGenBuffers(1, &r->buf);
        r->cap = bytes < (8u << 20) ? (8u << 20) : bytes * 2;
        glBindBuffer(r->target, r->buf);
        glBufferData(r->target, r->cap, NULL, GL_STREAM_DRAW);
        r->pos = r->cap;
    } else glBindBuffer(r->target, r->buf);
    if (!align) align = 1;
    at = (r->pos + align - 1) / align * align;
    if (at + bytes > r->cap) {
        glBufferData(r->target, r->cap, NULL, GL_STREAM_DRAW);   /* a fresh store */
        at = 0;
    }
    p = glMapBufferRange(r->target, at, bytes, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
    if (!p) return 0xFFFFFFFFu;
    memcpy(p, data, bytes);
    glUnmapBuffer(r->target);
    r->pos = at + bytes;
    return at;
}

/* a uniform block's data, bound at its binding point */
static void gpu_ub(int binding, const void *data, uint32_t bytes)
{
    uint32_t at = gpu_ring(&g_rub, data, bytes, (uint32_t)g_ubo_align);
    if (at != 0xFFFFFFFFu) glBindBufferRange(GL_UNIFORM_BUFFER, (GLuint)binding, g_rub.buf, at, bytes);
}

/* ---------------- surfaces ---------------- */

typedef struct {
    uint32_t base, w, h, is_depth;
    uint32_t pw, ph;                    /* allocated size: w, h times the scale */
    GLuint tex;
    uint32_t mem_sum;
    uint32_t pitch;
    uint64_t used;
    uint32_t synced_run;
    uint64_t wr;
    /* (1.9) MSAA: drawn into a multisampled renderbuffer (mrb), made into
     * the texture before anything reads it (ms_new); the texture written
     * another way is drawn into the renderbuffer first (ss_new) */
    GLuint mrb;
    uint8_t ms_new, ss_new;
} GSurf;
static int g_msaa = 1, g_msaa_bad;
static uint64_t g_wr_clock;
extern volatile uint32_t g_pb_runs;
#define GSURF_MAX 48
static GSurf g_surf[GSURF_MAX];
static uint64_t g_clock;

/* State shadow (what is bound), so a draw only sends what changed. Anything
 * that binds behind the draw path's back calls gpu_sh_reset. */
static struct {
    GLuint fbo; int tg;
    int vw, vh; int vp_ok;
    int sc[4]; int sc_ok;
    uint64_t blend; float bf[4]; int bs_ok;
    uint64_t ds[3]; int ds_ok;
    uint64_t rs; int rs_ok;
    GLuint tex[4], smp[4]; int tex_ok;
    float cbvs[4]; int cbvs_ok;
    unsigned char cbps[1152]; int cbps_ok;
    GLuint prog;
    int vao;
} g_sh;
static void gpu_sh_reset(void) { memset(&g_sh, 0, sizeof g_sh); }

static void gpu_set_fbo(GLuint fbo)
{
    if (!g_sh.tg || g_sh.fbo != fbo) { glBindFramebuffer(GL_FRAMEBUFFER, fbo); g_sh.fbo = fbo; g_sh.tg = 1; }
}

/* Framebuffer objects, one per (colour, depth) texture pair. */
#define FBO_MAX 64
static struct { GLuint col, dep, fbo; uint64_t used; } g_fbo[FBO_MAX];
static GLuint gpu_fbo(GLuint col, GLuint dep)
{
    int i, v = -1;
    for (i = 0; i < FBO_MAX; i++)
        if (g_fbo[i].fbo && g_fbo[i].col == col && g_fbo[i].dep == dep) { g_fbo[i].used = ++g_clock; return g_fbo[i].fbo; }
    for (i = 0; i < FBO_MAX; i++) {
        if (!g_fbo[i].fbo) { v = i; break; }
        if (v < 0 || g_fbo[i].used < g_fbo[v].used) v = i;
    }
    if (!g_fbo[v].fbo) glGenFramebuffers(1, &g_fbo[v].fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo[v].fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, dep, 0);
    /* a zeta-only framebuffer has no colour to draw to (GL 3.3 calls it
     * incomplete otherwise) */
    glDrawBuffer(col ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    glReadBuffer(col ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    {
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        static int said;
        if (st != GL_FRAMEBUFFER_COMPLETE && said++ < 5)
            fprintf(stderr, "  [GL] framebuffer incomplete (0x%04X), colour %u depth %u\n", st, col, dep);
    }
    g_sh.tg = 0;
    g_fbo[v].col = col; g_fbo[v].dep = dep; g_fbo[v].used = ++g_clock;
    return g_fbo[v].fbo;
}
/* a texture is going away: its framebuffers too */
static void gpu_fbo_forget(GLuint tex)
{
    int i;
    for (i = 0; i < FBO_MAX; i++)
        if (g_fbo[i].fbo && (g_fbo[i].col == tex || g_fbo[i].dep == tex)) {
            glDeleteFramebuffers(1, &g_fbo[i].fbo);
            memset(&g_fbo[i], 0, sizeof g_fbo[i]);
        }
    g_sh.tg = 0;
}

/* (1.9) framebuffers of multisampled renderbuffers */
static struct { GLuint col, dep, fbo; uint64_t used; } g_fboms[16];
static GLuint gpu_fbo_ms(GLuint col, GLuint dep)
{
    int i, v = -1;
    for (i = 0; i < 16; i++)
        if (g_fboms[i].fbo && g_fboms[i].col == col && g_fboms[i].dep == dep) { g_fboms[i].used = ++g_clock; return g_fboms[i].fbo; }
    for (i = 0; i < 16; i++) {
        if (!g_fboms[i].fbo) { v = i; break; }
        if (v < 0 || g_fboms[i].used < g_fboms[v].used) v = i;
    }
    if (!g_fboms[v].fbo) glGenFramebuffers(1, &g_fboms[v].fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fboms[v].fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, col);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, dep);
    glDrawBuffer(col ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    glReadBuffer(col ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    {
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        static int said;
        if (st != GL_FRAMEBUFFER_COMPLETE && said++ < 5)
            fprintf(stderr, "  [GL] multisampled framebuffer incomplete (0x%04X)\n", st);
    }
    g_sh.tg = 0;
    g_fboms[v].col = col; g_fboms[v].dep = dep; g_fboms[v].used = ++g_clock;
    return g_fboms[v].fbo;
}
static void gpu_fbo_ms_forget(GLuint rb)
{
    int i;
    for (i = 0; i < 16; i++)
        if (g_fboms[i].fbo && (g_fboms[i].col == rb || g_fboms[i].dep == rb)) {
            glDeleteFramebuffers(1, &g_fboms[i].fbo);
            memset(&g_fboms[i], 0, sizeof g_fboms[i]);
        }
    g_sh.tg = 0;
}

static uint32_t gpu_mem_sum(uint32_t base, uint32_t bytes)
{
    const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset() + base;
    uint32_t h = 2166136261u, i, step = bytes / 1024;
    if (step < 4) step = 4;
    for (i = 0; i + 4 <= bytes && i < 1024 * step; i += step)
        h = (h ^ *(const uint32_t *)(m + (i & ~3u))) * 16777619u;
    return h;
}

/* A texture of size w x h: colour (BGRA8) or depth-stencil. */
static GLuint gpu_tex2d(uint32_t w, uint32_t h, int depth)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (depth)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, (GLsizei)w, (GLsizei)h, 0, GL_DEPTH_STENCIL,
                     GL_UNSIGNED_INT_24_8, NULL);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
    g_sh.tex_ok = 0;
    return t;
}

/* Scratch 1x texture for scaled uploads/read-backs. */
static GLuint g_one_tex; static uint32_t g_one_w, g_one_h;
static GLuint g_present_smp, g_copy_smp;
static GLuint g_vao, g_vao_x, g_vao_empty;
static GLuint g_tex_black;                 /* what a stage reads from a target being drawn */

/* the fixed shaders' programs (gpu_init) */
static GLuint g_pr_copy, g_pr_present, g_pr_fxaa, g_pr_blit, g_pr_clear;
static GLint g_loc_ys_copy, g_loc_ys_present, g_loc_ys_fxaa, g_loc_ys_blit;

static int gpu_one(uint32_t w, uint32_t h)
{
    if (g_one_tex && g_one_w >= w && g_one_h >= h) return 1;
    if (g_one_tex) { gpu_fbo_forget(g_one_tex); glDeleteTextures(1, &g_one_tex); g_one_tex = 0; }
    if (w < g_one_w) w = g_one_w;
    if (h < g_one_h) h = g_one_h;
    g_one_tex = gpu_tex2d(w, h, 0);
    g_one_w = w; g_one_h = h;
    return g_one_tex != 0;
}

/* Draw texture src (its top-left uw x uh of a sw x sh texture) onto the
 * top-left dw x dh of framebuffer fbo, alpha included. */
static void gpu_copy_scaled(GLuint src, uint32_t uw, uint32_t uh, uint32_t sw, uint32_t sh,
                            GLuint fbo, uint32_t dw, uint32_t dh)
{
    float uvs[4];
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, (GLsizei)dw, (GLsizei)dh);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    uvs[0] = (float)uw / (float)sw; uvs[1] = (float)uh / (float)sh; uvs[2] = uvs[3] = 0;
    gpu_ub(UB_CLC, uvs, 16);
    glBindVertexArray(g_vao_empty);
    glUseProgram(g_pr_copy);
    glUniform1f(g_loc_ys_copy, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    glBindSampler(0, g_copy_smp);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    gpu_sh_reset();
}

static void gsurf_free(GSurf *s)
{
    if (s->mrb) { gpu_fbo_ms_forget(s->mrb); glDeleteRenderbuffers(1, &s->mrb); }
    if (s->tex) { gpu_fbo_forget(s->tex); glDeleteTextures(1, &s->tex); }
    memset(s, 0, sizeof *s);
    g_sh.tex_ok = 0;
}

/* Guest pixels (w x h at pitch) into a colour surface, scaled if need be. */
static void gsurf_upload(GSurf *s, const uint8_t *m, uint32_t pitch)
{
    if (s->mrb) { s->ss_new = 1; s->ms_new = 0; }   /* (the whole surface: what was drawn is replaced) */
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(pitch / 4));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (s->pw == s->w && s->ph == s->h) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s->tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)s->w, (GLsizei)s->h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, m);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        g_sh.tex_ok = 0;
        return;
    }
    if (!gpu_one(s->w, s->h)) { glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); return; }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_one_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)s->w, (GLsizei)s->h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, m);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gpu_copy_scaled(g_one_tex, s->w, s->h, g_one_w, g_one_h, gpu_fbo(s->tex, 0), s->pw, s->ph);
}

static GSurf *gsurf_find(uint32_t base, int depth)
{
    int i;
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex && g_surf[i].base == base && (depth < 0 || (int)g_surf[i].is_depth == depth))
            return &g_surf[i];
    return NULL;
}

static int gsurf_near(uint32_t off)
{
    int i;
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex && off >= g_surf[i].base && off < g_surf[i].base + g_surf[i].w * g_surf[i].h * 4)
            return 1;
    return 0;
}

/* Depth and stencil of a whole zeta texture to a value: as D3D's
 * ClearDepthStencilView -- no scissor, every bit written. */
static void gpu_clear_ds(GLuint fbo, int which, float z, int stencil)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    glClearDepth(z);
    glClearStencil(stencil);
    glClear(((which & 1) ? GL_DEPTH_BUFFER_BIT : 0) | ((which & 2) ? GL_STENCIL_BUFFER_BIT : 0));
    gpu_sh_reset();
}

/* ---- (1.9) MSAA ---- */
static void gsurf_ms_make(GSurf *s)
{
    GLint mx = 0;
    int n = g_msaa;
    glGetIntegerv(GL_MAX_SAMPLES, &mx);
    while (n > 1 && (n > mx || (uint64_t)s->pw * s->ph * (uint64_t)n * 8u > (1536ull << 20))) n >>= 1;
    if (n <= 1) return;
    while (glGetError() != GL_NO_ERROR) {}
    glGenRenderbuffers(1, &s->mrb);
    glBindRenderbuffer(GL_RENDERBUFFER, s->mrb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, s->is_depth ? GL_DEPTH24_STENCIL8 : GL_RGBA8, (GLsizei)s->pw, (GLsizei)s->ph);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (glGetError() != GL_NO_ERROR) { glDeleteRenderbuffers(1, &s->mrb); s->mrb = 0; g_msaa_bad = 1; return; }
    if (s->is_depth) {
        uint32_t zv = s_cb_scratch[0];
        gpu_clear_ds(gpu_fbo_ms(0, s->mrb), 3, (float)(zv >> 8) / 16777215.0f, (int)(zv & 0xFF));
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, gpu_fbo_ms(s->mrb, 0));
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        gpu_sh_reset();
        s->ss_new = 1;
    }
}
/* the texture made from what was drawn (before anything reads it) */
static void gsurf_resolve(GSurf *s)
{
    GLbitfield b;
    if (!s || !s->mrb || !s->ms_new) return;
    s->ms_new = 0;
    b = s->is_depth ? (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT) : GL_COLOR_BUFFER_BIT;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s->is_depth ? gpu_fbo_ms(0, s->mrb) : gpu_fbo_ms(s->mrb, 0));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s->is_depth ? gpu_fbo(0, s->tex) : gpu_fbo(s->tex, 0));
    glDisable(GL_SCISSOR_TEST);
    if (!s->is_depth) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    else { glDepthMask(GL_TRUE); glStencilMask(0xFF); }
    glBlitFramebuffer(0, 0, (GLint)s->pw, (GLint)s->ph, 0, 0, (GLint)s->pw, (GLint)s->ph, b, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gpu_sh_reset();
}
/* a surface drawn without its multisampled copy from now on */
static void gsurf_ms_drop(GSurf *s)
{
    if (!s || !s->mrb) return;
    gsurf_resolve(s);
    gpu_fbo_ms_forget(s->mrb);
    glDeleteRenderbuffers(1, &s->mrb);
    s->mrb = 0; s->ms_new = s->ss_new = 0;
}
/* the framebuffer the title's drawing goes to */
static GLuint gsurf_draw_fbo(GSurf *rt, GSurf *ds)
{
    if (rt->mrb) return gpu_fbo_ms(rt->mrb, ds ? ds->mrb : 0);
    return gpu_fbo(rt->tex, ds ? ds->tex : 0);
}

static GSurf *gsurf_get(uint32_t base, uint32_t w, uint32_t h, int depth)
{
    GSurf *s = NULL, *victim = NULL;
    int i;
    GLuint keep = 0;
    uint32_t keep_pw = 0, keep_ph = 0, keep_w = 0, keep_h = 0, tw, th;
    for (i = 0; i < GSURF_MAX; i++) {
        GSurf *c = &g_surf[i];
        /* zeta surfaces are also keyed by size: a depth buffer must be the
         * size of the colour target it is drawn with, and the title pairs one
         * zeta offset with targets of different sizes (the 128x128 lava
         * passes) */
        if (c->tex && c->base == base && (int)c->is_depth == depth && (!depth || (c->w == w && c->h == h))) { s = c; break; }
        if (!victim || (!c->tex && victim->tex) || (c->tex && victim->tex && c->used < victim->used)) victim = c;
    }
    if (s && s->w >= w && s->h >= h) { s->used = ++g_clock; return s; }
    if (s) {
        if (w < s->w) w = s->w;
        if (h < s->h) h = s->h;
        if (!depth && s->tex) {
            /* a colour surface that grows keeps what it holds */
            keep = s->tex; s->tex = 0;
            keep_pw = s->pw; keep_ph = s->ph; keep_w = s->w; keep_h = s->h;
        }
        gsurf_free(s);
    }
    else { s = victim; gsurf_free(s); }

    tw = RSX_(w, w, h); th = RSY_(h, w, h);
    s->tex = gpu_tex2d(tw, th, depth);
    if (!s->tex) { memset(s, 0, sizeof *s); return NULL; }
    if (depth) {
        /* A zeta surface starts at the title's clear value, as the software
         * z-buffer does: titles with an inverted depth range never clear
         * depth and rely on it. */
        uint32_t zv = s_cb_scratch[0];
        gpu_clear_ds(gpu_fbo(0, s->tex), 3, (float)(zv >> 8) / 16777215.0f, (int)(zv & 0xFF));
    } else {
        GLuint fbo = gpu_fbo(s->tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        if (keep) {
            /* Grown (a later copy reaching further down, as the heat haze's
             * second patch does): the old contents carry over, when both are
             * at the same scale. */
            if (keep_pw * w == tw * keep_w && keep_ph * h == th * keep_h) {
                GLuint src = gpu_fbo(keep, 0);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
                glBlitFramebuffer(0, 0, (GLint)keep_pw, (GLint)keep_ph, 0, 0, (GLint)keep_pw, (GLint)keep_ph,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
            gpu_fbo_forget(keep);
            glDeleteTextures(1, &keep);
        }
        gpu_sh_reset();
    }
    s->base = base; s->w = w; s->h = h; s->is_depth = (uint32_t)depth; s->used = ++g_clock;
    s->pw = tw; s->ph = th;
    if (g_msaa > 1) gsurf_ms_make(s);
    if (getenv("RECOMP_SURF_LOG")) {
        static int n;
        if (n++ < 200)
            fprintf(stderr, "  [SURF] new %s %08X %ux%u (fmt %08X pitch %08X, clip %u,%u %ux%u)\n", depth ? "zeta" : "colour",
                    base, w, h, s_gpu.format, s_gpu.pitch, s_gpu.clip_x, s_gpu.clip_y, s_gpu.clip_w, s_gpu.clip_h);
    }
    s->mem_sum = 0;
    if (!depth) {
        /* Start from what the guest has there: a surface the CPU filled (or
         * the software path drew before the switch) keeps its picture. */
        const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset() + base;
        uint32_t pitch = s_gpu.pitch && (s_gpu.pitch & 0xFFFF) >= w * 4 ? (s_gpu.pitch & 0xFFFF) : w * 4;
        if (dma_resolve(s_gpu.color_offset) != base) pitch = w * 4;
        gsurf_upload(s, m, pitch);
        s->pitch = pitch;
        s->mem_sum = gpu_mem_sum(base, pitch * h);
    }
    return s;
}

/* The GPU copy of a surface lives on the card; the guest's copy in memory
 * only changes when the CPU writes it -- a movie frame, a loading screen. If
 * memory has changed since the two were last in sync, the CPU drew: take its
 * picture. Checked once per pushbuffer run. */
static void gsurf_sync_from_mem(GSurf *s)
{
    uint32_t sum;
    if (!s || s->is_depth || !s->pitch) return;
    if (s->synced_run == g_pb_runs) return;
    s->synced_run = g_pb_runs;
    sum = gpu_mem_sum(s->base, s->pitch * s->h);
    if (sum == s->mem_sum) return;
    gsurf_upload(s, (const uint8_t *)xbox_GetMemoryOffset() + s->base, s->pitch);
    s->mem_sum = sum;
}

/* ---------------- textures from guest memory ---------------- */

typedef struct {
    uint32_t off, color, w, h, pitch, ver, hash;
    GLuint tex;
    int levels;
    uint64_t used;
} GTex;
#define FHASH_MAX 512
static uint32_t g_fhash[2][FHASH_MAX];
static int g_fhash_n[2], g_fhash_cur;
static void fhash_note(uint32_t h)
{
    int c = g_fhash_cur, i, n = g_fhash_n[c];
    for (i = 0; i < n; i++) if (g_fhash[c][i] == h) return;
    if (n < FHASH_MAX) { g_fhash[c][n] = h; g_fhash_n[c] = n + 1; }
}
int xbox_FrameTextureList(uint32_t *out, int max)
{
    int p = g_fhash_cur ^ 1, i, n = g_fhash_n[p] < max ? g_fhash_n[p] : max;
    for (i = 0; i < n; i++) out[i] = g_fhash[p][i];
    return n;
}
int xbox_FrameUsedTexture(uint32_t hash)
{
    int p = g_fhash_cur ^ 1, i;
    for (i = 0; i < g_fhash_n[p]; i++) if (g_fhash[p][i] == hash) return 1;
    return 0;
}
#define GTEX_MAX 512
static GTex g_tex[GTEX_MAX];

/* Ultra textures (nv2a_ultra.inc): the pack's pictures as BC7 (BPTC)
 * textures, all their levels -- where the driver has BPTC (GL 4.2, or
 * ARB_texture_compression_bptc; not macOS's GL 4.1) */
static GLuint *g_utex;
static int s_ultra_used, g_ultra_bptc = -1;
static GLuint ultra_gpu_get(int idx)
{
    UltraFile *u = &g_uf[idx];
    if (g_ultra_bptc < 0) {
        GLint mj = 0, mn = 0;
        glGetIntegerv(0x821B /* GL_MAJOR_VERSION */, &mj); glGetIntegerv(0x821C /* GL_MINOR_VERSION */, &mn);
        g_ultra_bptc = (mj > 4 || (mj == 4 && mn >= 2)) || gl_has_ext("GL_ARB_texture_compression_bptc") || gl_has_ext("GL_EXT_texture_compression_bptc");
        if (!g_ultra_bptc) fprintf(stderr, "  [ULTRA] no BC7 (BPTC) textures in this OpenGL: Ultra textures off\n");
    }
    if (!g_ultra_bptc) return 0;
    if (!g_utex) { g_utex = (GLuint *)calloc((size_t)g_uicap, sizeof *g_utex); if (!g_utex) return 0; }
    if (u->state == 3 && g_utex[idx]) { u->used = ++g_ultra_clock; return g_utex[idx]; }
    if (u->state == 2) {
        uint32_t l, W = u->w, H = u->h, off = 0, lv = u->levels > 16 ? 16 : u->levels;
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &g_utex[idx]);
        glBindTexture(GL_TEXTURE_2D, g_utex[idx]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)lv - 1);
        for (l = 0; l < lv; l++) {
            uint32_t sz = ((W + 3) / 4) * ((H + 3) / 4) * 16;
            glCompressedTexImage2D(GL_TEXTURE_2D, (GLint)l, 0x8E8C /* COMPRESSED_RGBA_BPTC_UNORM */, (GLsizei)W, (GLsizei)H, 0, (GLsizei)sz, u->data + off);
            off += sz;
            W = W > 1 ? W / 2 : 1; H = H > 1 ? H / 2 : 1;
        }
        free(u->data); u->data = NULL;
        g_sh.tex_ok = 0;
        g_ultra_bytes += u->size;
        u->used = ++g_ultra_clock;
        InterlockedExchange(&u->state, 3);
        while (g_ultra_bytes > ultra_budget()) {
            int i, old = -1;
            for (i = 0; i < g_uin; i++)
                if (i != idx && g_uf[i].state == 3 && g_utex[i] && (old < 0 || g_uf[i].used < g_uf[old].used)) old = i;
            if (old < 0) break;
            glDeleteTextures(1, &g_utex[old]); g_utex[old] = 0;
            g_ultra_bytes -= g_uf[old].size;
            InterlockedExchange(&g_uf[old].state, 0);
        }
        return g_utex[idx];
    }
    if (u->state == 0) ultra_request(idx);
    return 0;
}

static GLuint gtex_get(const Texture *t)
{
    uint32_t ver = 0;
    const uint32_t *argb;
    GTex *e = NULL, *victim = NULL;
    int i;
    s_ultra_used = 0;
    argb = texc_get_ver(t, &ver);
    if (!argb) return 0;
    fhash_note(s_texc_last_hash);
    if (s_texc_last_uidx >= 0 && g_ultra_on == 1 && ultra_sky_check(s_texc_last_uidx, t, s_3d.depth_test, s_3d.depth_mask)) {   /* Ultra textures: the pack's picture, once it is read */
        GLuint hd = ultra_gpu_get(s_texc_last_uidx);
        if (hd) { s_ultra_used = 1; return hd; }
    }
    for (i = 0; i < GTEX_MAX; i++) {
        GTex *c = &g_tex[i];
        if (c->tex && c->off == t->offset && c->color == t->color && c->w == t->width && c->h == t->height
            && c->pitch == t->pitch) { e = c; break; }
        if (!victim || (!c->tex && victim->tex) || (c->tex && victim->tex && c->used < victim->used)) victim = c;
    }
    if (!e) {
        int lv = 1;
        e = victim;
        if (e->tex) glDeleteTextures(1, &e->tex);
        memset(e, 0, sizeof *e);
        gpu_filter_opts();
        /* A full mip chain the GPU builds from the top level (the title's
         * own mip levels are not decoded), for stages that filter with
         * mipmaps -- less shimmer at a distance, and what anisotropic
         * filtering works from. */
        if (g_mips && t->width > 1 && t->height > 1 && tex_size_from_format(t->color)) {
            uint32_t m = t->width > t->height ? t->width : t->height;
            while (m > 1) { m >>= 1; lv++; }
        }
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &e->tex);
        glBindTexture(GL_TEXTURE_2D, e->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, lv - 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)t->width, (GLsizei)t->height, 0, GL_BGRA,
                     GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
        e->levels = lv;
        e->off = t->offset; e->color = t->color; e->w = t->width; e->h = t->height; e->pitch = t->pitch;
        e->ver = ver - 1;
        g_sh.tex_ok = 0;
    }
    if (e->ver != ver) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, e->tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)t->width, (GLsizei)t->height, GL_BGRA,
                        GL_UNSIGNED_INT_8_8_8_8_REV, argb);
        if (e->levels > 1) glGenerateMipmap(GL_TEXTURE_2D);
        e->ver = ver;
        g_sh.tex_ok = 0;
    }
    e->used = ++g_clock;
    return e->tex;
}

/* ---------------- samplers ---------------- */

typedef struct { uint64_t key; GLuint obj; } GSmp;
#define GSMP_MAX 128
static GSmp g_smp_c[GSMP_MAX];
static GLuint gpu_sampler_ex(uint32_t au, uint32_t av, int linear, int mip)
{
    uint64_t k;
    int i;
    GLuint s = 0;
    gpu_filter_opts();
    if (!g_mips) mip = 0;
    k = ((uint64_t)au << 16) | ((uint64_t)av << 8) | (uint64_t)linear | ((uint64_t)mip << 32) | (1ull << 40);
    for (i = 0; i < GSMP_MAX && g_smp_c[i].obj; i++)
        if (g_smp_c[i].key == k) return g_smp_c[i].obj;
    glGenSamplers(1, &s);
    {
        uint32_t a[2] = { au, av };
        GLenum pn[2] = { GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T };
        int j;
        for (j = 0; j < 2; j++)
            glSamplerParameteri(s, pn[j], a[j] == 1 ? GL_REPEAT : a[j] == 2 ? GL_MIRRORED_REPEAT
                                         : a[j] == 4 ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, linear ? (mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR)
                                                              : (mip ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST));
        glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, 0.0f);
        glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, mip ? 1000.0f : 0.0f);
        if (linear && mip && g_aniso > 1 && g_max_aniso > 1.0f)
            glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY_EXT, (float)g_aniso < g_max_aniso ? (float)g_aniso : g_max_aniso);
    }
    if (i < GSMP_MAX) { g_smp_c[i].key = k; g_smp_c[i].obj = s; }
    return s;
}
static GLuint gpu_sampler(uint32_t au, uint32_t av, int linear) { return gpu_sampler_ex(au, av, linear, 0); }
static void gpu_samplers_drop(void)
{
    int i;
    for (i = 0; i < GSMP_MAX; i++) if (g_smp_c[i].obj) glDeleteSamplers(1, &g_smp_c[i].obj);
    memset(g_smp_c, 0, sizeof g_smp_c);
    g_sh.tex_ok = 0;
}

/* ---------------- shaders ---------------- */

/* GLSL versions of the HLSL in nv2a_d3d11_shaders.h; the uniform blocks have
 * the cbuffers' layouts (std140 packs these 16-byte members the same way),
 * so the CPU side fills them with the same bytes. */

static const char s_glsl_vs_main[] =
"#version 330 core\n"
"layout(std140) uniform VSC { vec4 vp; };\n"
"layout(location = 0) in vec4 a_p;\n"
"layout(location = 1) in vec4 a_d0;\n"
"layout(location = 2) in vec4 a_d1;\n"
"layout(location = 3) in vec4 a_fog;\n"
"layout(location = 4) in vec4 a_t0;\n"
"layout(location = 5) in vec4 a_t1;\n"
"layout(location = 6) in vec4 a_t2;\n"
"layout(location = 7) in vec4 a_t3;\n"
"out vec4 v_d0; out vec4 v_d1; out vec4 v_fog; out vec4 v_t0; out vec4 v_t1; out vec4 v_t2; out vec4 v_t3;\n"
"invariant gl_Position;\n"
"void main() {\n"
"  gl_Position = vec4(a_p.x * vp.x - a_p.w, a_p.y * vp.y - a_p.w, 2.0 * a_p.z * vp.z - a_p.w, a_p.w);\n"
"  v_d0 = a_d0; v_d1 = a_d1; v_fog = a_fog; v_t0 = a_t0; v_t1 = a_t1; v_t2 = a_t2; v_t3 = a_t3;\n"
"}\n";

/* Declarations shared by the general fragment shader and the specialized
 * ones (as the HLSL's text before ps_main). */
static const char s_glsl_ps_head[] =
"#version 330 core\n"
"struct CStage { ivec4 cin; ivec4 ain; ivec4 f1; ivec4 f2; ivec4 f3; vec4 c0; vec4 c1; };\n"
"layout(std140) uniform PSC {\n"
"  CStage st[8];\n"
"  ivec4 fin0; ivec4 fin1; ivec4 misc;\n"
"  vec4 ff0; vec4 ff1; vec4 fogc; vec4 fogp;\n"
"  ivec4 smode; ivec4 sflags;\n"
"  vec4 sscale[4];\n"
"  ivec4 atest; ivec4 sdot; ivec4 sinp;\n"
"};\n"
"uniform sampler2D tx0; uniform sampler2D tx1; uniform sampler2D tx2; uniform sampler2D tx3;\n"
"in vec4 v_d0; in vec4 v_d1; in vec4 v_fog; in vec4 v_t0; in vec4 v_t1; in vec4 v_t2; in vec4 v_t3;\n"
"layout(location = 0) out vec4 o_color;\n"
"float sat(float x) { return clamp(x, 0.0, 1.0); }\n"
"vec3 sat3(vec3 x) { return clamp(x, 0.0, 1.0); }\n"
"vec4 sat4(vec4 x) { return clamp(x, 0.0, 1.0); }\n"
"float cmap(float x, int m) {\n"
"  if (m == 0) return max(x, 0.0);\n"
"  if (m == 1) return 1.0 - sat(x);\n"
"  if (m == 2) return 2.0 * max(x, 0.0) - 1.0;\n"
"  if (m == 3) return -2.0 * max(x, 0.0) + 1.0;\n"
"  if (m == 4) return max(x, 0.0) - 0.5;\n"
"  if (m == 5) return -max(x, 0.0) + 0.5;\n"
"  if (m == 6) return x;\n"
"  return -x;\n"
"}\n"
"float cop(float x, int op) {\n"
"  if (op == 1) x -= 0.5; else if (op == 2) x *= 2.0; else if (op == 3) x = (x - 0.5) * 2.0;\n"
"  else if (op == 4) x *= 4.0; else if (op == 6) x *= 0.5;\n"
"  return clamp(x, -1.0, 1.0);\n"
"}\n"
/* A depth-encoded stage reads the Z24S8 surface as A8R8G8B8:
 * A = z[23:16], R = z[15:8], G = z[7:0], B = stencil (here 0). */
"vec4 zread(sampler2D t, vec2 uv) {\n"
"  ivec2 sz = textureSize(t, 0);\n"
"  ivec2 ip = ivec2(clamp(floor(uv * vec2(sz)), vec2(0.0), vec2(sz - ivec2(1))));\n"
"  uint z = uint(texelFetch(t, ip, 0).r * 16777215.0 + 0.5);\n"
"  return vec4(float((z >> 8u) & 255u) / 255.0, float(z & 255u) / 255.0, 0.0, float((z >> 16u) & 255u) / 255.0);\n"
"}\n"
"vec3 dnorm(vec4 c, int m) {\n"
"  if (m == 0) return c.rgb;\n"
"  if (m == 3) { vec3 b = round(c.rgb * 255.0); return mix(b, b - 256.0, greaterThan(b, vec3(127.5))) / 127.0; }\n"
"  return c.rgb * 2.0 - 1.0;\n"
"}\n";

/* The general combiner interpreter (ps_main). */
static const char s_glsl_ps_main[] =
"vec4 stex(sampler2D t, vec4 tc, int k) {\n"
"  int mode = smode[k], fl = sflags[k];\n"
"  if (mode == 0) return vec4(0.0);\n"
"  if (mode == 4) return sat4(tc);\n"
"  if ((fl & 1) == 0) return vec4(0.0, 0.0, 0.0, 1.0);\n"
"  vec2 uv = tc.xy;\n"
"  if (mode == 1 && tc.w != 0.0 && tc.w != 1.0) uv /= tc.w;\n"
"  if ((fl & 4) == 0) uv *= sscale[k].xy;\n"
"  if ((fl & 8) != 0) return zread(t, uv);\n"
"  return texture(t, uv);\n"
"}\n"
"vec3 crgb(int c, vec4 R[16]) {\n"
"  int reg = c & 15, al = (c >> 4) & 1, m = (c >> 8) & 7;\n"
"  vec4 r = R[reg];\n"
"  if (al != 0) { float v = cmap(r.a, m); return vec3(v); }\n"
"  return vec3(cmap(r.r, m), cmap(r.g, m), cmap(r.b, m));\n"
"}\n"
"float calp(int c, vec4 R[16]) {\n"
"  int reg = c & 15, al = (c >> 4) & 1, m = (c >> 8) & 7;\n"
"  return cmap(al != 0 ? R[reg].a : R[reg].b, m);\n"
"}\n"
"void main() {\n"
"  vec4 R[16];\n"
"  int k;\n"
"  for (k = 0; k < 16; k++) R[k] = vec4(0.0);\n"
"  float dp1 = 0.0, dp2 = 0.0;\n"
"  if ((sflags.x & 2) != 0) R[8] = stex(tx0, v_t0, 0);\n"
"  if (smode.y == 17) { dp1 = dot(v_t1.xyz, dnorm(R[8], sdot.y)); R[9] = vec4(0.0); }\n"
"  else if ((sflags.y & 2) != 0) R[9] = stex(tx1, v_t1, 1);\n"
"  if (smode.z == 17) { dp2 = dot(v_t2.xyz, dnorm(sinp.z != 0 ? R[9] : R[8], sdot.z)); R[10] = vec4(0.0); }\n"
"  else if (smode.z == 9) { vec2 uv = vec2(dp1, dot(v_t2.xyz, dnorm(sinp.z != 0 ? R[9] : R[8], sdot.z)));\n"
"    R[10] = (sflags.z & 1) != 0 ? texture(tx2, uv) : vec4(0.0, 0.0, 0.0, 1.0); }\n"
"  else if ((sflags.z & 2) != 0) R[10] = stex(tx2, v_t2, 2);\n"
"  if (smode.w == 9) { vec4 nn = sinp.w == 2 ? R[10] : (sinp.w == 1 ? R[9] : R[8]);\n"
"    vec2 uv = vec2(dp2, dot(v_t3.xyz, dnorm(nn, sdot.w)));\n"
"    R[11] = (sflags.w & 1) != 0 ? texture(tx3, uv) : vec4(0.0, 0.0, 0.0, 1.0); }\n"
"  else if ((sflags.w & 2) != 0) R[11] = stex(tx3, v_t3, 3);\n"
"  R[4] = v_d0; R[5] = v_d1;\n"
"  vec4 fc = vec4(fogc.rgb, 1.0);\n"
"  if (fogp.w != 0.0) {\n"
"    float dd = v_fog.x, f; int fm = int(fogp.z);\n"
"    if (fm == 0x800 || fm == 0x802) { if (fm == 0x802) dd = abs(dd); f = fogp.x + exp2(dd * fogp.y * 16.0) - 1.5; }\n"
"    else if (fm == 0x801 || fm == 0x803) { if (fm == 0x803) dd = abs(dd); f = fogp.x + exp2(-dd * dd * fogp.y * fogp.y * 32.0) - 1.5; }\n"
"    else { if (fm == 0x804) dd = abs(dd); f = fogp.x + dd * fogp.y - 1.0; }\n"
"    fc.a = sat(f);\n"
"  }\n"
"  R[3] = fc;\n"
"  R[12].a = R[8].a;\n"
"  vec4 o;\n"
"  if (misc.w == 0) {\n"
"    o = ((atest.w != 0) ? R[8] : vec4(1.0)) * R[4];\n"
"  } else {\n"
"    for (k = 0; k < misc.x; k++) {\n"
"      CStage S = st[k];\n"
"      R[1] = S.c0; R[2] = S.c1;\n"
"      vec3 A = crgb(S.cin.x, R), B = crgb(S.cin.y, R), C = crgb(S.cin.z, R), D = crgb(S.cin.w, R);\n"
"      vec3 ab = (S.f1.y & 2) != 0 ? vec3(dot(A, B)) : A * B;\n"
"      vec3 cd = (S.f1.y & 1) != 0 ? vec3(dot(C, D)) : C * D;\n"
"      bool sel = misc.y != 0 ? (R[12].a >= 0.5) : ((int(R[12].a * 255.0 + 0.5) & 1) != 0);\n"
"      vec3 sm = (S.f1.y & 4) != 0 ? (sel ? cd : ab) : ab + cd;\n"
"      int op = S.f1.z;\n"
"      vec3 rab = vec3(cop(ab.x, op), cop(ab.y, op), cop(ab.z, op));\n"
"      vec3 rcd = vec3(cop(cd.x, op), cop(cd.y, op), cop(cd.z, op));\n"
"      vec3 rsm = vec3(cop(sm.x, op), cop(sm.y, op), cop(sm.z, op));\n"
"      float a0 = calp(S.ain.x, R), a1 = calp(S.ain.y, R), a2 = calp(S.ain.z, R), a3 = calp(S.ain.w, R);\n"
"      float aab = a0 * a1, acd = a2 * a3;\n"
"      float asm_ = (S.f1.y & 8) != 0 ? (sel ? acd : aab) : aab + acd;\n"
"      int aop = S.f1.w;\n"
"      float ra0 = cop(aab, aop), ra1 = cop(acd, aop), ra2 = cop(asm_, aop);\n"
"      if (S.f2.x != 0) { R[S.f2.x].rgb = rab; if ((S.f3.w & 2) != 0) R[S.f2.x].a = rab.b; }\n"
"      if (S.f2.y != 0) { R[S.f2.y].rgb = rcd; if ((S.f3.w & 1) != 0) R[S.f2.y].a = rcd.b; }\n"
"      if (S.f2.z != 0) R[S.f2.z].rgb = rsm;\n"
"      if (S.f3.x != 0) R[S.f3.x].a = ra0;\n"
"      if (S.f3.y != 0) R[S.f3.y].a = ra1;\n"
"      if (S.f3.z != 0) R[S.f3.z].a = ra2;\n"
"    }\n"
"    if (fin1.w == 0) {\n"
"      o = sat4(R[12]);\n"
"    } else {\n"
"      R[1] = ff0; R[2] = ff1;\n"
"      vec3 E = crgb(fin1.x, R), F = crgb(fin1.y, R);\n"
"      R[15].rgb = E * F;\n"
"      R[14].rgb = R[5].rgb + R[12].rgb;\n"
"      if (misc.z != 0) R[14].rgb = min(R[14].rgb, vec3(1.0));\n"
"      vec3 A = crgb(fin0.x, R), B = crgb(fin0.y, R), C = crgb(fin0.z, R), D = crgb(fin0.w, R);\n"
"      vec3 a = sat3(A);\n"
"      o.rgb = sat3(a * B + (1.0 - a) * C + D);\n"
"      o.a = sat(calp(fin1.z, R));\n"
"    }\n"
"  }\n"
"  if (atest.x != 0) {\n"
"    int a8 = int(sat(o.a) * 255.0 + 0.5), r = atest.z, f = atest.y & 15; bool okp = true;\n"
"    if (f == 0) okp = false; else if (f == 1) okp = a8 < r; else if (f == 2) okp = a8 == r;\n"
"    else if (f == 3) okp = a8 <= r; else if (f == 4) okp = a8 > r; else if (f == 5) okp = a8 != r;\n"
"    else if (f == 6) okp = a8 >= r;\n"
"    if (!okp) discard;\n"
"  }\n"
"  o_color = o;\n"
"}\n";

/* Full-screen triangles: clears, copies, the picture to the window. u_ys is
 * +1 for drawing into a surface (image row 0 at the top, as everything
 * here), -1 for the window, whose rows count from the bottom. */
static const char s_glsl_vs_tri[] =
"#version 330 core\n"
"layout(std140) uniform CLC { vec4 clear_color; };\n"
"uniform float u_ys;\n"
"out vec2 v_uv;\n"
"void main() {\n"
"  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
"  gl_Position = vec4(p.x * 2.0 - 1.0, (p.y * 2.0 - 1.0) * u_ys, 0.0, 1.0);\n"
"#ifdef BLIT\n"
"  v_uv = clear_color.zw + p * clear_color.xy;\n"
"#else\n"
"  v_uv = p * clear_color.xy;\n"
"#endif\n"
"}\n";

static const char s_glsl_ps_tri[] =
"#version 330 core\n"
"layout(std140) uniform CLC { vec4 clear_color; };\n"
"layout(std140) uniform POSTC { vec4 post; };\n"
"uniform sampler2D tx0;\n"
"in vec2 v_uv;\n"
"layout(location = 0) out vec4 o_color;\n"
"vec3 sharp_apply(vec3 c, vec3 n, vec3 s, vec3 w, vec3 e) {\n"
"  vec3 mn = min(c, min(min(n, s), min(w, e))), mx = max(c, max(max(n, s), max(w, e)));\n"
"  vec3 amp = clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0);\n"
"  vec3 k = sqrt(amp) * post.y * 0.25;\n"
"  return clamp(c + k * (4.0 * c - n - s - w - e), mn, mx);\n"
"}\n"
"vec3 post_sharp(vec3 c, vec2 uv) {\n"
"  if (post.y <= 0.0) return c;\n"
"  vec2 rc = clear_color.zw;\n"
"  vec3 n = textureLod(tx0, uv + vec2(0.0, -rc.y), 0.0).rgb, s = textureLod(tx0, uv + vec2(0.0, rc.y), 0.0).rgb;\n"
"  vec3 w = textureLod(tx0, uv + vec2(-rc.x, 0.0), 0.0).rgb, e = textureLod(tx0, uv + vec2(rc.x, 0.0), 0.0).rgb;\n"
"  return sharp_apply(c, n, s, w, e);\n"
"}\n"
/* (1.9) drawn larger than the window: the mean of the texels a pixel
 * covers (nv2a_d3d11_shaders.h) */
"vec2 ds_foot(vec2 uv) { return vec2(abs(dFdx(uv.x)), abs(dFdy(uv.y))) / clear_color.zw; }\n"
"vec3 ds_fetch(vec2 uv, vec2 f) {\n"
"  if (max(f.x, f.y) <= 1.5) return textureLod(tx0, uv, 0.0).rgb;\n"
"  int nx = clamp(int(ceil(f.x * 0.5)), 1, 8), ny = clamp(int(ceil(f.y * 0.5)), 1, 8);\n"
"  vec3 a = vec3(0.0);\n"
"  for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++)\n"
"    a += textureLod(tx0, uv + (vec2((float(x) + 0.5) / float(nx), (float(y) + 0.5) / float(ny)) - 0.5) * f * clear_color.zw, 0.0).rgb;\n"
"  return a / float(nx * ny);\n"
"}\n"
"vec3 ds_present(vec2 uv, vec2 f) {\n"
"  vec3 c = ds_fetch(uv, f);\n"
"  if (post.y > 0.0) {\n"
"    vec2 st = f * clear_color.zw;\n"
"    c = sharp_apply(c, ds_fetch(uv - vec2(0.0, st.y), f), ds_fetch(uv + vec2(0.0, st.y), f), ds_fetch(uv - vec2(st.x, 0.0), f), ds_fetch(uv + vec2(st.x, 0.0), f));\n"
"  }\n"
"  return c;\n"
"}\n"
"vec3 post_grade(vec3 c) {\n"
"  c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / post.x));\n"
"  float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
"  return clamp(mix(vec3(l), c, post.z), 0.0, 1.0);\n"
"}\n"
"float fx_luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
"void main() {\n"
"#if defined(CLEAR)\n"
"  o_color = clear_color;\n"
"#elif defined(PRESENT)\n"
"  vec2 f = ds_foot(v_uv);\n"
"  if (max(f.x, f.y) > 1.5) o_color = vec4(post_grade(ds_present(v_uv, f)), 1.0);\n"
"  else { vec3 c = texture(tx0, v_uv).rgb; o_color = vec4(post_grade(post_sharp(c, v_uv)), 1.0); }\n"
"#elif defined(FXAA)\n"
"  vec2 rc = clear_color.zw, uv = v_uv;\n"
"  vec2 f = ds_foot(uv);\n"
"  if (max(f.x, f.y) > 1.5) { o_color = vec4(post_grade(ds_present(uv, f)), 1.0); return; }\n"
"  vec3 nw = textureLod(tx0, uv + vec2(-1.0, -1.0) * rc, 0.0).rgb;\n"
"  vec3 ne = textureLod(tx0, uv + vec2( 1.0, -1.0) * rc, 0.0).rgb;\n"
"  vec3 sw = textureLod(tx0, uv + vec2(-1.0,  1.0) * rc, 0.0).rgb;\n"
"  vec3 se = textureLod(tx0, uv + vec2( 1.0,  1.0) * rc, 0.0).rgb;\n"
"  vec3 m  = textureLod(tx0, uv, 0.0).rgb;\n"
"  float lnw = fx_luma(nw), lne = fx_luma(ne), lsw = fx_luma(sw), lse = fx_luma(se), lm = fx_luma(m);\n"
"  float lmin = min(lm, min(min(lnw, lne), min(lsw, lse)));\n"
"  float lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));\n"
"  vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));\n"
"  float red = max((lnw + lne + lsw + lse) * (0.25 / 8.0), 1.0 / 128.0);\n"
"  float rmin = 1.0 / (min(abs(dir.x), abs(dir.y)) + red);\n"
"  dir = clamp(dir * rmin, -8.0, 8.0) * rc;\n"
"  vec3 a = 0.5 * (textureLod(tx0, uv + dir * (1.0 / 3.0 - 0.5), 0.0).rgb + textureLod(tx0, uv + dir * (2.0 / 3.0 - 0.5), 0.0).rgb);\n"
"  vec3 b = a * 0.5 + 0.25 * (textureLod(tx0, uv - dir * 0.5, 0.0).rgb + textureLod(tx0, uv + dir * 0.5, 0.0).rgb);\n"
"  float lb = fx_luma(b);\n"
"  vec3 r = (lb < lmin || lb > lmax) ? a : b;\n"
"  if (post.y > 0.0) r = clamp(r + post.y * 0.3 * (r - 0.25 * (nw + ne + sw + se)), min(m, min(min(nw, ne), min(sw, se))), max(m, max(max(nw, ne), max(sw, se))));\n"
"  o_color = vec4(post_grade(r), 1.0);\n"
"#else\n"
"  o_color = texture(tx0, v_uv);\n"
"#endif\n"
"}\n";

/* A shader object from sources (concatenated after an optional #define
 * line), compiled; 0 on failure (logged). Waits for the result. */
static GLuint gpu_shader_now(GLenum type, const char *const *src, int n, const char *what)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, n, src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "  [GL] %s did not compile: %s\n", what, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/* Bind the uniform blocks and samplers of a linked program to the fixed
 * binding points and texture units. */
static void gpu_prog_bindings(GLuint p)
{
    static const char *ub[5] = { "VSC", "PSC", "CLC", "POSTC", "VPC" };
    int i;
    for (i = 0; i < 5; i++) {
        GLuint bi = glGetUniformBlockIndex(p, ub[i]);
        if (bi != GL_INVALID_INDEX) glUniformBlockBinding(p, bi, (GLuint)i);
    }
    glUseProgram(p);
    for (i = 0; i < 4; i++) {
        char nm[8];
        GLint loc;
        snprintf(nm, sizeof nm, "tx%d", i);
        loc = glGetUniformLocation(p, nm);
        if (loc >= 0) glUniform1i(loc, i);
    }
    g_sh.prog = 0;
}

static GLuint gpu_link_now(GLuint vs, GLuint fs, const char *what)
{
    GLuint p = glCreateProgram();
    GLint ok = 0;
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "  [GL] %s did not link: %s\n", what, log);
        glDeleteProgram(p);
        return 0;
    }
    gpu_prog_bindings(p);
    return p;
}

static GLuint g_vs_main, g_fs_main, g_pr_main;
static int g_parallel = 0;      /* KHR_parallel_shader_compile */

/* A full-screen-triangle program (copy, present, fxaa, blit, clear). */
static GLuint gpu_tri_program(const char *vs_def, const char *fs_def, GLint *ys_loc, const char *what)
{
    const char *vsrc[3], *fsrc[3];
    GLuint vs, fs, p;
    vsrc[0] = "#version 330 core\n"; vsrc[1] = vs_def; vsrc[2] = s_glsl_vs_tri + strlen("#version 330 core\n");
    fsrc[0] = "#version 330 core\n"; fsrc[1] = fs_def; fsrc[2] = s_glsl_ps_tri + strlen("#version 330 core\n");
    vs = gpu_shader_now(GL_VERTEX_SHADER, vsrc, 3, what);
    fs = vs ? gpu_shader_now(GL_FRAGMENT_SHADER, fsrc, 3, what) : 0;
    if (!vs || !fs) { if (vs) glDeleteShader(vs); return 0; }
    p = gpu_link_now(vs, fs, what);
    glDeleteShader(vs); glDeleteShader(fs);
    if (p && ys_loc) *ys_loc = glGetUniformLocation(p, "u_ys");
    return p;
}

/* ---------------- generated programs (vertex and combiner) ------------- */

/* The program for a (vertex shader, fragment shader) pair. D3D binds the two
 * stages separately; GL links them into one program, so every pair a draw
 * uses gets one, linked once and kept -- in the background where the driver
 * offers it (KHR_parallel_shader_compile), the general shaders drawing until
 * it is ready. */
typedef struct {
    GLuint vs, fs, prog;
    int state;                  /* 0 linking, 1 ready, 2 failed */
} GPair;
#define GPAIR_MAX 4096
static GPair *g_pair;
static int g_pair_n;
static uint32_t g_pair_hash_tab[8192];          /* index + 1, open addressing */

static int gpu_pair_ready(GPair *e)
{
    GLint ok = 0;
    if (e->state != 0) return e->state == 1;
    if (g_parallel) {
        GLint done = 0;
        glGetProgramiv(e->prog, GL_COMPLETION_STATUS_KHR, &done);
        if (!done) return 0;
    }
    glGetProgramiv(e->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        static int said;
        if (said++ < 3) {
            char log[2048];
            glGetProgramInfoLog(e->prog, sizeof log, NULL, log);
            fprintf(stderr, "  [GL] a generated program did not link: %s\n", log);
        }
        glDeleteProgram(e->prog);
        e->prog = 0;
        e->state = 2;
        return 0;
    }
    gpu_prog_bindings(e->prog);
    e->state = 1;
    return 1;
}

/* The ready program for vs + fs, or 0 (then started, if it was not). */
static GLuint gpu_pair(GLuint vs, GLuint fs)
{
    uint32_t h = (vs * 2654435761u) ^ (fs * 40503u), i;
    GPair *e;
    if (!vs || !fs) return 0;
    if (!g_pair) { g_pair = (GPair *)calloc(GPAIR_MAX, sizeof *g_pair); if (!g_pair) return 0; }
    for (i = 0; i < 8192; i++) {
        uint32_t slot = (h + i) & 8191, ix = g_pair_hash_tab[slot];
        if (!ix) break;
        e = &g_pair[ix - 1];
        if (e->vs == vs && e->fs == fs) return gpu_pair_ready(e) ? e->prog : 0;
    }
    if (i == 8192 || g_pair_n >= GPAIR_MAX) return 0;
    e = &g_pair[g_pair_n++];
    e->vs = vs; e->fs = fs;
    e->prog = glCreateProgram();
    glAttachShader(e->prog, vs);
    glAttachShader(e->prog, fs);
    glLinkProgram(e->prog);
    e->state = 0;
    g_pair_hash_tab[(h + i) & 8191] = (uint32_t)g_pair_n;
    if (!g_parallel) return gpu_pair_ready(e) ? e->prog : 0;
    return 0;
}

/* A shader object compiled (asynchronously where the driver can); state
 * read later with gpu_shader_done. */
static GLuint gpu_shader_start(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}
/* 1 compiled, 0 still compiling, -1 failed (the object is deleted) */
static int gpu_shader_done(GLuint *s, const char *what, uint32_t id)
{
    GLint ok = 0;
    if (g_parallel) {
        GLint done = 0;
        glGetShaderiv(*s, GL_COMPLETION_STATUS_KHR, &done);
        if (!done) return 0;
    }
    glGetShaderiv(*s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        static int said;
        if (said++ < 3) {
            char log[2048];
            glGetShaderInfoLog(*s, sizeof log, NULL, log);
            fprintf(stderr, "  [GL] %s %08X did not compile: %s\n", what, id, log);
        }
        glDeleteShader(*s);
        *s = 0;
        return -1;
    }
    return 1;
}

/* ---------------- init ---------------- */

static void gpu_apply_settings(void);
static int gpu_init(void)
{
    const char *e = getenv("RECOMP_RENDERER");
    GLint maj = 0, mnr = 0;
    if (e && !strcasecmp(e, "software")) return 0;
    g_glc = (SDL_GLContext)xbox_FramebufferGLContext(10000);
    g_win = (SDL_Window *)xbox_FramebufferHwnd();
    if (!g_win || !g_glc) {
        fprintf(stderr, "  [GL] no OpenGL 3.3 context; using the software renderer\n");
        return 0;
    }
    if (SDL_GL_MakeCurrent(g_win, g_glc) != 0) {
        fprintf(stderr, "  [GL] cannot use the OpenGL context (%s); using the software renderer\n", SDL_GetError());
        return 0;
    }
    if (!gl_load()) { SDL_GL_MakeCurrent(g_win, NULL); return 0; }
    glGetIntegerv(GL_MAJOR_VERSION, &maj);
    glGetIntegerv(GL_MINOR_VERSION, &mnr);
    fprintf(stderr, "  [GL] %s / %s / OpenGL %s\n", (const char *)glGetString(GL_VENDOR),
            (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    if (maj < 3 || (maj == 3 && mnr < 3)) {
        fprintf(stderr, "  [GL] OpenGL 3.3 is needed; using the software renderer\n");
        SDL_GL_MakeCurrent(g_win, NULL);
        return 0;
    }
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &g_ubo_align);
    if (g_ubo_align < 16) g_ubo_align = 16;
    if (gl_has_ext("GL_EXT_texture_filter_anisotropic") || gl_has_ext("GL_ARB_texture_filter_anisotropic"))
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &g_max_aniso);
    if (!getenv("RECOMP_GL_SYNC_COMPILE")
        && (gl_has_ext("GL_KHR_parallel_shader_compile") || gl_has_ext("GL_ARB_parallel_shader_compile"))) {
        *(void **)&p_glMaxShaderCompilerThreadsKHR = SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsKHR");
        if (!p_glMaxShaderCompilerThreadsKHR)
            *(void **)&p_glMaxShaderCompilerThreadsKHR = SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsARB");
        if (p_glMaxShaderCompilerThreadsKHR) p_glMaxShaderCompilerThreadsKHR(0xFFFFFFFFu);
        g_parallel = 1;
    }
    fprintf(stderr, "  [GL] shaders compiled %s; anisotropic filtering up to x%.0f\n",
            g_parallel ? "in the background" : "when first used", g_max_aniso);

    {
        const char *vs[1] = { s_glsl_vs_main };
        const char *fs[2] = { s_glsl_ps_head, s_glsl_ps_main };
        g_vs_main = gpu_shader_now(GL_VERTEX_SHADER, vs, 1, "the main vertex shader");
        g_fs_main = gpu_shader_now(GL_FRAGMENT_SHADER, fs, 2, "the combiner shader");
        if (!g_vs_main || !g_fs_main) { SDL_GL_MakeCurrent(g_win, NULL); return 0; }
        g_pr_main = gpu_link_now(g_vs_main, g_fs_main, "the main program");
        if (!g_pr_main) { SDL_GL_MakeCurrent(g_win, NULL); return 0; }
    }
    g_pr_copy = gpu_tri_program("", "", &g_loc_ys_copy, "the copy shader");
    g_pr_present = gpu_tri_program("", "#define PRESENT\n", &g_loc_ys_present, "the present shader");
    g_pr_fxaa = gpu_tri_program("", "#define FXAA\n", &g_loc_ys_fxaa, "the FXAA shader");
    g_pr_blit = gpu_tri_program("#define BLIT\n", "", &g_loc_ys_blit, "the blit shader");
    g_pr_clear = gpu_tri_program("", "#define CLEAR\n", NULL, "the clear shader");
    if (!g_pr_copy || !g_pr_present || !g_pr_blit || !g_pr_clear) { SDL_GL_MakeCurrent(g_win, NULL); return 0; }
    if (g_pr_clear) { glUseProgram(g_pr_clear); glUniform1f(glGetUniformLocation(g_pr_clear, "u_ys"), 1.0f); }

    glGenVertexArrays(1, &g_vao);
    glGenVertexArrays(1, &g_vao_x);
    glGenVertexArrays(1, &g_vao_empty);
    {
        /* what a stage reads when its texture is the target being drawn
         * (D3D unbinds such a view, and reads give 0) */
        uint32_t z = 0;
        g_tex_black = gpu_tex2d(1, 1, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &z);
    }
    {
        const char *rs = getenv("RECOMP_RENDER_SCALE");
        g_msaa = xbox_GfxMsaa();
        if (g_msaa > 1) fprintf(stderr, "  [GL] MSAA %dx\n", g_msaa);
        g_rsf = rs ? (float)atof(rs) : 1.0f;
        if (!(g_rsf >= 1.0f)) g_rsf = 1.0f;
        if (g_rsf > 12.0f) g_rsf = 12.0f;
        g_rs = (int)g_rsf;
        {
            const char *as = getenv("RECOMP_ASPECT");
            float a = as ? (float)atof(as) : 0.0f;
            if (a > 1.34f && a < 6.0f) g_aspect = a;
        }
        g_rsx = g_rsf * (g_aspect / (4.0f / 3.0f));
        g_rsy = g_rsf;
        rs_fit();
        if (g_rsf > 1.0f || g_aspect > 1.34f)
            fprintf(stderr, "  [GL] rendering at %.2f x %.2f the original resolution, picture %.3f:1\n",
                    g_rsx, g_rsy, g_aspect);
    }
    {
        const char *v = getenv("RECOMP_HW_VP");
        g_hwvp_user = !(v && v[0] == '0');
    }
    g_post[0] = xbox_GfxGetF(GFX_BRIGHT); g_post[1] = xbox_GfxGetF(GFX_SHARP); g_post[2] = xbox_GfxGetF(GFX_COLOR);
    if (g_post[0] < 0.5f) g_post[0] = 1.0f;
    if (g_post[2] <= 0.0f) g_post[2] = 1.0f;
    g_present_smp = gpu_sampler(3, 3, 1);
    g_copy_smp = g_present_smp;
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gpu_sh_reset();
    fprintf(stderr, "  [GL] renderer up\n");
    fflush(stderr);
    return 1;
}

static int gpu_on(void)
{
    if (s_gpu_mode < 0) {
        s_gpu_mode = gpu_init();
        if (s_gpu_mode) xbox_FramebufferSetGpuOwned(1);
    }
#if defined(__APPLE__)
    /* The pushbuffer runs on whichever thread kicks it (the title's kickoff
     * or the poller), one at a time under the GPU lock
     * (xbox_memory_layout.c), but a GL context is current per thread: the
     * thread that set the renderer up was the only one that had it, and the
     * other one drew into no context at all (a crash in glBindFramebuffer).
     * Make it current on this thread too. macOS (CGL) allows one context
     * current on several threads as long as they never use it at once,
     * which the GPU lock guarantees. */
    if (s_gpu_mode > 0 && SDL_GL_GetCurrentContext() != g_glc) {
        if (SDL_GL_MakeCurrent(g_win, g_glc) != 0) {
            static int said;
            if (!said++) fprintf(stderr, "  [GL] cannot make the context current on this thread (%s)\n", SDL_GetError());
            return 0;
        }
    }
#endif
    return s_gpu_mode;
}

static void gpu_flush(void) { if (s_gpu_mode > 0) glFlush(); }

/* ---------------- drawing state ---------------- */

static void gpu_set_blend(int write_mask_only_clear)
{
    uint64_t k;
    uint32_t m = s_3d.color_mask;
    int wr = (m & 0x00010000u) != 0, wg = (m & 0x00000100u) != 0, wb = (m & 0x00000001u) != 0, wa = (m & 0x01000000u) != 0;
    int en = s_gpu.blend_enable && !write_mask_only_clear;
    float bf[4];
    uint32_t c = s_3d.blend_color;
    k = ((uint64_t)en << 63) | ((uint64_t)(wr | (wg << 1) | (wb << 2) | (wa << 3)) << 48) | ((uint64_t)s_3d.blend_eq << 32)
      | ((uint64_t)(s_gpu.blend_sfactor & 0xFFFF) << 16) | (s_gpu.blend_dfactor & 0xFFFF);
    bf[0] = ((c >> 16) & 0xFF) / 255.0f; bf[1] = ((c >> 8) & 0xFF) / 255.0f;
    bf[2] = (c & 0xFF) / 255.0f; bf[3] = (c >> 24) / 255.0f;
    if (g_sh.bs_ok && g_sh.blend == k && !memcmp(g_sh.bf, bf, sizeof bf)) return;
    glColorMask(wr, wg, wb, wa);
    if (en) {
        /* the NV2A's factors and equations are GL's own enums */
        static const uint32_t ok_f[] = { 0x0000, 0x0001, 0x0300, 0x0301, 0x0302, 0x0303, 0x0304, 0x0305, 0x0306,
                                         0x0307, 0x0308, 0x8001, 0x8002, 0x8003, 0x8004 };
        uint32_t sf = s_gpu.blend_sfactor & 0xFFFF, df = s_gpu.blend_dfactor & 0xFFFF, eq = s_3d.blend_eq & 0xFFFF;
        size_t i;
        int sok = 0, dok = 0;
        for (i = 0; i < sizeof ok_f / sizeof ok_f[0]; i++) { if (sf == ok_f[i]) sok = 1; if (df == ok_f[i]) dok = 1; }
        if (!sok) sf = GL_ONE;
        if (!dok) df = GL_ONE;
        if (eq != 0x800A && eq != 0x800B && eq != 0x8007 && eq != 0x8008) eq = GL_FUNC_ADD;
        glEnable(GL_BLEND);
        glBlendFuncSeparate(sf, df, sf, df);
        glBlendEquationSeparate(eq, eq);
        glBlendColor(bf[0], bf[1], bf[2], bf[3]);
    } else glDisable(GL_BLEND);
    g_sh.blend = k; memcpy(g_sh.bf, bf, sizeof bf); g_sh.bs_ok = 1;
}

static GLenum gl_sop(uint32_t op)
{
    switch (op) {
    case 0x0000: return GL_ZERO;
    case 0x1E01: return GL_REPLACE;
    case 0x1E02: return GL_INCR;
    case 0x1E03: return GL_DECR;
    case 0x150A: return GL_INVERT;
    case 0x8507: return GL_INCR_WRAP;
    case 0x8508: return GL_DECR_WRAP;
    default:     return GL_KEEP;
    }
}

static void gpu_set_depth(int have_dsv)
{
    uint64_t k[3];
    int dt = have_dsv && s_3d.depth_test, st = have_dsv && s_3d.stencil_test;
    k[0] = ((uint64_t)dt << 63) | ((uint64_t)(dt && s_3d.depth_mask) << 62) | ((uint64_t)st << 61)
         | ((uint64_t)(s_3d.depth_func & 0xF) << 48) | ((uint64_t)(s_3d.st_func & 0xF) << 40)
         | ((uint64_t)(s_3d.st_mask & 0xFF) << 32) | ((uint64_t)(s_3d.st_wmask & 0xFF) << 24)
         | (uint64_t)(s_3d.st_ref & 0xFF);
    k[1] = ((uint64_t)s_3d.st_fail << 32) | s_3d.st_zfail;
    k[2] = s_3d.st_zpass;
    if (g_sh.ds_ok && !memcmp(g_sh.ds, k, sizeof k)) return;
    if (dt) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(0x0200 | (s_3d.depth_func & 0xF));
        glDepthMask(s_3d.depth_mask ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }
    if (st) {
        glEnable(GL_STENCIL_TEST);
        glStencilFuncSeparate(GL_FRONT_AND_BACK, 0x0200 | (s_3d.st_func & 0xF), (GLint)(s_3d.st_ref & 0xFF), s_3d.st_mask & 0xFF);
        glStencilOpSeparate(GL_FRONT_AND_BACK, gl_sop(s_3d.st_fail), gl_sop(s_3d.st_zfail), gl_sop(s_3d.st_zpass));
        glStencilMask(s_3d.st_wmask & 0xFF);
    } else {
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0);
    }
    memcpy(g_sh.ds, k, sizeof k); g_sh.ds_ok = 1;
}

static int gpu_set_raster(void)
{
    uint64_t k;
    int cull = 0;
    GLenum face = GL_BACK;
    if (s_3d.cull_enable && !getenv("RECOMP_GPU_NOCULL")) {
        if (s_3d.cull_face == 0x408) return 0;        /* front and back: nothing drawn */
        cull = 1;
        face = s_3d.cull_face == 0x404 ? GL_FRONT : GL_BACK;
    }
    /* D3D: front = counter-clockwise unless the title's front face is CW
     * (0x900); see the top of this file for why D3D's clockwise is GL's
     * counter-clockwise here. */
    {
        int fccw = (s_3d.front_face != 0x900) ^ (getenv("RECOMP_GPU_CULLFLIP") != NULL);
        k = ((uint64_t)cull << 40) | ((uint64_t)face << 8) | (uint64_t)fccw | (1ull << 41);
        if (g_sh.rs_ok && g_sh.rs == k) return 1;
        if (cull) { glEnable(GL_CULL_FACE); glCullFace(face); } else glDisable(GL_CULL_FACE);
        glFrontFace(fccw ? GL_CW : GL_CCW);
        if (getenv("RECOMP_GPU_NOZCLIP")) glEnable(GL_DEPTH_CLAMP); else glDisable(GL_DEPTH_CLAMP);
        glEnable(GL_SCISSOR_TEST);
    }
    g_sh.rs = k; g_sh.rs_ok = 1;
    return 1;
}

/* The colour and zeta targets the title has bound, made current. */
static GSurf *g_cur_rt, *g_cur_ds;
static int g_cur_sc[4];                 /* the clip rectangle on g_cur_rt */
static int gpu_bind_targets(int need_depth)
{
    uint32_t base = dma_resolve(s_gpu.color_offset);
    uint32_t bpp = surface_bpp();
    uint32_t w, h;
    int sc[4];
    if (!s_gpu.color_offset || !s_gpu.clip_w || !s_gpu.clip_h) return 0;
    w = (bpp && (s_gpu.pitch & 0xFFFF) / (bpp ? bpp : 4) >= s_gpu.clip_x + s_gpu.clip_w)
        ? (s_gpu.pitch & 0xFFFF) / bpp : s_gpu.clip_x + s_gpu.clip_w;
    if (((s_gpu.format >> 8) & 0xF) == 2) w = s_gpu.clip_x + s_gpu.clip_w;   /* swizzled: no pitch */
    h = s_gpu.clip_y + s_gpu.clip_h;
    g_cur_rt = gsurf_get(base, w, h, 0);
    if (!g_cur_rt) return 0;
    g_cur_rt->wr = ++g_wr_clock;
    gsurf_sync_from_mem(g_cur_rt);
    g_cur_ds = NULL;
    if (need_depth) {
        uint32_t zb = s_zeta_offset ? dma_resolve(s_zeta_offset) : (base | 1u);
        g_cur_ds = gsurf_get(zb, g_cur_rt->w, g_cur_rt->h, 1);
        if (g_cur_ds) g_cur_ds->wr = ++g_wr_clock;
    }
    if (g_cur_ds && !g_cur_rt->mrb != !g_cur_ds->mrb) gsurf_ms_drop(g_cur_rt->mrb ? g_cur_rt : g_cur_ds);   /* (MSAA: both or neither) */
    if (g_cur_rt->mrb) {
        if (g_cur_rt->ss_new) {
            g_cur_rt->ss_new = 0;
            gpu_copy_scaled(g_cur_rt->tex, g_cur_rt->pw, g_cur_rt->ph, g_cur_rt->pw, g_cur_rt->ph, gpu_fbo_ms(g_cur_rt->mrb, 0), g_cur_rt->pw, g_cur_rt->ph);
        }
        g_cur_rt->ms_new = 1;
        if (g_cur_ds) g_cur_ds->ms_new = 1;
    }
    gpu_set_fbo(gsurf_draw_fbo(g_cur_rt, g_cur_ds));
    if (!g_sh.vp_ok || g_sh.vw != (int)g_cur_rt->pw || g_sh.vh != (int)g_cur_rt->ph) {
        glViewport(0, 0, (GLsizei)g_cur_rt->pw, (GLsizei)g_cur_rt->ph);
        g_sh.vw = (int)g_cur_rt->pw; g_sh.vh = (int)g_cur_rt->ph; g_sh.vp_ok = 1;
    }
    sc[0] = (int)SX(g_cur_rt, s_gpu.clip_x); sc[1] = (int)SY(g_cur_rt, s_gpu.clip_y);
    sc[2] = (int)SX(g_cur_rt, s_gpu.clip_x + s_gpu.clip_w) - sc[0];
    sc[3] = (int)SY(g_cur_rt, s_gpu.clip_y + s_gpu.clip_h) - sc[1];
    memcpy(g_cur_sc, sc, sizeof sc);
    if (!g_sh.sc_ok || memcmp(g_sh.sc, sc, sizeof sc)) {
        glScissor(sc[0], sc[1], sc[2], sc[3]);
        memcpy(g_sh.sc, sc, sizeof sc); g_sh.sc_ok = 1;
    }
    {
        float c[4];
        /* window z goes to depth as z / zmax (see nv2a_d3d11.inc) */
        float zmax = (((s_gpu.format >> 4) & 0xF) == 1) ? 65535.0f : 16777215.0f;
        if (s_clip_max > 0.0f && !getenv("RECOMP_GPU_ZFORMAT")) zmax = s_clip_max;
        c[0] = 2.0f / (float)g_cur_rt->w; c[1] = 2.0f / (float)g_cur_rt->h; c[2] = 1.0f / zmax; c[3] = 0;
        if (!g_sh.cbvs_ok || memcmp(g_sh.cbvs, c, sizeof c)) {
            gpu_ub(UB_VSC, c, 16);
            memcpy(g_sh.cbvs, c, sizeof c); g_sh.cbvs_ok = 1;
        }
    }
    return 1;
}

static GSurf *gpu_surface_for_texture(uint32_t off, uint32_t tw, uint32_t th, int *is_depth)
{
    /* One piece of memory can be both: the one drawn into last is what
     * memory holds (see nv2a_d3d11.inc). */
    GSurf *s = NULL;
    int i;
    /* The main depth buffer and the shadow pass's 256x256 zeta share one
     * address: a texture of the one's size reads that one (choosing by the
     * last binding alone broke the character shadows). */
    for (i = 0; i < GSURF_MAX; i++)
        if (g_surf[i].tex && g_surf[i].base == off) {
            GSurf *c = &g_surf[i];
            int cm = tw && c->w == tw && c->h == th, sm = s && tw && s->w == tw && s->h == th;
            if (!s || (cm != sm ? cm : c->wr > s->wr)) s = c;
        }
    if (!s) return NULL;
    *is_depth = (int)s->is_depth;
    return s;
}

typedef struct {
    int st[8][20];
    int fin0[4], fin1[4], misc[4];
    int fogmode, fogen;
    int smode[4], sflags[4];
    int at_en, at_func, at_s0;
    int sdot[4], sinp[4];
} PsKey;
static PsKey g_pskey;

static void gpu_bind_stages(void)
{
    struct {
        int cin[4], ain[4], f1[4], f2[4], f3[4]; float c0[4], c1[4];
    } st[8];
    struct {
        int fin0[4], fin1[4], misc[4];
        float ff0[4], ff1[4], fogc[4], fogp[4];
        int smode[4], sflags[4];
        float sscale[4][4];
        int atest[4];
        int sdot[4];
        int sinp[4];
    } tail;
    char cb[sizeof st + sizeof tail];
    GLuint tex[4] = { 0 }, smp[4] = { 0 };
    int k, i;

    memset(st, 0, sizeof st);
    memset(&tail, 0, sizeof tail);
    for (i = 0; i < s_cp.n; i++) {
        const CStage *S = &s_cp.st[i];
        for (k = 0; k < 4; k++) {
            st[i].cin[k] = S->c[k].reg | (S->c[k].alpha << 4) | (S->c[k].map << 8);
            st[i].ain[k] = S->a[k].reg | (S->a[k].alpha << 4) | (S->a[k].map << 8);
        }
        st[i].f1[1] = (S->abdot << 1) | S->cddot | (S->mux << 2) | (S->amux << 3);
        st[i].f1[2] = S->op; st[i].f1[3] = S->aop;
        st[i].f2[0] = S->dab; st[i].f2[1] = S->dcd; st[i].f2[2] = S->dsum;
        st[i].f3[0] = S->aab; st[i].f3[1] = S->acd; st[i].f3[2] = S->asum;
        st[i].f3[3] = (S->ab_b2a << 1) | S->cd_b2a;
        memcpy(st[i].c0, s_cf0[i], 16); memcpy(st[i].c1, s_cf1[i], 16);
    }
#define PACK(c) ((c).reg | ((c).alpha << 4) | ((c).map << 8))
    tail.fin0[0] = PACK(s_cp.fA); tail.fin0[1] = PACK(s_cp.fB); tail.fin0[2] = PACK(s_cp.fC); tail.fin0[3] = PACK(s_cp.fD);
    tail.fin1[0] = PACK(s_cp.fE); tail.fin1[1] = PACK(s_cp.fF); tail.fin1[2] = PACK(s_cp.fG); tail.fin1[3] = s_cp.has_final;
#undef PACK
    tail.misc[0] = s_cp.n; tail.misc[1] = s_cp.muxmsb; tail.misc[2] = s_cp.clamp_sum; tail.misc[3] = s_3d.comb_seen;
    memcpy(tail.ff0, s_ff0, 16); memcpy(tail.ff1, s_ff1, 16);
    memcpy(tail.fogc, s_fogc, 16);
    tail.fogp[0] = s_3d.fog_params[0]; tail.fogp[1] = s_3d.fog_params[1];
    tail.fogp[2] = (float)s_3d.fog_mode; tail.fogp[3] = s_3d.fog_enable ? 1.0f : 0.0f;
    tail.atest[0] = s_3d.alpha_test; tail.atest[1] = (int)s_3d.alpha_func; tail.atest[2] = (int)s_3d.alpha_ref;
    tail.atest[3] = s_stage[0].enabled;

    for (k = 0; k < 4; k++) {
        const SwStage *S = &s_stage[k];
        int fl = 0, isd = 0;
        GSurf *gs;
        tail.smode[k] = (int)S->mode;
        tail.sdot[k] = k ? (int)((s_dot_map >> (4 * (k - 1))) & 0xF) : 0;
        tail.sinp[k] = k == 2 ? (int)((s_other_in >> 20) & 1) : k == 3 ? (int)((s_other_in >> 21) & 3) : 0;
        if (S->enabled) fl |= 1;
        if (s_tex_used[k] || (S->mode && S->mode != 1 && S->mode != 2)) fl |= 2;
        if (S->normalized) fl |= 4;
        tail.sscale[k][0] = S->t.width ? 1.0f / (float)S->t.width : 1.0f;
        tail.sscale[k][1] = S->t.height ? 1.0f / (float)S->t.height : 1.0f;
        if ((fl & 1) && (fl & 2)) {
            gs = gpu_surface_for_texture(S->t.offset, S->t.width, S->t.height, &isd);
            if (getenv("RECOMP_SURF_LOG") && (gs || (S->t.width <= 256 && S->t.height <= 256 && S->t.offset < 0x04000000u
                                                     && gsurf_near(S->t.offset)))) {
                static uint32_t seen[64]; static int ns; int q;
                uint32_t key = S->t.offset ^ (s_tex_reg[k * 0x10 + 1] << 4);
                for (q = 0; q < ns && seen[q] != key; q++) {}
                if (q == ns && ns < 64) {
                    seen[ns++] = key;
                    fprintf(stderr, "  [SURF] stage %d samples %08X %ux%u fmt %08X mode %u -> %s\n", k, S->t.offset,
                            S->t.width, S->t.height, s_tex_reg[k * 0x10 + 1], S->mode,
                            gs ? (isd ? "zeta surface" : "colour surface") : "memory (near a surface!)");
                }
            }
            if (gs && !isd) gsurf_sync_from_mem(gs);
            if (gs) gsurf_resolve(gs);   /* (MSAA: the texture from its samples) */
            if (gs) {
                /* a surface bound as a target reads as zero, as in D3D */
                tex[k] = (gs == g_cur_rt || gs == g_cur_ds) ? g_tex_black : gs->tex;
                if (isd && tex[k] != g_tex_black) fl |= 8;
                if (!S->normalized) { tail.sscale[k][0] = 1.0f / (float)gs->w; tail.sscale[k][1] = 1.0f / (float)gs->h; }
            } else {
                tex[k] = gtex_get(&S->t);
            }
            if (!tex[k]) fl &= ~1;
            /* (a pack picture: always through its levels) */
            smp[k] = gpu_sampler_ex(S->t.addr_u, S->t.addr_v, S->bilinear || (!gs && s_ultra_used),
                                    (S->mip || (!gs && s_ultra_used)) && !gs && S->normalized);
        }
        tail.sflags[k] = fl;
    }
    {
        PsKey *K = &g_pskey;
        memset(K, 0, sizeof *K);
        K->misc[0] = tail.misc[0]; K->misc[1] = tail.misc[1]; K->misc[2] = tail.misc[2]; K->misc[3] = tail.misc[3];
        if (K->misc[3]) {
            for (i = 0; i < s_cp.n && i < 8; i++) {
                memcpy(&K->st[i][0], st[i].cin, 16); memcpy(&K->st[i][4], st[i].ain, 16);
                memcpy(&K->st[i][8], st[i].f1, 16); memcpy(&K->st[i][12], st[i].f2, 16);
                memcpy(&K->st[i][16], st[i].f3, 16);
            }
            memcpy(K->fin0, tail.fin0, 16); memcpy(K->fin1, tail.fin1, 16);
        } else {
            K->misc[0] = K->misc[1] = K->misc[2] = 0;
            K->at_s0 = tail.atest[3] != 0;
        }
        K->fogen = s_3d.fog_enable ? 1 : 0;
        K->fogmode = K->fogen ? (int)s_3d.fog_mode : 0;
        memcpy(K->smode, tail.smode, 16); memcpy(K->sflags, tail.sflags, 16);
        K->at_en = tail.atest[0] ? 1 : 0;
        K->at_func = K->at_en ? tail.atest[1] : 0;
        memcpy(K->sdot, tail.sdot, 16); memcpy(K->sinp, tail.sinp, 16);
    }
    memcpy(cb, st, sizeof st);
    memcpy(cb + sizeof st, &tail, sizeof tail);
    if (!g_sh.cbps_ok || memcmp(g_sh.cbps, cb, sizeof cb)) {
        gpu_ub(UB_PSC, cb, sizeof cb);
        memcpy(g_sh.cbps, cb, sizeof cb); g_sh.cbps_ok = 1;
    }
    if (!g_sh.tex_ok || memcmp(g_sh.tex, tex, sizeof tex) || memcmp(g_sh.smp, smp, sizeof smp)) {
        for (k = 0; k < 4; k++) {
            glActiveTexture(GL_TEXTURE0 + k);
            glBindTexture(GL_TEXTURE_2D, tex[k]);
            glBindSampler((GLuint)k, smp[k]);
        }
        memcpy(g_sh.tex, tex, sizeof tex); memcpy(g_sh.smp, smp, sizeof smp); g_sh.tex_ok = 1;
    }
}

/* ---------------- vertices on the CPU ---------------- */

typedef struct { float p[4], d0[4], d1[4], fog[4], t[4][4]; } GVert;
static GVert *g_verts;
static uint32_t *g_idx, g_verts_cap, g_idx_cap;
static uint32_t *g_slot, *g_slot_gen;
static uint32_t g_gen = 1;
static uint32_t *g_vidx, g_vidx_cap;
static uint8_t  *g_vok;

static uint32_t gpu_vertex_slot(uint32_t index, uint32_t *nverts)
{
    uint32_t slot = index & (SW3D_CACHE - 1);
    if (g_slot_gen[slot] == g_gen && g_slot[slot * 2] == index) return g_slot[slot * 2 + 1];
    if (*nverts == g_vidx_cap) {
        uint32_t nc = g_vidx_cap ? g_vidx_cap * 2 : 4096;
        uint32_t *ni = (uint32_t *)realloc(g_vidx, nc * 4);
        uint8_t *no = (uint8_t *)realloc(g_vok, nc);
        if (ni) g_vidx = ni;
        if (no) g_vok = no;
        if (!ni || !no) return 0xFFFFFFFFu;
        g_vidx_cap = nc;
    }
    g_vidx[*nverts] = index;
    g_slot_gen[slot] = g_gen;
    g_slot[slot * 2] = index;
    g_slot[slot * 2 + 1] = *nverts;
    return (*nverts)++;
}

static void gpu_xform_range(uint32_t lo, uint32_t hi)
{
    uint32_t k;
    for (k = lo; k < hi; k++) {
        SwVert v;
        GVert *g = &g_verts[k];
        if (!sw3d_transform(g_vidx[k], &v)) { g_vok[k] = 0; memset(g, 0, sizeof *g); continue; }
        if (v.p[3] == 0.0f) v.p[3] = 1e-30f;
        g_vok[k] = 1;
        memcpy(g->p, v.p, 16);
        memcpy(g->d0, &v.a[SA_D0], 16);
        memcpy(g->d1, &v.a[SA_D1], 16);
        g->fog[0] = v.a[SA_FOG]; g->fog[1] = g->fog[2] = g->fog[3] = 0;
        memcpy(g->t, &v.a[SA_T0], 64);
    }
}

/* Transform workers (as nv2a_d3d11.inc): 64-vertex chunks, on the cores
 * other than CPU 0. RECOMP_VTX_THREADS=n (0 = off) overrides the count. */
#define VX_MAX 8
#define VX_CHUNK 64
static int g_vx_n = -1;
static volatile LONG g_vx_gen, g_vx_next, g_vx_busy;
static volatile uint32_t g_vx_total;
static HANDLE g_vx_wake[VX_MAX];
static volatile LONG g_vx_sleeping[VX_MAX];
static void gpu_vx_work(void)
{
    for (;;) {
        LONG c = InterlockedIncrement(&g_vx_next) - 1;
        uint32_t lo = (uint32_t)c * VX_CHUNK, hi;
        if (lo >= g_vx_total) break;
        hi = lo + VX_CHUNK; if (hi > g_vx_total) hi = g_vx_total;
        gpu_xform_range(lo, hi);
    }
}
static DWORD WINAPI gpu_vx_thread(LPVOID arg)
{
    int idx = (int)(intptr_t)arg;
    LONG seen = 0;
    DWORD_PTR pm = 0, sm = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm) && (pm & ~(DWORD_PTR)1))
        SetThreadAffinityMask(GetCurrentThread(), pm & ~(DWORD_PTR)1);
    for (;;) {
        int spins = 0;
        while (g_vx_gen == seen) {
            if (++spins < 20000) { YieldProcessor(); continue; }
            InterlockedExchange(&g_vx_sleeping[idx], 1);
            if (g_vx_gen == seen) WaitForSingleObject(g_vx_wake[idx], 20);
            InterlockedExchange(&g_vx_sleeping[idx], 0);
            spins = 0;
        }
        seen = g_vx_gen;
        gpu_vx_work();
        InterlockedDecrement(&g_vx_busy);
    }
    return 0;
}
static void gpu_vx_init(void)
{
    DWORD_PTR pm = 0, sm = 0;
    int i, cpus = 0;
    const char *e = getenv("RECOMP_VTX_THREADS");
    g_vx_n = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm))
        for (i = 1; i < (int)(8 * sizeof(DWORD_PTR)); i++) if (pm & ((DWORD_PTR)1 << i)) cpus++;
    g_vx_n = cpus > 4 ? 4 : cpus;
    if (cpus >= 2 && g_vx_n > cpus - 1) g_vx_n = cpus - 1;
    if (e) { g_vx_n = atoi(e); if (g_vx_n < 0) g_vx_n = 0; if (g_vx_n > VX_MAX) g_vx_n = VX_MAX; }
    for (i = 0; i < g_vx_n; i++) {
        HANDLE h;
        g_vx_wake[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
        h = CreateThread(NULL, 0, gpu_vx_thread, (LPVOID)(intptr_t)i, 0, NULL);
        if (!h) { g_vx_n = i; break; }
        SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
        CloseHandle(h);
    }
    fprintf(stderr, "  [GPU3D] %d vertex transform thread(s) besides the caller\n", g_vx_n);
}

static void gpu_transform_all(uint32_t nverts)
{
    int i;
    if (nverts > g_verts_cap) {
        uint32_t nc = g_verts_cap ? g_verts_cap : 4096;
        GVert *nv;
        while (nc < nverts) nc *= 2;
        nv = (GVert *)realloc(g_verts, nc * sizeof(GVert));
        if (!nv) { nverts = g_verts_cap; } else { g_verts = nv; g_verts_cap = nc; }
    }
    s_3d.verts += nverts;
    if (g_vx_n < 0) gpu_vx_init();
    if (g_vx_n <= 0 || nverts < 2 * VX_CHUNK || (s_3d.mode == 2 && (s_vp_slow || s_vpd_cwrite))) {
        gpu_xform_range(0, nverts);
        return;
    }
    g_vx_total = nverts;
    g_vx_next = 0;
    g_vx_busy = g_vx_n;
    MemoryBarrier();
    InterlockedIncrement(&g_vx_gen);
    for (i = 0; i < g_vx_n; i++)
        if (g_vx_sleeping[i]) SetEvent(g_vx_wake[i]);
    gpu_vx_work();
    while (g_vx_busy > 0) YieldProcessor();
}

static uint32_t g_nidx;
static void gpu_tri(uint32_t a, uint32_t b, uint32_t c, uint32_t *nverts)
{
    uint32_t sa = gpu_vertex_slot(a, nverts), sb, sc;
    if (sa == 0xFFFFFFFFu) return;
    sb = gpu_vertex_slot(b, nverts); if (sb == 0xFFFFFFFFu) return;
    sc = gpu_vertex_slot(c, nverts); if (sc == 0xFFFFFFFFu) return;
    if (g_nidx + 3 > g_idx_cap) {
        uint32_t nc = g_idx_cap ? g_idx_cap * 2 : 16384;
        uint32_t *ni = (uint32_t *)realloc(g_idx, nc * 4);
        if (!ni) return;
        g_idx = ni; g_idx_cap = nc;
    }
    g_idx[g_nidx++] = sa; g_idx[g_nidx++] = sb; g_idx[g_nidx++] = sc;
}

static void gpu_drop_bad_tris(uint32_t nverts)
{
    uint32_t k, o = 0, bad = 0;
    for (k = 0; k < nverts; k++) if (!g_vok[k]) { bad = 1; break; }
    if (!bad) return;
    for (k = 0; k + 2 < g_nidx; k += 3) {
        if (!g_vok[g_idx[k]] || !g_vok[g_idx[k + 1]] || !g_vok[g_idx[k + 2]]) continue;
        g_idx[o++] = g_idx[k]; g_idx[o++] = g_idx[k + 1]; g_idx[o++] = g_idx[k + 2];
    }
    g_nidx = o;
}

/* ---------------- vertex programs as GLSL ---------------- */

/* As nv2a_d3d11_vp.inc: each program the title uploads is written out as a
 * vertex shader, compiled in the background, and once ready the batch's
 * vertices go to the GPU as raw inputs. Until then (or when the program
 * writes constants) the CPU path draws it. [Display] vertex_programs=cpu /
 * RECOMP_HW_VP=0 turns it off. */
typedef struct {
    uint32_t hash, len, state;        /* state: 0 compiling, 1 ready, 2 failed */
    uint32_t words[VP_SLOTS][4];
    uint32_t nin;
    uint8_t  inreg[16];
    GLuint vs;
} HwVp;

#define HWVP_MAX 512
static HwVp *g_hwvp[HWVP_MAX];
static int g_hwvp_n;
static int g_hwvp_on = -1;
static float g_vpc_shadow[VP_CONSTS][4];
static int g_vpc_ok;
static float *g_xin; static uint32_t g_xin_cap;
static uint32_t g_hwvp_stat_hw, g_hwvp_stat_cpu, g_hwvp_compiled, g_hwvp_failed;

static uint32_t hwvp_hash(const uint32_t (*w)[4], uint32_t len)
{
    uint32_t h = 2166136261u, i, k;
    for (i = 0; i < len; i++) for (k = 0; k < 4; k++) h = (h ^ w[i][k]) * 16777619u;
    return h ^ len;
}

#include <stdarg.h>
static void hwvp_cat(char **p, size_t *cap, size_t *len, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (!*p) return;
    for (;;) {
        char *np;
        va_start(ap, fmt);
        n = vsnprintf(*p + *len, *cap - *len, fmt, ap);
        va_end(ap);
        if (n >= 0 && (size_t)n < *cap - *len) { *len += n; return; }
        *cap *= 2;
        np = (char *)realloc(*p, *cap);
        if (!np) { free(*p); *p = NULL; return; }
        *p = np;
    }
}

static const char *hwvp_src_expr(const VpSrc *d, char *buf, size_t bs)
{
    static const char sw[] = "xyzw";
    char base[64];
    switch (d->mux) {
    case 1: if (d->reg == 12) snprintf(base, sizeof base, "o[0]"); else snprintf(base, sizeof base, "R[%u]", d->reg); break;
    case 2: snprintf(base, sizeof base, "v%u", d->reg); break;
    case 3:
        if (d->rel) snprintf(base, sizeof base, "kc(%d + a0)", d->ci);
        else if (d->ci >= 0 && d->ci < VP_CONSTS) snprintf(base, sizeof base, "c[%d]", d->ci);
        else snprintf(base, sizeof base, "vec4(0.0, 0.0, 0.0, 1.0)");
        break;
    default: snprintf(base, sizeof base, "vec4(0.0, 0.0, 0.0, 1.0)"); break;
    }
    snprintf(buf, bs, "%s(%s).%c%c%c%c", d->neg ? "-" : "", base,
             sw[d->sw[0]], sw[d->sw[1]], sw[d->sw[2]], sw[d->sw[3]]);
    return buf;
}

static const char *hwvp_mask(uint32_t m, char *b)
{
    int n = 0;
    b[n++] = '.';
    if (m & 8) b[n++] = 'x';
    if (m & 4) b[n++] = 'y';
    if (m & 2) b[n++] = 'z';
    if (m & 1) b[n++] = 'w';
    b[n] = 0;
    return b;
}

/* The decoded program (s_vpd) as GLSL; NULL if it cannot run on the GPU (it
 * writes constants). The arithmetic is nv2a_d3d11_vp.inc's, line for line. */
static char *hwvp_translate(HwVp *e)
{
    size_t cap = 16384, len = 0;
    char *p = (char *)malloc(cap);
    uint32_t k, used = 0, i;
    char a[96], b[96], c[96], mk[8];
    if (!p) return NULL;
    p[0] = 0;
    for (k = 0; k < s_vpd_len; k++) {
        const VpIns *I = &s_vpd[k];
        int s;
        if (I->o_mask && !I->orb) { free(p); return NULL; }
        for (s = 0; s < 3; s++) if (I->s[s].mux == 2) used |= 1u << I->s[s].reg;
    }
    e->nin = 0;
    for (i = 0; i < 16; i++) if (used & (1u << i)) e->inreg[e->nin++] = (uint8_t)i;

    hwvp_cat(&p, &cap, &len,
        "#version 330 core\n"
        "layout(std140) uniform VSC { vec4 vp; };\n"
        "layout(std140) uniform VPC { vec4 c[192]; };\n"
        "vec4 kc(int i) { return (i >= 0 && i < 192) ? c[i] : vec4(0.0, 0.0, 0.0, 1.0); }\n"
        "vec4 rcc(float x) { float r = 1.0 / x; float ar = abs(r);\n"
        "  ar = (ar < 5.42101e-20) ? 5.42101e-20 : ((ar > 1.884467e+19) ? 1.884467e+19 : ar);\n"
        "  r = (r < 0.0 || (r == 0.0 && floatBitsToUint(r) == 0x80000000u)) ? -ar : ar; return vec4(r); }\n"
        "vec4 vexp(float x) { float fl = floor(x); return vec4(exp2(fl), x - fl, exp2(x), 1.0); }\n"
        "vec4 vlog(float x) { x = abs(x); if (x == 0.0) return vec4(-uintBitsToFloat(0x7f800000u), 1.0, -uintBitsToFloat(0x7f800000u), 1.0);\n"
        "  float e = floor(log2(x)); return vec4(e, x / exp2(e), log2(x), 1.0); }\n"
        "vec4 vlit(vec4 s) { float nl = max(s.x, 0.0), nh = max(s.y, 0.0); float pw = clamp(s.w, -127.9961, 127.9961);\n"
        "  return vec4(1.0, nl, (s.x > 0.0) ? pow(nh, pw) : 0.0, 1.0); }\n");
    for (i = 0; i < e->nin; i++) hwvp_cat(&p, &cap, &len, "layout(location = %u) in vec4 v%u;\n", i, e->inreg[i]);
    hwvp_cat(&p, &cap, &len,
        "out vec4 v_d0; out vec4 v_d1; out vec4 v_fog; out vec4 v_t0; out vec4 v_t1; out vec4 v_t2; out vec4 v_t3;\n"
        "invariant gl_Position;\n"
        "void main() {\n"
        "  vec4 R[16]; vec4 o[13]; int a0 = 0; vec4 m, l;\n"
        "  for (int q = 0; q < 16; q++) R[q] = vec4(0.0);\n"
        "  for (int q2 = 0; q2 < 13; q2++) o[q2] = vec4(0.0, 0.0, 0.0, 1.0);\n"
        "  o[3] = vec4(1.0);\n");
    for (k = 0; k < s_vpd_len; k++) {
        const VpIns *I = &s_vpd[k];
        hwvp_src_expr(&I->s[0], a, sizeof a);
        hwvp_src_expr(&I->s[1], b, sizeof b);
        hwvp_src_expr(&I->s[2], c, sizeof c);
        hwvp_cat(&p, &cap, &len, "  m = vec4(0.0); l = vec4(0.0);\n");
        switch (I->mac) {
        case MAC_MOV: hwvp_cat(&p, &cap, &len, "  m = %s;\n", a); break;
        case MAC_MUL: hwvp_cat(&p, &cap, &len, "  m = %s * %s;\n", a, b); break;
        case MAC_ADD: hwvp_cat(&p, &cap, &len, "  m = %s + %s;\n", a, c); break;
        case MAC_MAD: hwvp_cat(&p, &cap, &len, "  m = %s * %s + %s;\n", a, b, c); break;
        case MAC_DP3: hwvp_cat(&p, &cap, &len, "  m = vec4(dot((%s).xyz, (%s).xyz));\n", a, b); break;
        case MAC_DPH: hwvp_cat(&p, &cap, &len, "  { vec4 A_ = %s, B_ = %s; m = vec4(dot(A_.xyz, B_.xyz) + B_.w); }\n", a, b); break;
        case MAC_DP4: hwvp_cat(&p, &cap, &len, "  m = vec4(dot(%s, %s));\n", a, b); break;
        case MAC_DST: hwvp_cat(&p, &cap, &len, "  { vec4 A_ = %s, B_ = %s; m = vec4(1.0, A_.y * B_.y, A_.z, B_.w); }\n", a, b); break;
        case MAC_MIN: hwvp_cat(&p, &cap, &len, "  m = min(%s, %s);\n", a, b); break;
        case MAC_MAX: hwvp_cat(&p, &cap, &len, "  m = max(%s, %s);\n", a, b); break;
        case MAC_SLT: hwvp_cat(&p, &cap, &len, "  m = vec4(lessThan(%s, %s));\n", a, b); break;
        case MAC_SGE: hwvp_cat(&p, &cap, &len, "  m = vec4(greaterThanEqual(%s, %s));\n", a, b); break;
        case MAC_ARL: hwvp_cat(&p, &cap, &len, "  a0 = int(floor((%s).x + 0.001));\n", a); break;
        default: break;
        }
        switch (I->ilu) {
        case ILU_MOV: hwvp_cat(&p, &cap, &len, "  l = %s;\n", c); break;
        case ILU_RCP: hwvp_cat(&p, &cap, &len, "  l = vec4(1.0 / (%s).x);\n", c); break;
        case ILU_RCC: hwvp_cat(&p, &cap, &len, "  l = rcc((%s).x);\n", c); break;
        case ILU_RSQ: hwvp_cat(&p, &cap, &len, "  l = vec4(1.0 / sqrt(abs((%s).x)));\n", c); break;
        case ILU_EXP: hwvp_cat(&p, &cap, &len, "  l = vexp((%s).x);\n", c); break;
        case ILU_LOG: hwvp_cat(&p, &cap, &len, "  l = vlog((%s).x);\n", c); break;
        case ILU_LIT: hwvp_cat(&p, &cap, &len, "  l = vlit(%s);\n", c); break;
        default: break;
        }
        if (I->mac && I->mac != MAC_ARL && I->mac_mask && I->out_r < 16 && I->out_r != 12)
            hwvp_cat(&p, &cap, &len, "  R[%u]%s = m%s;\n", I->out_r, hwvp_mask(I->mac_mask, mk), mk);
        if (I->ilu && I->ilu_mask) {
            uint32_t d = I->mac ? 1 : I->out_r;
            if (d < 16 && d != 12) hwvp_cat(&p, &cap, &len, "  R[%u]%s = l%s;\n", d, hwvp_mask(I->ilu_mask, mk), mk);
        }
        if (I->o_mask && I->orb && I->oaddr < VP_OUTS)
            hwvp_cat(&p, &cap, &len, "  o[%u]%s = %s%s;\n", I->oaddr, hwvp_mask(I->o_mask, mk), I->omux ? "l" : "m", mk);
        if (I->final) break;
    }
    hwvp_cat(&p, &cap, &len,
        "  float w = o[0].w; if (w == 0.0) w = 1e-30;\n"
        "  vec4 P = vec4(o[0].xyz * w, w);\n"
        "  gl_Position = vec4(P.x * vp.x - P.w, P.y * vp.y - P.w, 2.0 * P.z * vp.z - P.w, P.w);\n"
        "  v_d0 = clamp(o[3], 0.0, 1.0); v_d1 = clamp(o[4], 0.0, 1.0); v_fog = vec4(o[5].x, 0.0, 0.0, 0.0);\n"
        "  v_t0 = o[9]; v_t1 = o[10]; v_t2 = o[11]; v_t3 = o[12];\n"
        "}\n");
    return p;
}

static int hwvp_enabled(void)
{
    if (g_hwvp_on < 0) {
        /* set up even when the ini says cpu: the overlay can switch later */
        g_hwvp_on = 1;
        fprintf(stderr, "  [HWVP] vertex programs on the GPU: %s\n", g_hwvp_user ? "on" : "off");
    }
    return g_hwvp_on;
}

static void hwvp_poll(HwVp *e)
{
    if (e->state == 0 && e->vs) {
        int r = gpu_shader_done(&e->vs, "vertex program", e->hash);
        if (r > 0) { e->state = 1; g_hwvp_compiled++; }
        else if (r < 0) { e->state = 2; g_hwvp_failed++; }
    }
}

/* The shader for the current program, or NULL to use the CPU path. */
static HwVp *hwvp_lookup(void)
{
    uint32_t h, i;
    HwVp *e;
    if (!g_hwvp_user || !hwvp_enabled() || s_3d.mode != 2 || s_vp_slow || !s_vpd_len) return NULL;
    h = hwvp_hash((const uint32_t (*)[4])s_vpd_src, s_vpd_len);
    for (i = 0; i < (uint32_t)g_hwvp_n; i++) {
        e = g_hwvp[i];
        if (e->hash == h && e->len == s_vpd_len && !memcmp(e->words, s_vpd_src, s_vpd_len * 16)) {
            hwvp_poll(e);
            return e->state == 1 ? e : NULL;
        }
    }
    if (g_hwvp_n >= HWVP_MAX) return NULL;
    e = (HwVp *)calloc(1, sizeof *e);
    if (!e) return NULL;
    e->hash = h; e->len = s_vpd_len;
    memcpy(e->words, s_vpd_src, s_vpd_len * 16);
    {
        char *src = hwvp_translate(e);
        if (!src) e->state = 2;
        else { e->vs = gpu_shader_start(GL_VERTEX_SHADER, src); free(src); }
    }
    g_hwvp[g_hwvp_n++] = e;
    hwvp_poll(e);
    return e->state == 1 ? e : NULL;
}

static void hwvp_pack_range(const HwVp *e, uint32_t lo, uint32_t hi)
{
    uint32_t k, i;
    for (k = lo; k < hi; k++) {
        float *d = g_xin + (size_t)k * 4 * e->nin;
        for (i = 0; i < e->nin; i++) fetch_attr_any(e->inreg[i], g_vidx[k], d + 4 * i);
    }
}

/* ---------------- combiner programs as GLSL ---------------- */

/* As nv2a_d3d11_ps.inc: each distinct combiner program written out as
 * straight-line code with its registers, mappings, operations, texture modes,
 * fog mode and alpha test fixed; its constants still come from PSC.
 * RECOMP_HW_PS=0 turns it off. */
typedef struct {
    PsKey key;
    uint32_t hash, state;             /* 0 compiling, 1 ready, 2 failed */
    GLuint fs;
} HwPs;

#define HWPS_MAX 1024
static HwPs *g_hwps[HWPS_MAX];
static volatile LONG g_hwps_n;
static int g_hwps_on = -1;
static uint32_t g_hwps_stat_hw, g_hwps_stat_sw, g_hwps_compiled, g_hwps_failed;

static const char *ps_cmap(int m, const char *x, char *b, size_t n)
{
    switch (m & 7) {
    case 0: snprintf(b, n, "max(%s, 0.0)", x); break;
    case 1: snprintf(b, n, "(1.0 - clamp(%s, 0.0, 1.0))", x); break;
    case 2: snprintf(b, n, "(2.0 * max(%s, 0.0) - 1.0)", x); break;
    case 3: snprintf(b, n, "(-2.0 * max(%s, 0.0) + 1.0)", x); break;
    case 4: snprintf(b, n, "(max(%s, 0.0) - 0.5)", x); break;
    case 5: snprintf(b, n, "(-max(%s, 0.0) + 0.5)", x); break;
    case 6: snprintf(b, n, "(%s)", x); break;
    default: snprintf(b, n, "(-(%s))", x); break;
    }
    return b;
}
static const char *ps_crgb(int c, char *b, size_t n)
{
    char x[32], t[96];
    if ((c >> 4) & 1) { snprintf(x, sizeof x, "R%d.a", c & 15); snprintf(b, n, "vec3(%s)", ps_cmap(c >> 8, x, t, sizeof t)); }
    else { snprintf(x, sizeof x, "R%d.rgb", c & 15); ps_cmap(c >> 8, x, b, n); }
    return b;
}
static const char *ps_calp(int c, char *b, size_t n)
{
    char x[32];
    snprintf(x, sizeof x, "R%d.%c", c & 15, ((c >> 4) & 1) ? 'a' : 'b');
    return ps_cmap(c >> 8, x, b, n);
}
static const char *ps_cop(int op, const char *x, char *b, size_t n)
{
    switch (op) {
    case 1: snprintf(b, n, "clamp((%s) - 0.5, -1.0, 1.0)", x); break;
    case 2: snprintf(b, n, "clamp((%s) * 2.0, -1.0, 1.0)", x); break;
    case 3: snprintf(b, n, "clamp(((%s) - 0.5) * 2.0, -1.0, 1.0)", x); break;
    case 4: snprintf(b, n, "clamp((%s) * 4.0, -1.0, 1.0)", x); break;
    case 6: snprintf(b, n, "clamp((%s) * 0.5, -1.0, 1.0)", x); break;
    default: snprintf(b, n, "clamp(%s, -1.0, 1.0)", x); break;
    }
    return b;
}

static void ps_gen_stex(char **p, size_t *cap, size_t *len, const PsKey *K, int k)
{
    int mode = K->smode[k], fl = K->sflags[k];
    if (mode == 0) { hwvp_cat(p, cap, len, "  R%d = vec4(0.0);\n", 8 + k); return; }
    if (mode == 4) { hwvp_cat(p, cap, len, "  R%d = clamp(v_t%d, 0.0, 1.0);\n", 8 + k, k); return; }
    if (!(fl & 1)) { hwvp_cat(p, cap, len, "  R%d = vec4(0.0, 0.0, 0.0, 1.0);\n", 8 + k); return; }
    hwvp_cat(p, cap, len, "  { vec2 uv = v_t%d.xy;\n", k);
    if (mode == 1) hwvp_cat(p, cap, len, "    if (v_t%d.w != 0.0 && v_t%d.w != 1.0) uv /= v_t%d.w;\n", k, k, k);
    if (!(fl & 4)) hwvp_cat(p, cap, len, "    uv *= sscale[%d].xy;\n", k);
    if (fl & 8) hwvp_cat(p, cap, len, "    R%d = zread(tx%d, uv); }\n", 8 + k, k);
    else hwvp_cat(p, cap, len, "    R%d = texture(tx%d, uv); }\n", 8 + k, k);
}

static const char *ps_dnorm(int m, const char *c, char *b, size_t n)
{
    if (m == 0) snprintf(b, n, "%s.rgb", c);
    else if (m == 3) snprintf(b, n, "dnorm(%s, 3)", c);
    else snprintf(b, n, "(%s.rgb * 2.0 - 1.0)", c);
    return b;
}

static char *hwps_translate(const PsKey *K)
{
    size_t cap = 8192, len = 0;
    char *p = (char *)malloc(cap), a[160], b2[160], c[160], d[160], t[256];
    int k;
    if (!p) return NULL;
    p[0] = 0;
    hwvp_cat(&p, &cap, &len, "%s", s_glsl_ps_head);
    hwvp_cat(&p, &cap, &len, "void main() {\n"
                             "  vec4 R0 = vec4(0.0), R1 = vec4(0.0), R2 = vec4(0.0), R3 = vec4(0.0), R4 = vec4(0.0), R5 = vec4(0.0), R6 = vec4(0.0), R7 = vec4(0.0);\n"
                             "  vec4 R8 = vec4(0.0), R9 = vec4(0.0), R10 = vec4(0.0), R11 = vec4(0.0), R12 = vec4(0.0), R13 = vec4(0.0), R14 = vec4(0.0), R15 = vec4(0.0);\n"
                             "  float dp1 = 0.0, dp2 = 0.0;\n");
    if (K->sflags[0] & 2) ps_gen_stex(&p, &cap, &len, K, 0);
    if (K->smode[1] == 17) hwvp_cat(&p, &cap, &len, "  dp1 = dot(v_t1.xyz, %s); R9 = vec4(0.0);\n", ps_dnorm(K->sdot[1], "R8", a, sizeof a));
    else if (K->sflags[1] & 2) ps_gen_stex(&p, &cap, &len, K, 1);
    if (K->smode[2] == 17)
        hwvp_cat(&p, &cap, &len, "  dp2 = dot(v_t2.xyz, %s); R10 = vec4(0.0);\n", ps_dnorm(K->sdot[2], K->sinp[2] ? "R9" : "R8", a, sizeof a));
    else if (K->smode[2] == 9) {
        if (K->sflags[2] & 1)
            hwvp_cat(&p, &cap, &len, "  R10 = texture(tx2, vec2(dp1, dot(v_t2.xyz, %s)));\n",
                     ps_dnorm(K->sdot[2], K->sinp[2] ? "R9" : "R8", a, sizeof a));
        else hwvp_cat(&p, &cap, &len, "  R10 = vec4(0.0, 0.0, 0.0, 1.0);\n");
    } else if (K->sflags[2] & 2) ps_gen_stex(&p, &cap, &len, K, 2);
    if (K->smode[3] == 9) {
        const char *nn = K->sinp[3] == 2 ? "R10" : K->sinp[3] == 1 ? "R9" : "R8";
        if (K->sflags[3] & 1)
            hwvp_cat(&p, &cap, &len, "  R11 = texture(tx3, vec2(dp2, dot(v_t3.xyz, %s)));\n", ps_dnorm(K->sdot[3], nn, a, sizeof a));
        else hwvp_cat(&p, &cap, &len, "  R11 = vec4(0.0, 0.0, 0.0, 1.0);\n");
    } else if (K->sflags[3] & 2) ps_gen_stex(&p, &cap, &len, K, 3);
    hwvp_cat(&p, &cap, &len, "  R4 = v_d0; R5 = v_d1;\n  R3 = vec4(fogc.rgb, 1.0);\n");
    if (K->fogen) {
        int fm = K->fogmode;
        hwvp_cat(&p, &cap, &len, "  { float dd = v_fog.x, f;\n");
        if (fm == 0x800 || fm == 0x802)
            hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + exp2(dd * fogp.y * 16.0) - 1.5;\n", fm == 0x802 ? "    dd = abs(dd);\n" : "");
        else if (fm == 0x801 || fm == 0x803)
            hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + exp2(-dd * dd * fogp.y * fogp.y * 32.0) - 1.5;\n", fm == 0x803 ? "    dd = abs(dd);\n" : "");
        else hwvp_cat(&p, &cap, &len, "%s    f = fogp.x + dd * fogp.y - 1.0;\n", fm == 0x804 ? "    dd = abs(dd);\n" : "");
        hwvp_cat(&p, &cap, &len, "    R3.a = clamp(f, 0.0, 1.0); }\n");
    }
    hwvp_cat(&p, &cap, &len, "  R12.a = R8.a;\n  vec4 o;\n");
    if (K->misc[3] == 0) {
        hwvp_cat(&p, &cap, &len, "  o = %s * R4;\n", K->at_s0 ? "R8" : "vec4(1.0)");
    } else {
        for (k = 0; k < K->misc[0] && k < 8; k++) {
            const int *S = K->st[k];
            int f1y = S[9], op = S[10], aop = S[11];
            hwvp_cat(&p, &cap, &len, "  { R1 = st[%d].c0; R2 = st[%d].c1;\n", k, k);
            hwvp_cat(&p, &cap, &len, "    vec3 A = %s, B = %s, C = %s, D = %s;\n",
                     ps_crgb(S[0], a, sizeof a), ps_crgb(S[1], b2, sizeof b2), ps_crgb(S[2], c, sizeof c), ps_crgb(S[3], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    vec3 ab = %s, cd = %s;\n", (f1y & 2) ? "vec3(dot(A, B))" : "A * B", (f1y & 1) ? "vec3(dot(C, D))" : "C * D");
            if ((f1y & 4) || (f1y & 8))
                hwvp_cat(&p, &cap, &len, "    bool sel = %s;\n", K->misc[1] ? "R12.a >= 0.5" : "((int(R12.a * 255.0 + 0.5)) & 1) != 0");
            hwvp_cat(&p, &cap, &len, "    vec3 sm = %s;\n", (f1y & 4) ? "sel ? cd : ab" : "ab + cd");
            hwvp_cat(&p, &cap, &len, "    vec3 rab = %s;\n", ps_cop(op, "ab", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    vec3 rcd = %s;\n", ps_cop(op, "cd", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    vec3 rsm = %s;\n", ps_cop(op, "sm", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float a0 = %s, a1 = %s, a2 = %s, a3 = %s;\n",
                     ps_calp(S[4], a, sizeof a), ps_calp(S[5], b2, sizeof b2), ps_calp(S[6], c, sizeof c), ps_calp(S[7], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    float aab = a0 * a1, acd = a2 * a3, asm_ = %s;\n", (f1y & 8) ? "sel ? acd : aab" : "aab + acd");
            hwvp_cat(&p, &cap, &len, "    float ra0 = %s;\n", ps_cop(aop, "aab", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float ra1 = %s;\n", ps_cop(aop, "acd", t, sizeof t));
            hwvp_cat(&p, &cap, &len, "    float ra2 = %s;\n", ps_cop(aop, "asm_", t, sizeof t));
            if (S[12]) { hwvp_cat(&p, &cap, &len, "    R%d.rgb = rab;\n", S[12]); if (S[19] & 2) hwvp_cat(&p, &cap, &len, "    R%d.a = rab.b;\n", S[12]); }
            if (S[13]) { hwvp_cat(&p, &cap, &len, "    R%d.rgb = rcd;\n", S[13]); if (S[19] & 1) hwvp_cat(&p, &cap, &len, "    R%d.a = rcd.b;\n", S[13]); }
            if (S[14]) hwvp_cat(&p, &cap, &len, "    R%d.rgb = rsm;\n", S[14]);
            if (S[16]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra0;\n", S[16]);
            if (S[17]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra1;\n", S[17]);
            if (S[18]) hwvp_cat(&p, &cap, &len, "    R%d.a = ra2;\n", S[18]);
            hwvp_cat(&p, &cap, &len, "  }\n");
        }
        if (K->fin1[3] == 0) {
            hwvp_cat(&p, &cap, &len, "  o = clamp(R12, 0.0, 1.0);\n");
        } else {
            hwvp_cat(&p, &cap, &len, "  R1 = ff0; R2 = ff1;\n");
            hwvp_cat(&p, &cap, &len, "  { vec3 E = %s, F = %s;\n    R15.rgb = E * F; }\n",
                     ps_crgb(K->fin1[0], a, sizeof a), ps_crgb(K->fin1[1], b2, sizeof b2));
            hwvp_cat(&p, &cap, &len, "  R14.rgb = R5.rgb + R12.rgb;\n");
            if (K->misc[2]) hwvp_cat(&p, &cap, &len, "  R14.rgb = min(R14.rgb, vec3(1.0));\n");
            hwvp_cat(&p, &cap, &len, "  { vec3 A = %s, B = %s, C = %s, D = %s;\n",
                     ps_crgb(K->fin0[0], a, sizeof a), ps_crgb(K->fin0[1], b2, sizeof b2), ps_crgb(K->fin0[2], c, sizeof c), ps_crgb(K->fin0[3], d, sizeof d));
            hwvp_cat(&p, &cap, &len, "    vec3 sa = clamp(A, 0.0, 1.0);\n    o.rgb = clamp(sa * B + (1.0 - sa) * C + D, 0.0, 1.0);\n");
            hwvp_cat(&p, &cap, &len, "    o.a = clamp(%s, 0.0, 1.0); }\n", ps_calp(K->fin1[2], a, sizeof a));
        }
    }
    if (K->at_en) {
        static const char *cmp[7] = { "false", "a8 < r", "a8 == r", "a8 <= r", "a8 > r", "a8 != r", "a8 >= r" };
        int f = K->at_func & 15;
        if (f < 7)
            hwvp_cat(&p, &cap, &len, "  { int a8 = int(clamp(o.a, 0.0, 1.0) * 255.0 + 0.5), r = atest.z;\n"
                                     "    if (!(%s)) discard; }\n", cmp[f]);
    }
    hwvp_cat(&p, &cap, &len, "  o_color = o;\n}\n");
    return p;
}

static uint32_t hwps_hash(const PsKey *k)
{
    const uint32_t *w = (const uint32_t *)k;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof *k / 4; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}

static void hwps_poll(HwPs *e)
{
    if (e->state == 0 && e->fs) {
        int r = gpu_shader_done(&e->fs, "combiner program", e->hash);
        if (r > 0) { e->state = 1; g_hwps_compiled++; }
        else if (r < 0) { e->state = 2; g_hwps_failed++; }
    }
}

/* The fragment shader for the program in this key: the specialized one when
 * it is compiled, else the general one. */
static GLuint hwps_lookup(const PsKey *k)
{
    uint32_t h, i, n;
    HwPs *e;
    static HwPs *last;
    if (g_hwps_on < 0) {
        const char *v = getenv("RECOMP_HW_PS");
        g_hwps_on = !(v && v[0] == '0');
        fprintf(stderr, "  [HWPS] specialized fragment shaders: %s\n", g_hwps_on ? "on" : "off");
    }
    if (!g_hwps_on) return g_fs_main;
    if (last && !memcmp(&last->key, k, sizeof *k)) {
        hwps_poll(last);
        if (last->state == 1) { g_hwps_stat_hw++; return last->fs; }
        g_hwps_stat_sw++;
        return g_fs_main;
    }
    h = hwps_hash(k);
    n = (uint32_t)g_hwps_n;
    for (i = 0; i < n; i++) {
        e = g_hwps[i];
        if (e->hash == h && !memcmp(&e->key, k, sizeof *k)) {
            last = e;
            hwps_poll(e);
            if (e->state == 1) { g_hwps_stat_hw++; return e->fs; }
            g_hwps_stat_sw++;
            return g_fs_main;
        }
    }
    g_hwps_stat_sw++;
    if (n >= HWPS_MAX) return g_fs_main;
    e = (HwPs *)calloc(1, sizeof *e);
    if (!e) return g_fs_main;
    e->key = *k; e->hash = h;
    {
        char *src = hwps_translate(k);
        if (!src) e->state = 2;
        else { e->fs = gpu_shader_start(GL_FRAGMENT_SHADER, src); free(src); }
    }
    g_hwps[n] = e;
    g_hwps_n = (LONG)(n + 1);
    last = e;
    hwps_poll(e);
    return e->state == 1 ? e->fs : g_fs_main;
}

/* The program for vs with the batch's fragment shader: the specialized one
 * when its pairing is linked, else the general one's, else 0. */
static GLuint gpu_program_for(GLuint vs)
{
    GLuint fs = hwps_lookup(&g_pskey), pr = 0;
    if (fs != g_fs_main) pr = gpu_pair(vs, fs);
    if (!pr) {
        if (fs != g_fs_main) { g_hwps_stat_hw--; g_hwps_stat_sw++; }
        pr = vs == g_vs_main ? g_pr_main : gpu_pair(vs, g_fs_main);
    }
    return pr;
}

static void gpu_use(GLuint prog)
{
    if (g_sh.prog != prog) { glUseProgram(prog); g_sh.prog = prog; }
}

/* ---------------- batches ---------------- */

/* Draw the assembled batch with the program's GPU vertex shader; 0 when its
 * program is not linked yet (the caller draws it on the CPU path). */
static int gpu_batch_hw(HwVp *hv, uint32_t nverts)
{
    uint32_t stride = 16 * hv->nin, vb_at = 0, ib_at;
    uint32_t k;
    GLuint prog;
    gpu_bind_stages();
    prog = gpu_program_for(hv->vs);
    if (!prog) return 0;
    if (hv->nin) {
        size_t need = (size_t)nverts * 4 * hv->nin;
        if (need > g_xin_cap) {
            float *n = (float *)realloc(g_xin, need * sizeof(float) * 2);
            if (!n) return 1;
            g_xin = n; g_xin_cap = (uint32_t)(need * 2);
        }
        hwvp_pack_range(hv, 0, nverts);
        glBindVertexArray(g_vao_x);
        g_sh.vao = 2;
        vb_at = gpu_ring(&g_rvbx, g_xin, (uint32_t)(need * sizeof(float)), stride);
        if (vb_at == 0xFFFFFFFFu) return 1;
        for (k = 0; k < 16; k++) {
            if (k < hv->nin) {
                glEnableVertexAttribArray(k);
                glVertexAttribPointer(k, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (const void *)(uintptr_t)(vb_at + 16 * k));
            } else glDisableVertexAttribArray(k);
        }
    } else {
        glBindVertexArray(g_vao_empty);
        g_sh.vao = 3;
    }
    ib_at = gpu_ring(&g_rib, g_idx, g_nidx * 4, 4);
    if (ib_at == 0xFFFFFFFFu) return 1;
    if (!g_vpc_ok || memcmp(g_vpc_shadow, s_3d.c, sizeof g_vpc_shadow)) {
        memcpy(g_vpc_shadow, s_3d.c, sizeof g_vpc_shadow);
        g_vpc_ok = 1;
        gpu_ub(UB_VPC, g_vpc_shadow, sizeof g_vpc_shadow);
    }
    gpu_set_blend(0);
    gpu_set_depth(g_cur_ds != NULL);
    gpu_use(prog);
    glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)g_nidx, GL_UNSIGNED_INT, (const void *)(uintptr_t)ib_at, 0);
    s_gpu.tris_drawn += g_nidx / 3;
    s_gpu.drawn_offset = s_gpu.color_offset;
    s_3d.verts += nverts;
    g_hwvp_stat_hw++;
    return 1;
}

static void gpu_batch(void)
{
    uint32_t i, n = s_gpu.idx_count, nverts = 0;
    uint32_t vb_at, ib_at;
    int need_depth = s_3d.depth_test || s_3d.stencil_test;

    {
        static int lo = -2, hi;
        if (lo == -2) { const char *e = getenv("RECOMP_GPU_SKIP"); lo = -1; if (e) sscanf(e, "%d,%d", &lo, &hi); }
        if (lo >= 0 && (int)s_3d.batches >= lo && (int)s_3d.batches <= hi) return;
    }
    if (!g_slot) {
        g_slot = (uint32_t *)calloc(SW3D_CACHE * 2, 4);
        g_slot_gen = (uint32_t *)calloc(SW3D_CACHE, 4);
        if (!g_slot || !g_slot_gen) return;
    }
    if (!gpu_bind_targets(need_depth)) return;
    if (!gpu_set_raster()) return;
    g_gen++; if (!g_gen) g_gen = 1;
    g_nidx = 0;
    switch (s_gpu.prim) {
    case NV_PRIM_TRIANGLES:
        for (i = 0; i + 2 < n; i += 3) gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts);
        break;
    case NV_PRIM_TRIANGLE_STRIP:
        for (i = 0; i + 2 < n; i++) {
            if (i & 1) gpu_tri(s_gpu.idx[i+1], s_gpu.idx[i], s_gpu.idx[i+2], &nverts);
            else       gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts);
        }
        break;
    case NV_PRIM_TRIANGLE_FAN:
    case NV_PRIM_POLYGON:
        for (i = 1; i + 1 < n; i++) gpu_tri(s_gpu.idx[0], s_gpu.idx[i], s_gpu.idx[i+1], &nverts);
        break;
    case NV_PRIM_QUADS:
        for (i = 0; i + 3 < n; i += 4) {
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+2], &nverts);
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+2], s_gpu.idx[i+3], &nverts);
        }
        break;
    case NV_PRIM_QUAD_STRIP:
        for (i = 0; i + 3 < n; i += 2) {
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+1], s_gpu.idx[i+3], &nverts);
            gpu_tri(s_gpu.idx[i], s_gpu.idx[i+3], s_gpu.idx[i+2], &nverts);
        }
        break;
    default:
        return;
    }
    if (!g_nidx) return;
    if (s_3d.mode == 2 && s_vpd_len > 3) g_frame_3d = 1;
    if (g_wide_prev && s_3d.mode == 2 && s_vpd_len <= 3 && g_aspect > 1.34f) {
        static int hud = -1;
        if (hud < 0) hud = getenv("RECOMP_HUD_STRETCH") == NULL;
        if (hud) {
            /* the 2D overlay at 4:3 proportions in a widened frame (see
             * nv2a_d3d11.inc) */
            uint32_t k; float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f, kk = g_aspect / (4.0f / 3.0f);
            int samples_frame = 0, st;
            gpu_transform_all(nverts);
            gpu_drop_bad_tris(nverts);
            if (!g_nidx) return;
            for (k = 0; k < nverts; k++) if (g_vok[k]) {
                float x = g_verts[k].p[0] / g_verts[k].p[3], y = g_verts[k].p[1] / g_verts[k].p[3];
                if (x < mnx) mnx = x; if (x > mxx) mxx = x;
                if (y < mny) mny = y; if (y > mxy) mxy = y;
            }
            for (st = 0; st < 4; st++) {
                int isd;
                if (s_stage[st].enabled && gpu_surface_for_texture(s_stage[st].t.offset, s_stage[st].t.width, s_stage[st].t.height, &isd)) samples_frame = 1;
            }
            if (!samples_frame && (mxx - mnx) < 400.0f && (mxx - mnx) * (mxy - mny) < 0.2f * 640.0f * 480.0f
                && g_cur_rt && g_cur_rt->w >= 640)
            {
                hud_note(mnx, mxx, mny, mxy);
                for (k = 0; k < nverts; k++) if (g_vok[k]) {
                    float w = g_verts[k].p[3], x = g_verts[k].p[0] / w;
                    g_verts[k].p[0] = hud_wide_x(x, (mnx + mxx) * 0.5f, (mny + mxy) * 0.5f, kk) * w;
                }
            }
            goto cpu_verts_ready;
        }
    }
    {
        HwVp *hv = hwvp_lookup();
        if (hv) {
            if (gpu_batch_hw(hv, nverts)) return;
        }
        if (s_3d.mode == 2) g_hwvp_stat_cpu++;
    }
    gpu_transform_all(nverts);
    gpu_drop_bad_tris(nverts);
    if (!g_nidx) return;
cpu_verts_ready:
    {
        GLuint prog;
        vb_at = gpu_ring(&g_rvb, g_verts, nverts * sizeof(GVert), sizeof(GVert));
        ib_at = gpu_ring(&g_rib, g_idx, g_nidx * 4, 4);
        if (vb_at == 0xFFFFFFFFu || ib_at == 0xFFFFFFFFu) return;
        gpu_bind_stages();
        gpu_set_blend(0);
        gpu_set_depth(g_cur_ds != NULL);
        if (g_sh.vao != 1) {
            /* the CPU path's vertex layout: 8 float4s (GVert) */
            glBindVertexArray(g_vao);
            glBindBuffer(GL_ARRAY_BUFFER, g_rvb.buf);
            for (i = 0; i < 8; i++) {
                glEnableVertexAttribArray(i);
                glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(GVert), (const void *)(uintptr_t)(16 * i));
            }
            g_sh.vao = 1;
        }
        prog = gpu_program_for(g_vs_main);
        gpu_use(prog);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_rib.buf);
        glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)g_nidx, GL_UNSIGNED_INT, (const void *)(uintptr_t)ib_at,
                                 (GLint)(vb_at / sizeof(GVert)));
    }
    s_gpu.tris_drawn += g_nidx / 3;
    s_gpu.drawn_offset = s_gpu.color_offset;
}

/* ---------------- clears, copies, read-backs ---------------- */

/* CLEAR_SURFACE. Colour with the per-channel mask inside the clip rectangle
 * (D3D draws a quad for it; a GL clear honours the scissor and colour mask
 * the same way); depth and stencil clear the whole view, as D3D's
 * ClearDepthStencilView does. */
static void gpu_clear(uint32_t param)
{
    int need_depth = (param & 3) != 0;
    if (!gpu_bind_targets(need_depth)) return;
    if ((param & 3) && g_cur_ds) {
        uint32_t zv = s_cb_scratch[0];
        float z = (((s_gpu.format >> 4) & 0xF) == 1) ? (float)(zv & 0xFFFF) / 65535.0f : (float)(zv >> 8) / 16777215.0f;
        gpu_clear_ds(gsurf_draw_fbo(g_cur_rt, g_cur_ds), (int)(param & 3), z, (int)(zv & 0xFF));
        gpu_set_fbo(gsurf_draw_fbo(g_cur_rt, g_cur_ds));
    }
    if (param & 0xF0) {
        uint32_t c = s_gpu.clear_color;
        gpu_set_fbo(gsurf_draw_fbo(g_cur_rt, g_cur_ds));
        glEnable(GL_SCISSOR_TEST);
        glScissor(g_cur_sc[0], g_cur_sc[1], g_cur_sc[2], g_cur_sc[3]);
        g_sh.sc_ok = 0;
        glColorMask((param & 0x10) != 0, (param & 0x20) != 0, (param & 0x40) != 0, (param & 0x80) != 0);
        glClearColor(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, (c >> 24) / 255.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        g_sh.bs_ok = 0;                 /* the colour mask */
    }
}

/* CopyRects between surfaces the GPU owns (the heat-haze copy of the back
 * buffer). 0 when the source is not a GPU surface: the caller copies guest
 * memory as before. */
static int gpu_blit(uint32_t sb, uint32_t db, uint32_t ix, uint32_t iy, uint32_t ox, uint32_t oy,
                    uint32_t w, uint32_t h, uint32_t dp, uint32_t bpp)
{
    GSurf *src = gsurf_find(sb, 0), *dst;
    uint32_t dw;
    if (!src || bpp != 4) return 0;
    dw = dp / bpp;
    if (dw < ox + w) dw = ox + w;
    {
        /* a frame-pitch buffer is made frame sized at once */
        uint32_t dh = oy + h;
        if (dw == src->w && dh < src->h) dh = src->h;
        dst = gsurf_get(db, dw, dh, 0);
    }
    if (!dst) return 0;
    if (ix + w > src->w) w = src->w > ix ? src->w - ix : 0;
    if (iy + h > src->h) h = src->h > iy ? src->h - iy : 0;
    if (!w || !h) return 1;
    gsurf_resolve(src); gsurf_resolve(dst);   /* (MSAA: copies are between the textures) */
    if (dst->mrb) dst->ss_new = 1;
    if (src->pw * dst->w == dst->pw * src->w && src->ph * dst->h == dst->ph * src->h) {
        uint32_t l = SX(src, ix), t = SY(src, iy), r = SX(src, ix + w), b = SY(src, iy + h);
        if (r > src->pw) r = src->pw;
        if (b > src->ph) b = src->ph;
        if (SX(dst, ox) + (r - l) > dst->pw || SY(dst, oy) + (b - t) > dst->ph) return 1;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gpu_fbo(src->tex, 0));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gpu_fbo(dst->tex, 0));
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer((GLint)l, (GLint)t, (GLint)r, (GLint)b, (GLint)SX(dst, ox), (GLint)SY(dst, oy),
                          (GLint)(SX(dst, ox) + (r - l)), (GLint)(SY(dst, oy) + (b - t)), GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gpu_sh_reset();
    } else {
        /* different scales: draw the region */
        float c[4];
        glBindFramebuffer(GL_FRAMEBUFFER, gpu_fbo(dst->tex, 0));
        glViewport((GLint)SX(dst, ox), (GLint)SY(dst, oy), (GLsizei)(SX(dst, ox + w) - SX(dst, ox)),
                   (GLsizei)(SY(dst, oy + h) - SY(dst, oy)));
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        c[0] = (float)w / (float)src->w; c[1] = (float)h / (float)src->h;
        c[2] = (float)ix / (float)src->w; c[3] = (float)iy / (float)src->h;
        gpu_ub(UB_CLC, c, 16);
        glBindVertexArray(g_vao_empty);
        glUseProgram(g_pr_blit);
        glUniform1f(g_loc_ys_blit, 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src->tex);
        glBindSampler(0, g_copy_smp);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        gpu_sh_reset();
    }
    return 1;
}

/* A GPU surface back into guest memory (frame dumps, replay, captures). */
static void gpu_readback(uint32_t base, uint32_t pitch)
{
    GSurf *s = gsurf_find(base, 0);
    uint8_t *m = (uint8_t *)xbox_GetMemoryOffset() + base;
    if (!s) return;
    gsurf_resolve(s);
    if (!pitch) pitch = s->w * 4;
    if (s->pw == s->w && s->ph == s->h) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gpu_fbo(s->tex, 0));
    } else {
        /* down to the title's size first */
        if (!gpu_one(s->w, s->h)) return;
        gpu_copy_scaled(s->tex, s->pw, s->ph, s->pw, s->ph, gpu_fbo(g_one_tex, 0), s->w, s->h);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gpu_fbo(g_one_tex, 0));
    }
    if (pitch % 4 == 0 && pitch >= s->w * 4) {
        glPixelStorei(GL_PACK_ROW_LENGTH, (GLint)(pitch / 4));
        glReadPixels(0, 0, (GLsizei)s->w, (GLsizei)s->h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, m);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    } else {
        uint32_t y, wb = s->w * 4 < pitch ? s->w * 4 : pitch;
        uint8_t *tmp = (uint8_t *)malloc((size_t)s->w * 4 * s->h);
        if (tmp) {
            glReadPixels(0, 0, (GLsizei)s->w, (GLsizei)s->h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, tmp);
            for (y = 0; y < s->h; y++) memcpy(m + (size_t)y * pitch, tmp + (size_t)y * s->w * 4, wb);
            free(tmp);
        }
    }
    s->mem_sum = gpu_mem_sum(base, (s->pitch ? s->pitch : s->w * 4) * s->h);
    gpu_sh_reset();
}

/* ---------------- graphics settings overlay ---------------- */

static GLuint g_menu_tex;
static int g_menu_w, g_menu_h;
static unsigned g_menu_ver;
static void gpu_set_alpha_blend(void)
{
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
}
/* The panel (src/video/gfx_menu.c) over the finished frame, centred, at one
 * texel per screen pixel; the window framebuffer is current. */
static void gpu_draw_menu(const uint32_t *px, int w, int h, unsigned ver)
{
    float cb[4] = { 1, 1, 0, 0 };
    int left, top;
    if (!g_menu_tex || g_menu_w != w || g_menu_h != h) {
        if (g_menu_tex) glDeleteTextures(1, &g_menu_tex);
        g_menu_tex = gpu_tex2d((uint32_t)w, (uint32_t)h, 0);
        g_menu_w = w; g_menu_h = h; g_menu_ver = ver - 1;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_menu_tex);
    if (g_menu_ver != ver) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px);
        g_menu_ver = ver;
    }
    left = (g_sc_w - w) / 2; top = (g_sc_h - h) / 2;
    glViewport(left, g_sc_h - top - h, w, h);
    gpu_ub(UB_CLC, cb, 16);
    gpu_set_alpha_blend();
    glBindVertexArray(g_vao_empty);
    glUseProgram(g_pr_copy);
    glUniform1f(g_loc_ys_copy, -1.0f);
    glBindSampler(0, g_present_smp);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    gpu_sh_reset();
}

/* A setting changed in the overlay: take the new values, between frames. */
static void gpu_apply_settings(void)
{
    int i, rs, aniso, mips;
    float asp, rsf;
    g_gfx_seen = xbox_gfx_rev;
    g_aa_on = xbox_GfxFxaa();
    g_vsync_on = xbox_GfxGet(GFX_VSYNC);
    g_stretch_on = xbox_GfxGet(GFX_STRETCH);
    g_hwvp_user = xbox_GfxGet(GFX_VP);
    g_post[0] = xbox_GfxGetF(GFX_BRIGHT); g_post[1] = xbox_GfxGetF(GFX_SHARP); g_post[2] = xbox_GfxGetF(GFX_COLOR);
    if (g_post[0] < 0.5f) g_post[0] = 1.0f;
    if (g_post[2] <= 0.0f) g_post[2] = 1.0f;
    gpu_filter_opts();
    aniso = xbox_GfxGet(GFX_ANISO);
    mips = xbox_GfxGet(GFX_MIPS);
    if (aniso != g_aniso || mips != g_mips) {
        gpu_samplers_drop();
        if (mips != g_mips)
            for (i = 0; i < GTEX_MAX; i++) {
                if (g_tex[i].tex) glDeleteTextures(1, &g_tex[i].tex);
                memset(&g_tex[i], 0, sizeof g_tex[i]);
            }
        g_aniso = aniso; g_mips = mips;
        g_present_smp = gpu_sampler(3, 3, 1);
        g_copy_smp = g_present_smp;
    }
    rs = xbox_GfxGet(GFX_SCALE);
    rsf = xbox_GfxScaleF();
    asp = xbox_GfxAspect();
    if (asp < 1.34f) asp = 4.0f / 3.0f;
    {
        int ms = g_msaa_bad ? 1 : xbox_GfxMsaa();
        if (ms != g_msaa) { g_msaa = ms; g_rsf = -1.0f; }   /* (MSAA changed: every surface made again) */
    }
    if (rsf != g_rsf || asp != g_aspect) {
        for (i = 0; i < GSURF_MAX; i++)
            if (g_surf[i].tex) gsurf_free(&g_surf[i]);
        g_cur_rt = g_cur_ds = NULL;
        g_rs = rs; g_rsf = rsf; g_aspect = asp;
        g_rsx = g_rsf * (g_aspect / (4.0f / 3.0f));
        g_rsy = g_rsf;
        rs_fit();
    }
    gpu_sh_reset();
    fprintf(stderr, "  [GL] settings: scale %g (%.2f x %.2f), picture %.3f:1, aniso x%d, mipmaps %s, %s, vsync %s, %s, vertex programs on the %s\n",
            (double)g_rsf, g_rsx, g_rsy, g_aspect, g_aniso, g_mips ? "on" : "off", g_msaa > 1 ? (g_aa_on ? "MSAA + FXAA" : "MSAA") : g_aa_on ? "FXAA" : "no AA",
            g_vsync_on ? "on" : "off", g_stretch_on ? "stretched" : "shape kept", g_hwvp_user ? "GPU" : "CPU");
    fflush(stderr);
}

/* ---------------- title overlays (HUD mods) ---------------- */

typedef struct {
    const uint32_t *px;          /* BGRA, straight alpha, w*h; NULL = slot unused */
    int w, h;
    unsigned ver;                /* change when px changes */
    float x, y, dw, dh;          /* 640x480 game coordinates */
} XboxOverlayQuad;
#define OVL_MAX 8
int (*volatile xbox_overlay_cb)(XboxOverlayQuad *q, int max);
static struct { GLuint tex; int w, h; unsigned ver; } g_ovl[OVL_MAX];

static void gpu_draw_overlays(GSurf *s, int wide)
{
    XboxOverlayQuad q[OVL_MAX];
    int n, i;
    float k = wide ? g_aspect / (4.0f / 3.0f) : 1.0f;
    if (!xbox_overlay_cb || !s || s->is_depth) return;
    memset(q, 0, sizeof q);
    n = xbox_overlay_cb(q, OVL_MAX);
    if (n <= 0) return;
    glBindFramebuffer(GL_FRAMEBUFFER, gpu_fbo(s->tex, 0));
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gpu_set_alpha_blend();
    glBindVertexArray(g_vao_empty);
    glUseProgram(g_pr_copy);
    glUniform1f(g_loc_ys_copy, 1.0f);
    glBindSampler(0, g_present_smp);
    glActiveTexture(GL_TEXTURE0);
    for (i = 0; i < n && i < OVL_MAX; i++) {
        float cb[4] = { 1, 1, 0, 0 }, x0, x1;
        if (!q[i].px || q[i].w <= 0 || q[i].h <= 0) continue;
        if (!g_ovl[i].tex || g_ovl[i].w != q[i].w || g_ovl[i].h != q[i].h) {
            if (g_ovl[i].tex) glDeleteTextures(1, &g_ovl[i].tex);
            memset(&g_ovl[i], 0, sizeof g_ovl[i]);
            g_ovl[i].tex = gpu_tex2d((uint32_t)q[i].w, (uint32_t)q[i].h, 0);
            g_ovl[i].w = q[i].w; g_ovl[i].h = q[i].h; g_ovl[i].ver = q[i].ver - 1;
        }
        glBindTexture(GL_TEXTURE_2D, g_ovl[i].tex);
        if (g_ovl[i].ver != q[i].ver) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, q[i].w, q[i].h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, q[i].px);
            g_ovl[i].ver = q[i].ver;
        }
        /* HUD art keeps its 4:3 shape in a widened frame (as the game's own) */
        x0 = hud_wide_x(q[i].x, q[i].x + q[i].dw * 0.5f, q[i].y + q[i].dh * 0.5f, k);
        x1 = hud_wide_x(q[i].x + q[i].dw, q[i].x + q[i].dw * 0.5f, q[i].y + q[i].dh * 0.5f, k);
        {
            float vx = x0 * (float)s->pw / (float)s->w, vy = q[i].y * (float)s->ph / (float)s->h;
            float vw = (x1 - x0) * (float)s->pw / (float)s->w, vh = q[i].dh * (float)s->ph / (float)s->h;
            glViewport((GLint)floorf(vx + 0.5f), (GLint)floorf(vy + 0.5f), (GLsizei)floorf(vw + 0.5f), (GLsizei)floorf(vh + 0.5f));
        }
        gpu_ub(UB_CLC, cb, 16);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);
    gpu_sh_reset();
}

/* A one-off window dump requested by the title (testing). */
static volatile char g_dump_req[260];
void xbox_PresentDumpRequest(const char *path)
{
    if (path && strlen(path) < sizeof g_dump_req - 1) { memcpy((char *)g_dump_req, path, strlen(path) + 1); }
}

static void gpu_dump_window(const char *nm)
{
    uint8_t *buf;
    FILE *o;
    uint32_t w = (uint32_t)g_sc_w, h = (uint32_t)g_sc_h;
    if (!w || !h) return;
    buf = (uint8_t *)malloc((size_t)w * h * 4);
    if (!buf) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, buf);
    if ((o = fopen(nm, "wb"))) {
        /* GL's rows run bottom-up, which is a BMP's own order */
        uint8_t hd[54] = { 'B', 'M' };
        uint32_t row = w * 4, img = row * h, tot = img + 54;
        int32_t hh = (int32_t)h;
        memcpy(hd + 2, &tot, 4); hd[10] = 54; hd[14] = 40; memcpy(hd + 18, &w, 4); memcpy(hd + 22, &hh, 4);
        hd[26] = 1; hd[28] = 32; memcpy(hd + 34, &img, 4);
        fwrite(hd, 1, 54, o);
        fwrite(buf, 1, img, o);
        fclose(o);
    }
    free(buf);
    gpu_sh_reset();
}

/* The flip: the finished surface onto the window. */
static void gpu_present(uint32_t base, uint32_t pitch)
{
    hud_frame_end();   /* this frame's HUD pieces, grouped (hud_wide_x) */
    GSurf *s = gsurf_find(base, 0);
    int wide;
    if (getenv("RECOMP_FB_DUMP")) {
        static unsigned every, k;
        if (!every) {
            const char *e = getenv("RECOMP_FB_WINDOW_DUMP_EVERY");
            every = (e && atoi(e) > 0) ? (unsigned)atoi(e) / 4 : 15;
            if (!every) every = 1;
        }
        if (k++ % every == 0) gpu_readback(base, pitch);
    }
    if (!g_win || !s) return;
    gsurf_sync_from_mem(s);
    gsurf_resolve(s);   /* (MSAA: the picture from its samples) */
    {
        extern int xbox_MoviePlaying(void);
        wide = g_aspect > 1.34f && g_frame_3d && !xbox_MoviePlaying() && g_ws_cam_tick != 0;
        gpu_draw_overlays(s, wide);
    }
    g_fhash_cur ^= 1; g_fhash_n[g_fhash_cur] = 0;
    if (SDL_GetWindowFlags(g_win) & SDL_WINDOW_MINIMIZED) goto settings;
    SDL_GL_GetDrawableSize(g_win, &g_sc_w, &g_sc_h);
    if (g_sc_w < 8 || g_sc_h < 8) goto settings;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, g_sc_w, g_sc_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    {
        /* the picture's shape: g_aspect (4:3 unless widescreen), movies 4:3,
         * black bars round it; stretch fills the window */
        float a, ww = (float)g_sc_w, wh = (float)g_sc_h, pw, ph, uvs[4];
        int vx, vy;
        float mz;
        a = wide ? g_aspect : 4.0f / 3.0f;
        { extern int xbox_MoviePlaying(void); extern volatile int xbox_frontend_hint; mz = menu_fill_zoom((!g_frame_3d || xbox_frontend_hint == 1) && !xbox_MoviePlaying()); }
        g_wide_prev = wide; g_frame_3d = 0; g_present_n++;
        if (g_stretch_on < 0) g_stretch_on = getenv("RECOMP_PRESENT_STRETCH") != NULL;
        if (g_stretch_on) { pw = ww; ph = wh; }
        else if (ww / wh > a) { ph = wh; pw = wh * a; }
        else { pw = ww; ph = ww / a; }
        pw *= mz; ph *= mz;
        { extern int xbox_MoviePlaying(void); movie_fill(xbox_MoviePlaying(), ww, wh, &pw, &ph); }
        vx = (int)((ww - pw) * 0.5f); vy = (int)((wh - ph) * 0.5f);
        glViewport(vx, g_sc_h - vy - (int)ph, (GLsizei)pw, (GLsizei)ph);
        present_map_set(vx / ww, vy / wh, pw / ww, ph / wh, wide ? g_aspect / (4.0f / 3.0f) : 1.0f);
        uvs[0] = 640.0f / (float)s->w; uvs[1] = 480.0f / (float)s->h;
        uvs[2] = 1.0f / (float)s->pw; uvs[3] = 1.0f / (float)s->ph;
        gpu_ub(UB_CLC, uvs, 16);
        gpu_ub(UB_POST, g_post, 16);
    }
    glBindVertexArray(g_vao_empty);
    if (g_aa_on < 0) g_aa_on = xbox_GfxFxaa();
    if (g_aa_on && g_pr_fxaa) { glUseProgram(g_pr_fxaa); glUniform1f(g_loc_ys_fxaa, -1.0f); }
    else { glUseProgram(g_pr_present); glUniform1f(g_loc_ys_present, -1.0f); }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glBindSampler(0, g_present_smp);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    {
        const uint32_t *mpx; int mw, mh; unsigned mver;
        xbox_GfxMenuSetAvailable(1);
        if (xbox_GfxMenuRender(g_sc_w, g_sc_h, &mpx, &mw, &mh, &mver)) gpu_draw_menu(mpx, mw, mh, mver);
    }
    {
        /* RECOMP_PRESENT_DUMP=prefix: every Nth presented frame as a BMP of
         * the window's picture (testing the final picture) */
        static const char *pd = (const char *)1; static unsigned pk, seq;
        static int pevery;
        int req = g_dump_req[0] != 0;
        if (pd == (const char *)1) pd = getenv("RECOMP_PRESENT_DUMP");
        if (!pevery) { const char *e = getenv("RECOMP_PRESENT_DUMP_EVERY"); pevery = e && atoi(e) > 0 ? atoi(e) : 300; }
        if (req || (pd && pk++ % pevery == (unsigned)pevery / 2)) {
            char nm[260];
            if (req) { snprintf(nm, sizeof nm, "%s", (const char *)g_dump_req); g_dump_req[0] = 0; }
            else snprintf(nm, sizeof nm, "%s%03u.bmp", pd, seq++);
            gpu_dump_window(nm);
        }
    }
    if (g_vsync_on < 0) { const char *v = getenv("RECOMP_VSYNC"); g_vsync_on = v && v[0] == '1'; }
    if (g_vsync_applied != g_vsync_on) {
        /* adaptive vsync where the driver has it, else plain */
        if (!g_vsync_on || SDL_GL_SetSwapInterval(-1) != 0) SDL_GL_SetSwapInterval(g_vsync_on ? 1 : 0);
        g_vsync_applied = g_vsync_on;
    }
    SDL_GL_SwapWindow(g_win);
    gpu_sh_reset();
    {
        static int said;
        GLenum err = glGetError();
        if (err != GL_NO_ERROR && said++ < 10) fprintf(stderr, "  [GL] error 0x%04X during the frame\n", err);
    }
settings:
    if (g_msaa_bad && g_msaa > 1) {
        fprintf(stderr, "  [GL] MSAA: a multisampled surface could not be made (video memory?): MSAA off\n"); fflush(stderr);
        gpu_apply_settings();
    }
    if (xbox_gfx_rev != g_gfx_seen) gpu_apply_settings();
}
