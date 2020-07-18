/*
 *
 * (C) 2018-19 - ntop.org
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include "ebpf_flow.h"
#include "container_info.h"

#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <climits>
#include <sys/time.h>
#include <bcc/BPF.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/version.h>

#include "ebpflow.ebpf.enc"

static ContainerInfo cinfo;

/* ******************************************* */

std::string b64decode(const void* data, const size_t len) {
  unsigned char* p = (unsigned char*)data;
  int pad = len > 0 && (len % 4 || p[len - 1] == '=');
  const size_t L = ((len + 3) / 4 - pad) * 4;
  std::string str(L / 4 * 3 + pad, '\0');
  const int B64index[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62, 63, 62, 62, 63, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,
    7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,
    0,  0,  0, 63,  0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51 };

  for(size_t i = 0, j = 0; i < L; i += 4) {
    int n = B64index[p[i]] << 18 |
      B64index[p[i + 1]] << 12 |
      B64index[p[i + 2]] << 6 |
      B64index[p[i + 3]];
    str[j++] = n >> 16;
    str[j++] = n >> 8 & 0xFF;
    str[j++] = n & 0xFF;
  }

  if(pad) {
    int n = B64index[p[L]] << 18 | B64index[p[L + 1]] << 12;
    str[str.size() - 1] = n >> 16;

    if(len > L + 2 && p[L + 2] != '=') {
      n |= B64index[p[L + 2]] << 6;
      str.push_back(n >> 8 & 0xFF);
    }
  }

  return str;
}

/* ******************************************* */
/* ******************************************* */

static int attachEBPFTracepoint(ebpf::BPF *bpf,
				const char *tracepoint, const char *probe_func) {
  ebpf::StatusTuple rc = bpf->attach_tracepoint(tracepoint, probe_func);

#ifdef DEBUG
  if(rc.code() != 0)
    printf("ERROR: %s/%s: %d/%s\n", tracepoint, probe_func, rc.code(), rc.msg().c_str());
#endif

  return(rc.code());
}

/* ******************************************* */

static int attachEBPFKernelProbe(ebpf::BPF *bpf, const char *queue_name,
				 const char *entry_point,
				 bpf_probe_attach_type attach_type) {
  int rc = bpf->attach_kprobe(queue_name, entry_point,
#if defined HAVE_NEW_EBPF || LINUX_VERSION_CODE <= KERNEL_VERSION(3,15,0) 
			      0,
#endif
			      attach_type).code();

#ifdef DEBUG
  if(rc != 0)
    printf("ERROR: %s/%s: %d\n", queue_name, entry_point, rc);
#endif

  return(rc);
}

/* ******************************************* */

