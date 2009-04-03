#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <pthread.h>
#include <libsmbclient.h>
#include <pwd.h>

#include "common.h"
#include "smbitem.h"
#include "auth.h"
#include "process.h"
#include "samba.h"
#include "function.h"
#include "event.h"
#include "reconfigure.h"

void check_samba_version(void){
    const char	*samba_version;

    samba_version = smbc_version();
    if (strncmp(samba_version, "3.", 2) != 0){
	fprintf(stderr, "Unknown libsmbclient version: %s\n", samba_version);
	exit(EXIT_FAILURE);
    }
}

inline size_t get_default_rw_block_size(void){
    return (strncmp(smbc_version(), "3.0.", 4) == 0) ? 48 : 128;
}

void sig_handler(int signum){
    fprintf(stderr, "%d->%s: signal %d received\n",
	getpid(), __FUNCTION__, signum);
    common_print_backtrace();
    exit(signum);
}

void set_signal_reactions(void){
    struct{
	int	signum;
	char	*name;
    }			sig[]	= { {SIGILL,  "SIGILL" },
				    {SIGSEGV, "SIGSEGV"},
				    {SIGABRT, "SIGABRT"} };
    int			i;
    struct sigaction	action;

    sigemptyset(&action.sa_mask);
    action.sa_handler = sig_handler;
    action.sa_flags = SA_RESTART;

    for(i = 0; i < 3; i++){
	if (sigaction(sig[i].signum, &action, NULL) < 0){
	    fprintf(stderr, "Can't set %s handler\n", sig[i].name);
	    exit(EXIT_FAILURE);
	}
    }

    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGHUP);
    sigaddset(&action.sa_mask, SIGCHLD);
    if (pthread_sigmask(SIG_BLOCK, &action.sa_mask, NULL) != 0){
	fprintf(stderr, "Can't block SIGHUP and SIGCHLD signals.\n");
	exit(EXIT_FAILURE);
    }
}

void set_default_login_and_configdir(void){
    char			buf[1024];
    register struct passwd	*pwd;
    const char			*home, *user, *dir;

    pwd = getpwuid(getuid());

    user = getenv("USER");
    if ((user == NULL) || (*user == '\0')) user = getenv("LOGNAME");
    if ((user == NULL) || (*user == '\0')){
	user = ((pwd != NULL) && 
	        (pwd->pw_name != NULL) &&
	        (pwd->pw_name != '\0')) ? pwd->pw_name : "anonymous";
	setenv("USER", user, 1);
	setenv("LOGNAME", user, 1);
    }
    auth_set_default_login_name(user);

    home = getenv("HOME");
    if ((home == NULL) || (*home != '/')){
	home = ((pwd != NULL) && 
	        (pwd->pw_dir  != NULL) &&
	        (*pwd->pw_dir == '/')) ? pwd->pw_dir : "/";
	setenv("HOME", home, 1);
    }

    dir = config_dir_posfix;
    if (strlen(home) + strlen(dir) + 1 > sizeof(buf)) home = "/";
    strcpy(buf, home);
    strcat(buf, (home[strlen(home) - 1] == '/') ? dir + 1 : dir);
    reconfigure_set_config_dir(buf);
}

int main(int argc, char *argv[]){
    setlocale(LC_ALL, "");
    check_samba_version();
    set_default_login_and_configdir();
    set_signal_reactions();

    /* init all subsystems with their default values */
    smbitem_init();
    process_init();
    samba_init(1024 * get_default_rw_block_size());
    event_set_event_handler(&smb_oper);

//    reconfigure_set_config_dir("/home/kl/smbnetfs/conf/");

    /* read configuration */
    reconfigure_read_config_file(config_file, 1);
    samba_allocate_ctxs();

    fuse_main(argc, argv, &smb_oper, NULL);
    samba_destroy_unused_ctxs();
    smbitem_delete_obsolete(time(NULL) + 10, SMBITEM_SAMBA_TREE);
    smbitem_delete_obsolete(time(NULL) + 10, SMBITEM_USER_TREE);
    auth_delete_obsolete(time(NULL) + 10);
    smbitem_done();
    process_cleanup_from_zombies();
    return 0;
}
