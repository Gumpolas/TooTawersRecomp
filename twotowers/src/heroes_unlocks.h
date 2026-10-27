/* Desbloqueos: una sola sustitucion, sin borrar nunca el destino primero.
 * Las macros permiten probar fallos de E/S en archivos sinteticos. */
#ifndef TT_HEROES_UNLOCKS_H
#define TT_HEROES_UNLOCKS_H
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <windows.h>
#ifndef TT_UNLOCK_REPLACE
#ifndef _WIN32   /* (1.9.9) rename() replaces the target in one step on POSIX */
#define TT_UNLOCK_REPLACE(a,b) (rename((a),(b)) == 0)
#else
#define TT_UNLOCK_REPLACE(a,b) MoveFileExA((a),(b),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
#endif
#endif
#ifndef TT_UNLOCK_FLUSH
#define TT_UNLOCK_FLUSH(f) fflush(f)
#endif
#ifndef TT_UNLOCK_CLOSE
#define TT_UNLOCK_CLOSE(f) fclose(f)
#endif
#ifndef TT_UNLOCK_READERROR
#define TT_UNLOCK_READERROR(f) ferror(f)
#endif
static int tt_unlocks_store(const char *path,const char *key,int wins)
{
    char tmp[800], line[256], found[32];
    FILE *in, *out;
    int ok=1, count=0;
    if(!path || !key || !*key || strlen(key)>15 || wins<0) return 0;
    for(const unsigned char *p=(const unsigned char *)key;*p;p++)
        if(!((*p>='0'&&*p<='9')||(*p>='A'&&*p<='Z')||
             (*p>='a'&&*p<='z')||*p=='_'||*p=='-')) return 0;
    count=snprintf(tmp,sizeof tmp,"%s.new",path);
    if(count<0 || (size_t)count>=sizeof tmp) return 0;
    errno=0;in=fopen(path,"r");
    if(!in && errno!=ENOENT) return 0; /* ilegible no equivale a inexistente */
    out=fopen(tmp,"w");
    if(!out){if(in)fclose(in);return 0;}
    if(in) {
        while(fgets(line,sizeof line,in)) {
            /* Un registro demasiado largo se conserva abortando la escritura. */
            if(!strchr(line,'\n') && !feof(in)){ok=0;break;}
            if(sscanf(line,"%31s",found)==1 &&
               (!strcmp(found,key)||!strcmp(found,"@newest"))) continue;
            if(fputs(line,out)==EOF){ok=0;break;}
            if(!strchr(line,'\n') && fputc('\n',out)==EOF){ok=0;break;}
        }
        if(TT_UNLOCK_READERROR(in))ok=0;
        if(fclose(in)!=0)ok=0;
    }
    if(ok && fprintf(out,"%s %d\n@newest %s\n",key,wins,key)<0)ok=0;
    if(TT_UNLOCK_FLUSH(out)!=0 || ferror(out))ok=0;
    if(TT_UNLOCK_CLOSE(out)!=0)ok=0;
    if(ok)ok=TT_UNLOCK_REPLACE(tmp,path)!=0;
    if(!ok)remove(tmp); /* solo el temporal de esta operacion */
    return ok;
}
#endif
