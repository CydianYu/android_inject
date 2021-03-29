#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <asm/ptrace.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <elf.h>
#include <android/log.h>


#define ENABLE_DEBUG 1

#if ENABLE_DEBUG
#define  LOG_TAG "SharkChilli"
#define  LOGD(fmt, args...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG, fmt, ##args)
#define DEBUG_PRINT(format, args...) \
    LOGD(format, ##args)
#else
#define DEBUG_PRINT(format,args...)
#endif

#define CPSR_T_MASK     ( 1u << 5 )

const char *libc_path = "/system/lib/libc.so";
const char *linker_path = "/system/bin/linker";

//��ȡ��������
int ptrace_readdata(pid_t pid, uint8_t *src, uint8_t *buf, size_t size) {
    uint32_t i, j, remain;
    uint8_t *laddr;

    union u {
        long val;
        char chars[sizeof(long)];
    } d;

    j = size / 4;
    remain = size % 4;

    laddr = buf;

    for (i = 0; i < j; i++) {
        //����srcָ�������
        d.val = ptrace(PTRACE_PEEKTEXT, pid, src, 0);
        memcpy(laddr, d.chars, 4);
        src += 4;
        laddr += 4;
    }

    if (remain > 0) {
        d.val = ptrace(PTRACE_PEEKTEXT, pid, src, 0);
        memcpy(laddr, d.chars, remain);
    }

    return 0;
}

//������д������
int ptrace_writedata(pid_t pid, uint8_t *dest, uint8_t *data, size_t size) {
    uint32_t i, j, remain;
    uint8_t *laddr;

    union u {
        long val;
        char chars[sizeof(long)];
    } d;

    j = size / 4;
    remain = size % 4;

    laddr = data;

    for (i = 0; i < j; i++) {
        memcpy(d.chars, laddr, 4);
        ptrace(PTRACE_POKETEXT, pid, dest, d.val);

        dest += 4;
        laddr += 4;
    }

    if (remain > 0) {
        for (i = 0; i < remain; i++) {
            d.chars[i] = *laddr++;
        }

        ptrace(PTRACE_POKETEXT, pid, dest, d.val);
    }

    return 0;
}


int ptrace_call(pid_t pid, uint32_t addr, long *params, uint32_t num_params, struct pt_regs* regs)
{
    uint32_t i;
    //ǰ4����������Ĵ���
    for (i = 0; i < num_params && i < 4; i ++) {
        regs->uregs[i] = params[i];
    }

		//����Ĳ�����������������ջ
    if (i < num_params) {
        //ջ�ռ��С
        regs->ARM_sp -= (num_params - i) * sizeof(long) ;
        //д��ջ��
        ptrace_writedata(pid, (void *)regs->ARM_sp, (uint8_t *)&params[i], (num_params - i) * sizeof(long));
    }

    regs->ARM_pc = addr;
    if (regs->ARM_pc & 1) {
        /* thumb */
        regs->ARM_pc &= (~1u);
        regs->ARM_cpsr |= CPSR_T_MASK;
    } else {
        /* arm */
        regs->ARM_cpsr &= ~CPSR_T_MASK;
    }
		
		//��ô���notify��������mmpִ�����ˡ�����ͨ��������仰��
		//ԭ���ǵ���������ʱ�򣬵�����ʹ��bl����bx�����ӼĴ���ָ�������һ�����ص�ַ��
		//������������ص�ַ��ֵ��0������ʱ��pc=0���ͻ�����쳣���൱��һ��notify��
		//Ȼ���������Ǹ�waitpid�õ��쳣ģʽ��ȷ��mmpִ���ꡣ������ʵ���治һ����0��ֻҪ����Ч���ɡ�
    regs->ARM_lr = 0;

    if (ptrace_setregs(pid, regs) == -1
            || ptrace_continue(pid) == -1) {
        printf("error\n");
        return -1;
    }

    int stat = 0;
    waitpid(pid, &stat, WUNTRACED);
    while (stat != 0xb7f) {
        if (ptrace_continue(pid) == -1) {
            printf("error\n");
            return -1;
        }
        waitpid(pid, &stat, WUNTRACED);
    }

    return 0;
}


//��ȡ���̼Ĵ�������
int ptrace_getregs(pid_t pid, struct pt_regs *regs) {
    if (ptrace(PTRACE_GETREGS, pid, NULL, regs) < 0) {
        perror("ptrace_getregs: Can not get register values");
        return -1;
    }

    return 0;
}

//���ý��̼Ĵ���
int ptrace_setregs(pid_t pid, struct pt_regs *regs) {
    if (ptrace(PTRACE_SETREGS, pid, NULL, regs) < 0) {
        perror("ptrace_setregs: Can not set register values");
        return -1;
    }

    return 0;
}

//���̼���ָ��
int ptrace_continue(pid_t pid) {
    if (ptrace(PTRACE_CONT, pid, NULL, 0) < 0) {
        perror("ptrace_cont");
        return -1;
    }

    return 0;
}

//���ص�Ŀ�����
int ptrace_attach(pid_t pid) {
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) {
        perror("ptrace_attach");
        return -1;
    }

    int status = 0;
    waitpid(pid, &status, WUNTRACED);

    return 0;
}