extern "C" {
  void* init_ebpf_flow(void *priv_ptr, eBPFHandler ebpfHandler,
		       ebpfRetCode *rc, u_int16_t flags) {
    ebpf::BPF *bpf = NULL;
    std::string code = b64decode(ebpf_code, strlen(ebpf_code));
    ebpf::StatusTuple open_res(0);

    // Default value is 0
    flags = (flags == 0) ? 0xFFFF : flags;

    if(code == "") {
      *rc = ebpf_unable_to_load_kernel_probe;
      goto init_failed;
    }

    try {
      bpf = new ebpf::BPF;
    } catch(std::bad_alloc& ba) {
      *rc = ebpf_out_of_memory;
      goto init_failed;
    }

    if(bpf->init(code).code() != 0) {
      *rc = ebpf_initialization_failed;
      goto init_failed;
    }

    // attaching probes ----- //
    if((flags & LIBEBPF_TCP) && (flags & LIBEBPF_OUTCOMING)) {
      if(attachEBPFKernelProbe(bpf,"tcp_v4_connect",
			       "trace_connect_entry", BPF_PROBE_ENTRY)
	 || attachEBPFKernelProbe(bpf, "tcp_v4_connect",
				  "trace_connect_v4_return", BPF_PROBE_RETURN)
	 || attachEBPFKernelProbe(bpf, "tcp_v6_connect",
				  "trace_connect_entry", BPF_PROBE_ENTRY)
	 || attachEBPFKernelProbe(bpf, "tcp_v6_connect",
				  "trace_connect_v6_return", BPF_PROBE_RETURN)
	 ) {
	*rc = ebpf_kprobe_attach_error;
	goto init_failed;
      }
    }

    if((flags & LIBEBPF_TCP) && (flags & LIBEBPF_INCOMING)) {
      if(attachEBPFKernelProbe(bpf, "inet_csk_accept",
			       "trace_tcp_accept", BPF_PROBE_RETURN)) {
	*rc = ebpf_kprobe_attach_error;
	goto init_failed;
      }
    }

    if((flags & LIBEBPF_UDP) && (flags & LIBEBPF_OUTCOMING)) {
      if(attachEBPFTracepoint(bpf, "net:net_dev_queue",
			      "trace_netif_tx_entry")) {
	*rc = ebpf_kprobe_attach_error;
	goto init_failed;
      }
    }

    if((flags & LIBEBPF_UDP) && (flags & LIBEBPF_INCOMING)) {
      if(attachEBPFTracepoint(bpf, "net:netif_receive_skb",
			      "trace_netif_rx_entry")) {
	*rc = ebpf_kprobe_attach_error;
	goto init_failed;
      }
    }

    if(flags & LIBEBPF_TCP_CLOSE) {
      if(attachEBPFKernelProbe(bpf, "tcp_set_state",
			       "trace_tcp_set_state", BPF_PROBE_ENTRY)) {
	*rc = ebpf_kprobe_attach_error;
	goto init_failed;
      }
    }

    if(flags & LIBEBPF_TCP_RETR) {
      if(attachEBPFKernelProbe(bpf, "tcp_retransmit_skb",
			       "trace_tcp_retransmit_skb", BPF_PROBE_ENTRY)) {
	*rc = ebpf_kprobe_attach_error;
	goto init_failed;
      }
    }

    // opening output buffer ----- //
    open_res = bpf->open_perf_buffer("ebpf_events", ebpfHandler, NULL, (void*)priv_ptr);
    if(open_res.code() != 0) { *rc = ebpf_events_open_error; goto init_failed; }

    *rc = ebpf_no_error;
    return((void*)bpf);

  init_failed:
    if(bpf) delete bpf;
    return(NULL);
  };

  /* ******************************************* */

  static int is_kernel_thread(__u32 pid) {
    char pathname[64];
    struct stat statbuf;

    snprintf(pathname, sizeof(pathname), "/proc/%u", pid);

    /* The process exists... */
    if(stat(pathname, &statbuf) == 0) {
      snprintf(pathname, sizeof(pathname), "/proc/%u/exe", pid);

      if(stat(pathname, &statbuf) == -1)
	return(1); /* It looks like a kernel thread */
    }

    return(0);
  }

  /* ******************************************* */

  static void check_pid(struct taskInfo *task) {
    if((task->pid != 0) && is_kernel_thread(task->pid))
      memset(task, 0, sizeof(struct taskInfo));
  }

  /* ******************************************* */

  static void fill_exe_path_name(struct taskInfo *task) {
    if(task->pid != 0) {
      char what[256], sym[256];
      int l;

      snprintf(what, sizeof(what), "/proc/%u/exe", task->pid);
      if((l = readlink(what, sym, sizeof(sym))) != -1) {
	char *space;
	
        sym[l] = '\0';

	if((space = strchr(sym, ' ')) != NULL) {
	  if(space[1] == '(') /* (deleted) */
	    space[0] = '\0';
	}
	
        task->full_task_path = strdup(sym);
      }
    }
  }

  /* ******************************************* */

  void ebpf_preprocess_event(eBPFevent *event) {
    struct container_info *container_info;

    gettimeofday(&event->event_time, NULL);
    check_pid(&event->proc), check_pid(&event->father);

    event->proc.full_task_path = NULL;

    fill_exe_path_name(&event->proc);
    fill_exe_path_name(&event->father);

    if((event->container_id[0] == '\0')
       || ((event->container_id[0] == '/') && (event->container_id[1] == '\0')))
      event->container_id[0] = '\0';
    else {
      event->docker.name = event->kube.name = event->kube.pod = event->kube.ns = NULL;
      
      // Docker names cgroup as follows: "docker-<container_id>.scope"
      if(strstr(event->container_id, "docker-") != NULL) {
        event->container_id[71] = '\0';
        memmove(event->container_id, event->container_id+7, strlen(event->container_id)-6);
      }
      
      if(cinfo.get_container_info(event->container_id, &container_info) == 0) {
        if(container_info->docker.name[0] != '\0') /* Docker info available */
          event->docker.name = strdup(container_info->docker.name.c_str());
       
        if(container_info->kube.name[0] != '\0') /* Kubernetes info available */ {
          event->kube.name = strdup(container_info->kube.name.c_str());
          event->kube.pod  = strdup(container_info->kube.pod.c_str());
          event->kube.ns   = strdup(container_info->kube.ns.c_str());
        }
      }
    }
  }

  /* ******************************************* */

  void ebpf_free_event(eBPFevent *event) {
    if(event->proc.full_task_path != NULL)
      free(event->proc.full_task_path);

    if(event->father.full_task_path != NULL)
      free(event->father.full_task_path);

    if(event->docker.name != NULL)  free(event->docker.name);
    if(event->kube.name != NULL)    free(event->kube.name);
    if(event->kube.pod != NULL)     free(event->kube.pod);
    if(event->kube.ns != NULL)      free(event->kube.ns);
  }

  /* ******************************************* */

  void term_ebpf_flow(void *ebpfHook) {
    ebpf::BPF *bpf = (ebpf::BPF*)ebpfHook;

    if(bpf != NULL)
      delete bpf;
  }

  /* ******************************************* */

  int ebpf_poll_event(void *ebpfHook, u_int ms_timeout) {
    ebpf::BPF *bpf = (ebpf::BPF*)ebpfHook;

    /* NOTE: some library versions do not return a value [TODO] */
    /* int rc = */ bpf->poll_perf_buffer("ebpf_events", ms_timeout);

    return(1);
  }

  /* ******************************************* */

  const char* ebpf_print_error(ebpfRetCode rc) {
    switch(rc) {
    case ebpf_no_error:
      return("ebpf_no_error");

    case ebpf_initialization_failed:
      return("ebpf_initialization_failed");

    case ebpf_unable_to_load_kernel_probe:
      return("ebpf_unable_to_load_kernel_probe");

    case ebpf_out_of_memory:
      return("ebpf_out_of_memory");

    case ebpf_kprobe_attach_error:
      return("ebpf_kprobe_attach_error");

    case ebpf_events_open_error:
      return("ebpf_events_open_error");
    }

    return("Unknown error");
  }

  /* ******************************************* */

  void* ebpf_get_cinfo_handler() { return((void*)&cinfo); }

  /* ******************************************* */

  const char* ebpf_flow_version() {
    return(EBPF_FLOW_VERSION);
  }

};
