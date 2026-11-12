#define main daemon_main
#include "../src/dell_top_button.c"
#undef main
#include <assert.h>
static int send_key(int code,int value) {
    struct input_event e={.type=EV_KEY,.code=code,.value=value}; return decode(&e);
}
int main(void) {
    assert(send_key(KEY_F20,1)==-1); /* no Meta */
    send_key(KEY_F20,0);
    const int keys[]={KEY_F20,KEY_F19,KEY_F18};
    for(int i=0;i<3;i++) {
        assert(send_key(KEY_LEFTMETA,1)==-1);
        assert(send_key(keys[i],1)==i);
        assert(send_key(keys[i],1)==-1); /* duplicate */
        assert(send_key(keys[i],2)==-1); /* autorepeat */
        send_key(keys[i],0); send_key(KEY_LEFTMETA,0);
    }
    send_key(KEY_LEFTMETA,1);
    struct input_event dropped={.type=EV_SYN,.code=SYN_DROPPED};decode(&dropped);
    assert(send_key(KEY_F20,1)==-1);
    dropped.code=SYN_REPORT;decode(&dropped);
    assert(send_key(KEY_F19,1)==-1); /* Meta must be fresh after overflow */
    puts("top decode: all gestures, modifier, repeat and overflow tests passed"); return 0;
}