//��Ŀ�������ж��
int ptrace_detach(pid_t pid) {
    if (ptrace(PTRACE_DETACH, pid, NULL, 0) < 0) {
        perror("ptrace_detach");
        return -1;
    }

    return 0;
}

//��ȡĿ�����ģ���ַ
void *get_module_base(pid_t pid, const char *module_name) {
    FILE *fp;
    long addr = 0;
    char *pch;
    char filename[32];
    char line[1024];

    if (pid < 0) {
        /* self process */
        snprintf(filename, sizeof(filename), "/proc/self/maps", pid);
    } else {
        snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    }

    fp = fopen(filename, "r");

    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, module_name)) {
                pch = strtok(line, "-");
                
                addr = strtoul(pch, NULL, 16);

                if (addr == 0x8000)
                    addr = 0;

                break;
            }
        }

        fclose(fp);
    }

    return (void *) addr;
}

//��ȡԶ��ģ���ַ
void *get_remote_addr(pid_t target_pid, const char *module_name, void *local_addr) {
    void *local_handle, *remote_handle;

    local_handle = get_module_base(-1, module_name);
    remote_handle = get_module_base(target_pid, module_name);

    DEBUG_PRINT("[+] get_remote_addr: local[%x], remote[%x]\n", local_handle, remote_handle);

    void *ret_addr = (void *) ((uint32_t) local_addr + (uint32_t) remote_handle -
                               (uint32_t) local_handle);

    return ret_addr;
}

//���ݽ��������ҽ���id  ��ȡ/proc/%d/cmdline��ȡ������
int find_pid_of(const char *process_name) {
    int id;
    pid_t pid = -1;
    DIR *dir;
    FILE *fp;
    char filename[32];
    char cmdline[256];

    struct dirent *entry;

    if (process_name == NULL)
        return -1;

    dir = opendir("/proc");
    if (dir == NULL)
        return -1;

    while ((entry = readdir(dir)) != NULL) {
        id = atoi(entry->d_name);
        if (id != 0) {
            sprintf(filename, "/proc/%d/cmdline", id);
            fp = fopen(filename, "r");
            if (fp) {
                fgets(cmdline, sizeof(cmdline), fp);
                fclose(fp);

                if (strcmp(process_name, cmdline) == 0) {
                    /* process found */
                    pid = id;
                    break;
                }
            }
        }
    }

    closedir(dir);
    return pid;
}

long ptrace_retval(struct pt_regs *regs) {
    return regs->ARM_r0;
}

long ptrace_ip(struct pt_regs *regs) {
    return regs->ARM_pc;
}

int ptrace_call_wrapper(pid_t target_pid, const char *func_name, void *func_addr, long *parameters,
                        int param_num, struct pt_regs *regs) {
    DEBUG_PRINT("[+] Calling %s in target process.\n", func_name);
    if (ptrace_call(target_pid, (uint32_t) func_addr, parameters, param_num, regs) == -1)
        return -1;

    if (ptrace_getregs(target_pid, regs) == -1)
        return -1;
    DEBUG_PRINT("[+] Target process returned from %s, return value=%x, pc=%x \n",
                func_name, ptrace_retval(regs), ptrace_ip(regs));
    return 0;
}

