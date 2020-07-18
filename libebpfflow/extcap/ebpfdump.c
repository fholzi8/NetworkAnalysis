/*
 *
 * (C) 2019 - ntop.org
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lessed General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <getopt.h>
#include <pcap/pcap.h>
#include <signal.h>
#include <stdlib.h>

#include "ebpf_flow.h"

#define EBPFDUMP_INTERFACE "ebpf"

#define EXIT_SUCCESS 0

#define EBPFDUMP_MAX_NBPF_LEN    8192
#define EBPFDUMP_MAX_DATE_LEN    26
#define EBPFDUMP_MAX_NAME_LEN    4096

#define EBPFDUMP_VERSION_MAJOR   "0"
#define EBPFDUMP_VERSION_MINOR   "1"
#define EBPFDUMP_VERSION_RELEASE "0"

#define EXTCAP_OPT_LIST_INTERFACES	'l'
#define EXTCAP_OPT_VERSION		'v'
#define EXTCAP_OPT_LIST_DLTS		'L'
#define EXTCAP_OPT_INTERFACE		'i'
#define EXTCAP_OPT_CONFIG		'c'
#define EXTCAP_OPT_CAPTURE		'C'
#define EXTCAP_OPT_FIFO			'F'
#define EXTCAP_OPT_DEBUG		'D'
#define EBPFDUMP_OPT_HELP		'h'
#define EBPFDUMP_OPT_NAME		'n'
#define EBPFDUMP_OPT_CUSTOM_NAME	'N'

static struct option longopts[] = {
  /* mandatory extcap options */
  { "extcap-interfaces",	no_argument, 		NULL, EXTCAP_OPT_LIST_INTERFACES },
  { "extcap-version", 		optional_argument, 	NULL, EXTCAP_OPT_VERSION },
  { "extcap-dlts", 		no_argument, 		NULL, EXTCAP_OPT_LIST_DLTS },
  { "extcap-interface", 	required_argument, 	NULL, EXTCAP_OPT_INTERFACE },
  { "extcap-config", 		no_argument, 		NULL, EXTCAP_OPT_CONFIG },
  { "capture", 			no_argument, 		NULL, EXTCAP_OPT_CAPTURE },
  { "fifo", 			required_argument, 	NULL, EXTCAP_OPT_FIFO },
  { "debug", 			optional_argument, 	NULL, EXTCAP_OPT_DEBUG },

  /* custom extcap options */
  { "help", 			no_argument, 		NULL, EBPFDUMP_OPT_HELP },
  { "name", 			required_argument,	NULL, EBPFDUMP_OPT_NAME },
  { "custom-name", 		required_argument, 	NULL, EBPFDUMP_OPT_CUSTOM_NAME },

  {0, 0, 0, 0}
};

typedef struct _extcap_interface {
  const char * interface;
  const char * description;
  u_int16_t dlt;
  const char * dltname;
  const char * dltdescription;
} extcap_interface;

#define DLT_EN10MB 1

static extcap_interface extcap_interfaces[] = {
  { EBPFDUMP_INTERFACE, "eBPF interface", DLT_EN10MB, NULL, "The EN10MB Ethernet2 DLT" },
};

static size_t extcap_interfaces_num = sizeof(extcap_interfaces) / sizeof(extcap_interface);

static char *extcap_selected_interface   = NULL;
static char *extcap_capture_fifo         = NULL;
static pcap_dumper_t *dumper             = NULL;

/* ***************************************************** */

void sigproc(int sig) {
  fprintf(stdout, "Exiting...");
  fflush(stdout);
}

/* ***************************************************** */

void extcap_version() {
  /* Print version */
  printf("extcap {version=%s.%s.%s}\n", EBPFDUMP_VERSION_MAJOR, EBPFDUMP_VERSION_MINOR, EBPFDUMP_VERSION_RELEASE);
}

/* ***************************************************** */

void extcap_list_interfaces() {
  int i;

  for(i = 0; i < extcap_interfaces_num; i++) {
    printf("interface {value=%s}{display=%s}\n", extcap_interfaces[i].interface, extcap_interfaces[i].description);
  }
}

/* ***************************************************** */

void extcap_dlts() {
  int i;

  if(!extcap_selected_interface) return;
  for(i = 0; i < extcap_interfaces_num; i++) {
    extcap_interface *eif = &extcap_interfaces[i];

    if(!strncmp(extcap_selected_interface, eif->interface, strlen(eif->interface))) {
      printf("dlt {number=%u}{name=%s}{display=%s}\n", eif->dlt, eif->interface, eif->dltdescription);
      break;
    }
  }
}

/* ***************************************************** */

int exec_head(const char *bin, char *line, size_t line_len) {
  FILE *fp;

  fp = popen(bin, "r");

  if (fp == NULL)
    return -1;

  if (fgets(line, line_len-1, fp) == NULL) {
    pclose(fp);
    return -1;
  }

  pclose(fp);
  return 0;
}

/* ***************************************************** */

