// Veyra .cube parser, shared with the portable malformed-input regression tests.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#define VEYRA_MAX_LUT_SIZE 64
struct cube_lut {
    int size;
    float domain_min[3], domain_max[3];
    uint8_t *rgba;
};
static bool cube_parse(const char *path, struct cube_lut *out)
{
    memset(out, 0, sizeof(*out));
    out->domain_max[0]=out->domain_max[1]=out->domain_max[2]=1.f;
    FILE *file=fopen(path,"rb");
    if(!file) return false;
    int entries=0; bool ok=false; char line[512];
    while(fgets(line,sizeof(line),file)) {
        if(!strchr(line,'\n') && !feof(file)) goto done;
        char *comment=strchr(line,'#'); if(comment) *comment=0;
        char *p=line; while(isspace((unsigned char)*p)) ++p;
        if(!*p) continue;
        if(!strncmp(p,"TITLE",5) && isspace((unsigned char)p[5])) continue;
        if(!strncmp(p,"LUT_3D_SIZE",11)) {
            int n=0,size=0;
            if(out->rgba || sscanf(p,"LUT_3D_SIZE %d %n",&size,&n)!=1 || !n || p[n] || size<2 || size>VEYRA_MAX_LUT_SIZE) goto done;
            out->size=size;
            out->rgba=(uint8_t*)malloc((size_t)size*size*size*4);
            if(!out->rgba) goto done;
            continue;
        }
        float values[3]; int n=0;
        const bool minimum=!strncmp(p,"DOMAIN_MIN",10);
        const bool maximum=!strncmp(p,"DOMAIN_MAX",10);
        if(minimum || maximum) {
            if(sscanf(p+10,"%f %f %f %n",values,values+1,values+2,&n)!=3 || !n || p[10+n]) goto done;
            for(int c=0;c<3;++c) {
                if(!isfinite(values[c])) goto done;
                (minimum?out->domain_min:out->domain_max)[c]=values[c];
            }
            continue;
        }
        if(!out->rgba || entries>=out->size*out->size*out->size ||
           sscanf(p,"%f %f %f %n",values,values+1,values+2,&n)!=3 || !n || p[n]) goto done;
        for(int c=0;c<3;++c) {
            if(!isfinite(values[c])) goto done;
            out->rgba[entries*4+c]=(uint8_t)(fminf(fmaxf(values[c],0.f),1.f)*255.f+.5f);
        }
        out->rgba[entries*4+3]=255; ++entries;
    }
    if(ferror(file) || !out->rgba || entries!=out->size*out->size*out->size) goto done;
    for(int c=0;c<3;++c) if(out->domain_min[c]>=out->domain_max[c]) goto done;
    ok=true;
done:
    fclose(file);
    if(!ok) { free(out->rgba); out->rgba=NULL; }
    return ok;
}
