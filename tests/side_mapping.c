#define SIDE_CONFIG_PATH "/data/local/tmp/dell-side-mapping-test.conf"
#define main bridge_program_main
#include "../src/dell_pen_bridge.c"
#undef main
#include <assert.h>
int main(void) {
 unlink(SIDE_CONFIG_PATH);
 int fds[2]; assert(pipe(fds)==0);
 struct bridge b={0}; b.uinput_fd=fds[1];
 unsigned states[]={2,0,1,0,3,0};
 unsigned codes[][2]={{104,0},{104,0},{109,0},{109,0},{104,109},{104,109}};
 for(unsigned i=0;i<6;i++) {
  assert(set_mask(&b,states[i],"test")==0);
  unsigned n=i<4?2:3; struct input_event e[3];
  assert(read(fds[0],e,n*sizeof(*e))==(ssize_t)(n*sizeof(*e)));
  for(unsigned k=0;k<n-1;k++) {assert(e[k].type==EV_KEY);assert(e[k].code==codes[i][k]);assert(e[k].value==(i%2==0));}
  assert(e[n-1].type==EV_SYN);
 }
 FILE *cfg=fopen(SIDE_CONFIG_PATH,"w");assert(cfg);fputs("109 104\n",cfg);fclose(cfg);
 assert(set_mask(&b,2,"swapped")==0);struct input_event e[2];assert(read(fds[0],e,sizeof(e))==sizeof(e));assert(e[0].code==109 && e[0].value==1);
 cfg=fopen(SIDE_CONFIG_PATH,"w");assert(cfg);fputs("104 109\n",cfg);fclose(cfg);
 assert(set_mask(&b,0,"release")==0);assert(read(fds[0],e,sizeof(e))==sizeof(e));assert(e[0].code==109 && e[0].value==0);
 assert(set_mask(&b,2,"new press")==0);assert(read(fds[0],e,sizeof(e))==sizeof(e));assert(e[0].code==104 && e[0].value==1);
 unlink(SIDE_CONFIG_PATH);
 close(fds[0]);close(fds[1]);puts("Side mapping: upper PAGEUP, lower PAGEDOWN, press/release and combined release passed");return 0;
}