//ע��Զ�̽�������
int inject_remote_process(pid_t target_pid, const char *library_path, const char *function_name,
                          const char *param, size_t param_size) {
    int ret = -1;
    void *mmap_addr, *dlopen_addr, *dlsym_addr, *dlclose_addr, *dlerror_addr;
    void *local_handle, *remote_handle, *dlhandle;
    uint8_t *map_base = 0;
    uint8_t *dlopen_param1_ptr, *dlsym_param2_ptr, *saved_r0_pc_ptr, *inject_param_ptr, *remote_code_ptr, *local_code_ptr;

    struct pt_regs regs, original_regs;
    extern uint32_t _dlopen_addr_s, _dlopen_param1_s, _dlopen_param2_s, _dlsym_addr_s, \
 _dlsym_param2_s, _dlclose_addr_s, _inject_start_s, _inject_end_s, _inject_function_param_s, \
 _saved_cpsr_s, _saved_r0_pc_s;

    uint32_t code_length;
    long parameters[10];

    DEBUG_PRINT("[+] Injecting process: %d\n", target_pid);
    //1.���ȹ��ص�Ŀ�����
    if (ptrace_attach(target_pid) == -1)
        goto exit;
    //2.��ȡĿ����̼Ĵ�������
    if (ptrace_getregs(target_pid, &regs) == -1)
        goto exit;

    /*3.����ԭ���ļĴ�������*/
    memcpy(&original_regs, &regs, sizeof(regs));
    //��ȡĿ�����mmap�����ĵ�ַ
    mmap_addr = get_remote_addr(target_pid, libc_path, (void *) mmap);
     //��ȡĿ����� dlopen �����ĵ�ַ
    dlopen_addr = get_remote_addr(target_pid, linker_path, (void *) dlopen);
    //��ȡĿ����� dlsym �����ĵ�ַ
    dlsym_addr = get_remote_addr(target_pid, linker_path, (void *) dlsym);
    //��ȡĿ����� dlclose �����ĵ�ַ
    dlclose_addr = get_remote_addr(target_pid, linker_path, (void *) dlclose);
    //��ȡĿ����� dlerror �����ĵ�ַ
    dlerror_addr = get_remote_addr(target_pid, linker_path, (void *) dlerror);

    DEBUG_PRINT("[+] Get imports: dlopen: %x, dlsym: %x, dlclose: %x, dlerror: %x\n",
                dlopen_addr, dlsym_addr, dlclose_addr, dlerror_addr);
    DEBUG_PRINT("[+] Remote mmap address: %x\n", mmap_addr);

		//4.ʹ��mmap���������ַ����ڴ�
    /* call mmap ׼��mmap���� �������0x4000��С���ڴ�*/
    parameters[0] = 0;  // addr
    parameters[1] = 0x4000; // size
    parameters[2] = PROT_READ | PROT_WRITE | PROT_EXEC;  // prot
    parameters[3] = MAP_ANONYMOUS | MAP_PRIVATE; // flags
    parameters[4] = 0; //fd
    parameters[5] = 0; //offset

    //��Ŀ������е���mmap����
    if (ptrace_call_wrapper(target_pid, "mmap", mmap_addr, parameters, 6, &regs) == -1)
        goto exit;

    map_base = ptrace_retval(&regs);
   

    printf("library path = %s\n", library_path);
    //5.��Ŀ�����д��library_path�е��ַ���
    ptrace_writedata(target_pid, map_base, library_path, strlen(library_path) + 1);

    parameters[0] = map_base;
    parameters[1] = RTLD_NOW | RTLD_GLOBAL;
    //6.��Ŀ����̵��� dlopen ����
    if (ptrace_call_wrapper(target_pid, "dlopen", dlopen_addr, parameters, 2, &regs) == -1)
        goto exit;

    void *sohandle = ptrace_retval(&regs);
		//7.��Ŀ�����д��function_name�е��ַ��� (������䵽0x100���� һ��������ַ������ᳬ��)
#define FUNCTION_NAME_ADDR_OFFSET       0x100
    ptrace_writedata(target_pid, map_base + FUNCTION_NAME_ADDR_OFFSET, function_name,
                     strlen(function_name) + 1);
    parameters[0] = sohandle;
    parameters[1] = map_base + FUNCTION_NAME_ADDR_OFFSET;
    //8.��Ŀ����̵��� dlsym ����(�õ�������Ҫ���õĺ�����ַ)
    if (ptrace_call_wrapper(target_pid, "dlsym", dlsym_addr, parameters, 2, &regs) == -1)
        goto exit;

    void *hook_entry_addr = ptrace_retval(&regs);
    DEBUG_PRINT("hook_entry_addr = %p\n", hook_entry_addr);
		//9.Ϊ���õĺ��������������ַ��� (������䵽0x200���� һ��������ַ������ᳬ��)
#define FUNCTION_PARAM_ADDR_OFFSET      0x200
    ptrace_writedata(target_pid, map_base + FUNCTION_PARAM_ADDR_OFFSET, param, strlen(param) + 1);
    parameters[0] = map_base + FUNCTION_PARAM_ADDR_OFFSET;

    //10.��Ŀ����̵���hook_entry ����
    if (ptrace_call_wrapper(target_pid, function_name, hook_entry_addr, parameters, 1, &regs) == -1)
        goto exit;

    printf("Press enter to dlclose and detach\n");


    /* 11.�ָ��Ĵ���*/
    ptrace_setregs(target_pid, &original_regs);
    //12.ж��Ŀ�����
    ptrace_detach(target_pid);
    ret = 0;

    exit:
    return ret;
}

int main(int argc, char **argv) {
    pid_t target_pid;
    target_pid = find_pid_of(
            "com.shark.initapp");
    if (-1 == target_pid) {
        printf("Can't find the process\n");
        return -1;
    }
    printf("target_pid=%d argc=%d\n ", target_pid, argc);
    char *sopath = "/data/local/tmp/inject_shark.so";
    if (argc > 1) {
        sopath = argv[1];
    }
    char *main_entry = "main_entry";
    if (argc > 2) {
        main_entry = argv[2];
    }
    char *parameter = "parameter";
    if (argc > 3) {
        parameter = argv[3];
    }

    printf("inject_remote_process start\n");
    inject_remote_process(target_pid, sopath, main_entry, parameter, strlen(parameter));
    return 0;
}
