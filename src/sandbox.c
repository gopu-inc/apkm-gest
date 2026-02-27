#include "apkm.h"
#include <seccomp.h>
#include <grp.h>
#include <cap-ng.h>

int apkm_sandbox_create(const char* path, bool network, bool mount) {
    // Création des namespaces
    int flags = CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID;
    if (!network) flags |= CLONE_NEWNET;
    
    if (unshare(flags) != 0) {
        perror("unshare");
        return -1;
    }
    
    // Mount proc
    mount("proc", "/proc", "proc", 0, NULL);
    
    // Setup seccomp
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
    
    // Autoriser seulement les syscalls nécessaires
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(open), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(close), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(munmap), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0);
    
    seccomp_load(ctx);
    
    // Drop capabilities
    capng_clear(CAPNG_SELECT_BOTH);
    capng_update(CAPNG_DROP, CAPNG_EFFECTIVE | CAPNG_PERMITTED, CAP_NET_BIND_SERVICE);
    capng_apply(CAPNG_SELECT_BOTH);
    
    // Changer l'utilisateur et le groupe
    gid_t gid = 65534; // nobody
    uid_t uid = 65534; // nobody
    
    setgroups(0, NULL);
    setgid(gid);
    setuid(uid);
    
    // Chroot si nécessaire
    if (mount) {
        chroot(path);
        chdir("/");
    }
    
    return 0;
}
