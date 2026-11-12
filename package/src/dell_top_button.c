/* MIT. Dell PN7522W Bluetooth top button; independent of the side-key probe. */
#define _GNU_SOURCE
#include <linux/input.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

static volatile sig_atomic_t stopped;
static char module[PATH_MAX], runtime[PATH_MAX], config[PATH_MAX];
static unsigned long events;
static int meta, pressed[3], synced = 1;
static void signal_stop(int sig) { (void)sig; stopped = 1; }
static int exists(const char *base, const char *name) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", base, name) >= (int)sizeof(path)) return 1;
    return access(path, F_OK) == 0;
}
static int enabled(void) {
    FILE *f = fopen(config, "r");
    int c = f ? fgetc(f) : '0';
    if (f) fclose(f);
    return c == '1' && !exists(module,"paused") && !exists(module,"disable") && !exists(module,"remove");
}
static void status(int connected, const char *last) {
    char path[PATH_MAX], temp[PATH_MAX];
    if (snprintf(path,sizeof(path),"%s/status.json",runtime) >= (int)sizeof(path) ||
        snprintf(temp,sizeof(temp),"%s/status.tmp",runtime) >= (int)sizeof(temp)) return;
    FILE *f = fopen(temp,"w");
    if (!f) return;
    fprintf(f,"{\"running\":%s,\"enabled\":%s,\"connected\":%s,\"events\":%lu,\"last\":\"%s\"}\n",
        stopped?"false":"true", enabled()?"true":"false",connected?"true":"false",events,last);
    fclose(f); rename(temp,path);
}
static int device(void) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e; int found = -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name,"event",5)) continue;
        char p[PATH_MAX], name[128] = {0}; struct input_id id = {0};
        snprintf(p,sizeof(p),"/dev/input/%s",e->d_name);
        int fd = open(p,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if (fd < 0) continue;
        if (ioctl(fd,EVIOCGNAME(sizeof(name)),name) >= 0 && !strcmp(name,"Dell PN7522W Keyboard") &&
            ioctl(fd,EVIOCGID,&id) == 0 && id.bustype == BUS_BLUETOOTH && id.vendor == 0x413c && id.product == 0x8207) {
            int clockid = CLOCK_MONOTONIC;
            if (ioctl(fd,EVIOCSCLOCKID,&clockid) == 0) { found = fd; break; }
        }
        close(fd);
    }
    closedir(d); return found;
}
/* Return gesture only on a fresh press while Meta is down. */
static int decode(struct input_event *e) {
    if (e->type == EV_SYN && e->code == SYN_DROPPED) { synced=0; meta=0; memset(pressed,0,sizeof(pressed)); return -1; }
    if (!synced) { if (e->type==EV_SYN && e->code==SYN_REPORT) synced=1; return -1; }
    if (e->type != EV_KEY) return -1;
    if (e->code == KEY_LEFTMETA) { meta=e->value!=0; return -1; }
    int n = e->code==KEY_F20 ? 0 : e->code==KEY_F19 ? 1 : e->code==KEY_F18 ? 2 : -1;
    if (n<0) return -1;
    if (e->value==0) { pressed[n]=0; return -1; }
    if (e->value!=1 || pressed[n]) return -1;
    pressed[n]=1; return meta ? n : -1;
}
int main(int argc,char **argv) {
    if (argc!=4) { fprintf(stderr,"Usage: dell-top-button MODULE RUNTIME CONFIG\n"); return 2; }
    if (strlen(argv[1])>PATH_MAX-100 || strlen(argv[2])>PATH_MAX-100 || strlen(argv[3])>=PATH_MAX) return 2;
    strcpy(module,argv[1]); strcpy(runtime,argv[2]); strcpy(config,argv[3]);
    umask(077); mkdir(runtime,0700);
    char path[PATH_MAX], dispatcher[PATH_MAX];
    snprintf(path,sizeof(path),"%s/lock",runtime);
    int lock=open(path,O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if (lock<0 || flock(lock,LOCK_EX|LOCK_NB)) return 1;
    snprintf(path,sizeof(path),"%s/pid",runtime);
    FILE *pf=fopen(path,"w"); if (!pf) return 1;
    fprintf(pf,"%ld\n",(long)getpid()); fclose(pf);
    snprintf(dispatcher,sizeof(dispatcher),"%s/bin/top-config.sh",module);
    signal(SIGTERM,signal_stop); signal(SIGINT,signal_stop); signal(SIGHUP,signal_stop);
    int fd=-1, grabbed=0; pid_t child=0; const char *last="none";
    time_t tick=0; struct timespec child_started={0};
    while (!stopped && !exists(runtime,"stop") && !exists(module,"remove") && !exists(module,"disable")) {
        if (child) {
            struct timespec current; clock_gettime(CLOCK_MONOTONIC,&current);
            double elapsed=current.tv_sec-child_started.tv_sec+(current.tv_nsec-child_started.tv_nsec)/1e9;
            if(waitpid(child,NULL,WNOHANG)==child) { kill(-child,SIGKILL); child=0; }
            else if(elapsed>=8.0) { kill(-child,SIGKILL);kill(child,SIGKILL);waitpid(child,NULL,0);child=0; }
        }
        time_t now=time(NULL);
        if (now!=tick) {
            tick=now;
            if (fd<0) { fd=device(); meta=0; synced=1; memset(pressed,0,sizeof(pressed)); }
            int want=enabled();
            if (fd>=0 && want!=grabbed) {
                if (ioctl(fd,EVIOCGRAB,want)==0) grabbed=want;
                meta=0; memset(pressed,0,sizeof(pressed));
            }
            status(fd>=0,last);
        }
        struct pollfd pollfd={.fd=fd,.events=POLLIN};
        int r=poll(&pollfd,1,200);
        if (r<0) { if(errno==EINTR) continue; break; }
        if (r==0) continue;
        if (pollfd.revents&(POLLHUP|POLLERR|POLLNVAL)) { close(fd);fd=-1;grabbed=0;continue; }
        struct input_event buf[32]; ssize_t bytes;
        while ((bytes=read(fd,buf,sizeof(buf)))>0) {
            for (size_t i=0;i<(size_t)bytes/sizeof(buf[0]);i++) {
                struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
                double age=ts.tv_sec-buf[i].time.tv_sec+(ts.tv_nsec/1e9-buf[i].time.tv_usec/1e6);
                if(age<0 || age>0.5) { meta=0;memset(pressed,0,sizeof(pressed));continue; }
                int gesture=decode(&buf[i]);
                if(gesture<0 || !grabbed || !enabled()) continue;
                const char *names[]={"single","double","long"}; last=names[gesture]; events++;
                if (!child) {
                    clock_gettime(CLOCK_MONOTONIC,&child_started);
                    child=fork();
                    if (child==0) {
                        setsid();
                        execl("/data/adb/ksu/bin/busybox","busybox","sh",dispatcher,"run",last,(char*)NULL);
                        _exit(127);
                    }
                    if(child<0) child=0;
                }
                status(1,last);
            }
        }
        if (bytes<0 && errno!=EAGAIN && errno!=EINTR) {close(fd);fd=-1;grabbed=0;}
    }
    if(fd>=0) { if(grabbed) ioctl(fd,EVIOCGRAB,0);close(fd); }
    if(child) {kill(-child,SIGKILL);kill(child,SIGKILL);waitpid(child,NULL,0);}
    stopped=1;status(0,last); unlink(path); close(lock); return 0;
}
