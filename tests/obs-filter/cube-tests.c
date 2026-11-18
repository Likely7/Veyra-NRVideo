#include "../../linux/obs/cube-parser.h"
static int checks=0,failures=0;
static void check(const char* path,const char* data,bool expected) {
    FILE* f=fopen(path,"wb"); if(!f) exit(2);
    fputs(data,f);fclose(f);
    struct cube_lut cube;bool ok=cube_parse(path,&cube);
    ++checks;
    if(ok!=expected){fprintf(stderr,"case %d failed\n",checks);++failures;}
    if(ok){if(cube.size!=2 || cube.rgba[0]!=0 || cube.rgba[31]!=255)++failures;free(cube.rgba);}
}
int main(int argc,char**argv) {
    if(argc!=2)return 2;
    const char* rows="0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    char valid[512];snprintf(valid,sizeof(valid),"  # comment\nTITLE \"identity\"\n LUT_3D_SIZE 2\n%s",rows);
    check(argv[1],valid,true);check(argv[1],"",false);
    check(argv[1],"LUT_3D_SIZE 0\n",false);
    check(argv[1],"LUT_3D_SIZE 65\n",false);
    check(argv[1],"LUT_3D_SIZE 2\nLUT_3D_SIZE 2\n",false);
    check(argv[1],"LUT_3D_SIZE 2\n0 0 0\n",false);
    char bad[1024];
    snprintf(bad,sizeof(bad),"%s0 0 0\n",valid);check(argv[1],bad,false);
    snprintf(bad,sizeof(bad),"DOMAIN_MIN 1 0 0\n%s",valid);check(argv[1],bad,false);
    snprintf(bad,sizeof(bad),"DOMAIN_MAX nan 1 1\n%s",valid);check(argv[1],bad,false);
    snprintf(bad,sizeof(bad),"LUT_3D_SIZE 2\nnan 0 0\n%s",rows);check(argv[1],bad,false);
    snprintf(bad,sizeof(bad),"LUT_1D_SIZE 2\n%s",valid);check(argv[1],bad,false);
    snprintf(bad,sizeof(bad),"LUT_3D_SIZE 2garbage\n%s",rows);check(argv[1],bad,false);
    snprintf(bad,sizeof(bad),"DOMAIN_MIN 0 0 0 invalid\n%s",valid);check(argv[1],bad,false);
    memset(bad,'1',sizeof(bad)-1);bad[sizeof(bad)-1]=0;check(argv[1],bad,false);
    remove(argv[1]);printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
