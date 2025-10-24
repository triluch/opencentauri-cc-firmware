#include <iostream>
#include <fstream>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <semaphore.h>
#include <sys/utsname.h>
#include "configfile.h"
#include "beep.h"
#include <random>
#include "klippy.h"
#include "hl_tpool.h"
#include "hl_wlan.h"
#include "hl_netlink_uevent.h"
#include "hl_usb_device.h"
#include "hl_disk.h"
#include "system.h"
#include "hl_camera.h"
#include "config.h"
#include "hl_common.h"
#include "gui.h"
#include "ui.h"
#include "app.h"
#include "params.h"
#include "aw_dsp.h"
#include "simplebus.h"
#include "service.h"
#include "break_save.h"
#include "jenkins.h"
#include "hl_boot.h"
#if CONFIG_SUPPORT_AIC
#include "ai_camera.h"
#endif
#if CONFIG_SUPPORT_TLP
#include "aic_tlp.h"
#endif
#include "file_manager.h"
#include "web.h"

#define LOG_TAG "main"
#undef LOG_LEVEL
#define LOG_LEVEL LOG_INFO
#include "log.h"


sem_t sem;

const char *wan_detection_url_table[] = {
    "http://connect.rom.miui.com/generate_204",                   // MI
    "http://connectivitycheck.platform.hicloud.com/generate_204", // HUAWEI
    "http://wifi.vivo.com.cn/generate_204",                       // VIVO
    "http://www.google-analytics.com/generate_204",               // GOOGLE
    "http://www.google.com/generate_204",                         // GOOGLE
    "http://connectivitycheck.gstatic.com/generate_204",          // GOOGLE
    "http://captive.apple.com",                                   // APPLE
    "http://www.apple.com/library/test/success.html",             // APPLE
    "http://www.msftconnecttest.com/connecttest.txt",             // MICROSOFT
    "http://detectportal.firefox.com/success.txt",                // FIREFOX
};
extern void sysconf_init();
extern ConfigParser *get_sysconf();
static int sigpipe = 0;
void handle_sigpipe(int sig);
void report_sysinfo(void);

int main(int argc, char *argv[])
{
    signal(SIGPIPE, handle_sigpipe);

    hl_tpool_init(24);
    log_init();

    char b_id[64] = {0};
    hl_get_chipid(b_id, sizeof(b_id));
    LOG_I("CHIPID:%s\n", b_id);
    struct tm tm;
    tm = hl_get_localtime_time_from_utc_second(hl_get_utc_second());
    LOG_I("%d-%d %d:%d:%d \n", tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    simple_bus_init();

    // 重启DSP
    // printf("dsp_restart\n");
    // dsp_restart();

    sem_init(&sem, 0, 0);
#define SET_PRINT_THREAD_PRIORITY
#ifdef SET_PRINT_THREAD_PRIORITY
    extern void *process_task_printer(void *arg);
    pthread_t main_process_tid;
    pthread_attr_t attr;
    struct sched_param param;

    // Initialize thread attribute
    pthread_attr_init(&attr);

    // Set scheduling policy
    pthread_attr_setschedpolicy(&attr, SCHED_RR);

    // Set thread priority
    param.sched_priority = sched_get_priority_max(SCHED_RR);
    pthread_attr_setschedparam(&attr, &param);

    // Create the thread
    int result = pthread_create(&main_process_tid, &attr, process_task_printer, NULL);
    if (result != 0) {
        fprintf(stderr, "Failed to create thread\n");
        return EXIT_FAILURE;
    }
#else
    pthread_t main_process_tid;
    extern void *process_task_printer(void *arg);
    pthread_create(&main_process_tid, NULL, process_task_printer, NULL);
#endif

    gui_init();
    hl_netlink_uevent_init();
    hl_usb_device_init("/lib/modules/5.4.61");
    hl_disk_init();
    param_init();
    sysconf_init();
    
    /* file_manager init */
    FileManager::GetInstance();

#if CONFIG_SUPPORT_CAMERA
    hl_camera_init();
#endif
#if CONFIG_SUPPORT_AIC
    ai_camera_init();
#endif
#if CONFIG_SUPPORT_TLP
    aic_tlp_early_init();
#endif
#if CONFIG_SUPPORT_NETWORK
    hl_net_init(
        get_sysconf()->GetInt("system", "wifi", 0), HL_NET_MODE_DHCP,
        0, HL_NET_MODE_STATIC,
        "/usr/share/udhcpc/default.script", "/tmp/udhcpc.status",
        "/tmp/resolv.conf",
        2, 5000,
        2, 5000,
        wan_detection_url_table, sizeof(wan_detection_url_table) / sizeof(wan_detection_url_table[0]),
        "/board-resource/wlan_entery");
#endif
    report_sysinfo();
    sem_wait(&sem);
    LOG_I("sem_wait finish !!!\n");
    // 屏幕亮度 75%
    system("echo setbl > /sys/kernel/debug/dispdbg/command");
    system("echo lcd0 > /sys/kernel/debug/dispdbg/name");
    system("echo 191 > /sys/kernel/debug/dispdbg/param");
    system("echo 1 > /sys/kernel/debug/dispdbg/start");

    service_init();
    register_api();


    ui_init();
    app_init();
    webserver_start();

    //上电时删除本地与U盘.tmp后缀文件（复制文件过程中断电时，删除复制的文件）
    char disk_path[1024];
    if (hl_disk_get_default_mountpoint(HL_DISK_TYPE_USB, NULL, disk_path, sizeof(disk_path)) == 0)
        utils_vfork_system("rm %s/*.tmp", disk_path);
    if (hl_disk_get_default_mountpoint(HL_DISK_TYPE_EMMC, NULL, disk_path, sizeof(disk_path)) == 0)
        utils_vfork_system("rm %s/*.tmp", disk_path);

    // 断电续打
    pthread_t break_save_tid;
    pthread_create(&break_save_tid, NULL, break_save_task, NULL);

	pthread_t webserver_tid;
	pthread_create(&webserver_tid, NULL, webserver_task, NULL);

    while (1)
    {
        ui_loop();
        lv_task_handler();
        usleep(20000);
        if (sigpipe == 1)
        {
            sigpipe = 0;
            LOG_E("Caught SIGPIPE\n");
        }
    }
    return 0;
}


void handle_sigpipe(int sig) 
{
    sigpipe = 1;
}

void report_sysinfo(void)
{
    /* report pid */
    pid_t pid = getpid();
    LOG_I("===============system info=============\n");
    LOG_I("Current pid: %d\n", pid);

    /* report date and time */
    time_t current_time;
    time(&current_time);
    struct tm *local_time = localtime(&current_time);
    char time_str[100];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local_time);
    LOG_I("Current Date and Time: %s\n", time_str);

    /* report compiling info */
    LOG_I("Compiled on: %s at %s\n", __DATE__, __TIME__);

    /* report host machine info */
    struct utsname buffer;
    if (uname(&buffer) == 0) {
        LOG_I("System Name: %s\n", buffer.sysname);
        LOG_I("Node Name: %s\n", buffer.nodename);
        LOG_I("Release: %s\n", buffer.release);
        LOG_I("Version: %s\n", buffer.version);
        LOG_I("Machine: %s\n", buffer.machine);
    }

    LOG_I("FW Version: %s\n", JENKINS_VERSION);

}