float wireshark_version() {
  char line[1035];
  char *version, *rev;
  float v = 0;

  if (exec_head("/usr/bin/wireshark -v", line, sizeof(line)) != 0 &&
      exec_head("/usr/local/bin/wireshark -v", line, sizeof(line)) != 0)
    return 0;

  version = strchr(line, ' ');
  if (version == NULL) return 0;
  version++;
  rev = strchr(version, '.');
  if (rev == NULL) return 0;
  rev++;
  rev = strchr(rev, '.');
  if (rev == NULL) return 0;
  *rev = '\0';

  sscanf(version, "%f", &v);

  return v;
}

/* ***************************************************** */

void extcap_config() {
  u_int argidx = 0;

  if(!extcap_selected_interface) return;

  if(!strncmp(extcap_selected_interface, EBPFDUMP_INTERFACE, strlen(EBPFDUMP_INTERFACE))) {
    u_int nameidx;

    nameidx = argidx;
    printf("arg {number=%u}{call=--name}"
	   "{display=Interface Name}{type=radio}"
	   "{tooltip=The interface name}\n", argidx++);
  }
}

/* ***************************************************** */

static void ebpfHandler(void* t_bpfctx, void* t_data, int t_datasize) {
  eBPFevent *e = (eBPFevent*)t_data;
  eBPFevent event;
  struct timespec tp;
  struct pcap_pkthdr hdr;
  
  memcpy(&event, e, sizeof(eBPFevent)); /* Copy needed as ebpf_preprocess_event will modify the memory */

  ebpf_preprocess_event(&event, true);

  gettimeofday(&hdr.ts, NULL);
  hdr.len = hdr.caplen = sizeof(event);
  
  pcap_dump((u_char*)dumper, (struct pcap_pkthdr*)&hdr, (const u_char*)&event);
  ebpf_free_event(&event);
}

/* ***************************************************** */

void extcap_capture() {
  ebpfRetCode rc;
  void *ebpf;
  u_int num = 0;

  ebpf = init_ebpf_flow(NULL, ebpfHandler, &rc, 0xFFFF);
  
  if(ebpf == NULL) {
    fprintf(stderr, "Unable to initialize libebpfflow\n");
    return;
  }
  
  if((dumper = pcap_dump_open(pcap_open_dead(DLT_EN10MB, 16384 /* MTU */), extcap_capture_fifo)) == NULL) {
    fprintf(stderr, "Unable to open the pcap dumper on %s", extcap_capture_fifo);
    return;
  }

  if((signal(SIGINT, sigproc) == SIG_ERR)
     || (signal(SIGTERM, sigproc) == SIG_ERR)
     || (signal(SIGQUIT, sigproc) == SIG_ERR)) {
    fprintf(stderr, "Unable to install SIGINT/SIGTERM signal handler");
    return;
  }

  while(1) {
    /* fprintf(stderr, "%u\n", ++num); */
    ebpf_poll_event(ebpf, 10);
  }
  
  term_ebpf_flow(ebpf);
  pcap_dump_close(dumper);
}

/* ***************************************************** */

int extcap_print_help() {
  printf("Wireshark extcap eBPF plugin by ntop\n");
  printf("Supported interfaces:\n");
  extcap_list_interfaces();
  return 0;
}

/* ***************************************************** */

int main(int argc, char *argv[]) {
  int option_idx = 0, result;
  time_t epoch;
  char date_str[EBPFDUMP_MAX_DATE_LEN];
  struct tm* tm_info;

  if (argc == 1) {
    extcap_print_help();
    return EXIT_SUCCESS;
  }

  u_int defer_dlts = 0, defer_config = 0, defer_capture = 0;
  while ((result = getopt_long(argc, argv, "h", longopts, &option_idx)) != -1) {
    // fprintf(stderr, "OPT: '%c' VAL: '%s' \n", result, optarg != NULL ? optarg : "");
    
    switch (result) {
      /* mandatory extcap options */
    case EXTCAP_OPT_DEBUG:
      break;
    case EXTCAP_OPT_LIST_INTERFACES:
      extcap_version();
      extcap_list_interfaces();
      defer_dlts = defer_config = defer_capture = 0;
      break;
    case EXTCAP_OPT_VERSION:
      extcap_version();
      defer_dlts = defer_config = defer_capture = 0;
      break;
    case EXTCAP_OPT_LIST_DLTS:
      defer_dlts = 1; defer_config = defer_capture = 0;
      break;
    case EXTCAP_OPT_INTERFACE:
      extcap_selected_interface = strndup(optarg, EBPFDUMP_MAX_NAME_LEN);
      break;
    case EXTCAP_OPT_CONFIG:
      defer_config = 1; defer_dlts = defer_capture = 0;
      break;
    case EXTCAP_OPT_CAPTURE:
      defer_capture = 1; defer_dlts = defer_config = 0;
      break;
      break;
    case EXTCAP_OPT_FIFO:
      extcap_capture_fifo = strdup(optarg);
      break;

      /* custom ebpfdump options */
    case EBPFDUMP_OPT_HELP:
      extcap_print_help();
      return EXIT_SUCCESS;
      break;
    }
  }

  if(defer_dlts) extcap_dlts();
  else if(defer_config) extcap_config();
  else if(defer_capture) extcap_capture();

  if(extcap_selected_interface)   free(extcap_selected_interface);
  if(extcap_capture_fifo)         free(extcap_capture_fifo);

  return EXIT_SUCCESS;
}
