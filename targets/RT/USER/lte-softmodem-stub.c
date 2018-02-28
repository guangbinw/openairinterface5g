/*
 * lte-softmodem-stub.c
 *
 *  Created on: Nov 27, 2017
 *      Author: montre
 */


/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file lte-enb.c
 * \brief Top-level threads for eNodeB
 * \author R. Knopp, F. Kaltenberger, Navid Nikaein
 * \date 2012
 * \version 0.1
 * \company Eurecom
 * \email: knopp@eurecom.fr,florian.kaltenberger@eurecom.fr, navid.nikaein@eurecom.fr
 * \note
 * \warning
 */


#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>


#include "T.h"

#include "rt_wrapper.h"


#undef MALLOC //there are two conflicting definitions, so we better make sure we don't use it at all

#include "assertions.h"
#include "msc.h"

#include "PHY/types.h"

#include "PHY/defs.h"
#include "common/ran_context.h"
#include "common/config/config_userapi.h"
#include "common/utils/load_module_shlib.h"
#undef MALLOC //there are two conflicting definitions, so we better make sure we don't use it at all
//#undef FRAME_LENGTH_COMPLEX_SAMPLES //there are two conflicting definitions, so we better make sure we don't use it at all

#include "../../ARCH/COMMON/common_lib.h"
#include "../../ARCH/ETHERNET/USERSPACE/LIB/if_defs.h"

//#undef FRAME_LENGTH_COMPLEX_SAMPLES //there are two conflicting definitions, so we better make sure we don't use it at all

#include "PHY/vars.h"
#include "SCHED/vars.h"
#include "LAYER2/MAC/vars.h"

#include "../../SIMU/USER/init_lte.h"

#include "LAYER2/MAC/defs.h"
#include "LAYER2/MAC/vars.h"
#include "LAYER2/MAC/proto.h"
#include "RRC/LITE/vars.h"
#include "PHY_INTERFACE/vars.h"

#ifdef SMBV
#include "PHY/TOOLS/smbv.h"
unsigned short config_frames[4] = {2,9,11,13};
#endif
#include "UTIL/LOG/log_extern.h"
#include "UTIL/OTG/otg_tx.h"
#include "UTIL/OTG/otg_externs.h"
#include "UTIL/MATH/oml.h"
#include "UTIL/LOG/vcd_signal_dumper.h"
#include "UTIL/OPT/opt.h"
#include "enb_config.h"
#include "targets/COMMON/create_tasks.h"
//#include "PHY/TOOLS/time_meas.h"

#ifndef OPENAIR2
#include "UTIL/OTG/otg_vars.h"
#endif

#define D2D_MODE

#if defined(ENABLE_ITTI)
#include "intertask_interface_init.h"
#include "create_tasks.h"
#endif

#include "system.h"

#ifdef XFORMS
#include "PHY/TOOLS/lte_phy_scope.h"
#include "stats.h"
#endif
#include "lte-softmodem.h"

#ifdef XFORMS
// current status is that every UE has a DL scope for a SINGLE eNB (eNB_id=0)
// at eNB 0, an UL scope for every UE
FD_lte_phy_scope_ue  *form_ue[NUMBER_OF_UE_MAX];
FD_lte_phy_scope_enb *form_enb[MAX_NUM_CCs][NUMBER_OF_UE_MAX];
FD_stats_form                  *form_stats=NULL,*form_stats_l2=NULL;
char title[255];
unsigned char                   scope_enb_num_ue = 2;
static pthread_t                forms_thread; //xforms
#endif //XFORMS

pthread_cond_t nfapi_sync_cond;
pthread_mutex_t nfapi_sync_mutex;
int nfapi_sync_var=-1; //!< protected by mutex \ref nfapi_sync_mutex

uint8_t nfapi_mode = 3;

uint16_t sf_ahead=2;

char *emul_iface;

pthread_cond_t sync_cond;
pthread_mutex_t sync_mutex;
int sync_var=-1; //!< protected by mutex \ref sync_mutex.
int config_sync_var=-1;

uint16_t runtime_phy_rx[29][6]; // SISO [MCS 0-28][RBs 0-5 : 6, 15, 25, 50, 75, 100]
uint16_t runtime_phy_tx[29][6]; // SISO [MCS 0-28][RBs 0-5 : 6, 15, 25, 50, 75, 100]

#if defined(ENABLE_ITTI)
volatile int             start_eNB = 0;
volatile int             start_UE = 0;
#endif
volatile int             oai_exit = 0;

static clock_source_t clock_source = internal;
static int wait_for_sync = 0;

static char              UE_flag=0;
unsigned int                    mmapped_dma=0;
int                             single_thread_flag=1;

static char                     threequarter_fs=0;

uint32_t                 downlink_frequency[MAX_NUM_CCs][4];
int32_t                  uplink_frequency_offset[MAX_NUM_CCs][4];



#if defined(ENABLE_ITTI)
static char                    *itti_dump_file = NULL;
#endif

int UE_scan = 1;
int UE_scan_carrier = 0;
runmode_t mode = normal_txrx;

FILE *input_fd=NULL;


#if MAX_NUM_CCs == 1
rx_gain_t                rx_gain_mode[MAX_NUM_CCs][4] = {{max_gain,max_gain,max_gain,max_gain}};
double tx_gain[MAX_NUM_CCs][4] = {{20,0,0,0}};
double rx_gain[MAX_NUM_CCs][4] = {{110,0,0,0}};
#else
rx_gain_t                rx_gain_mode[MAX_NUM_CCs][4] = {{max_gain,max_gain,max_gain,max_gain},{max_gain,max_gain,max_gain,max_gain}};
double tx_gain[MAX_NUM_CCs][4] = {{20,0,0,0},{20,0,0,0}};
double rx_gain[MAX_NUM_CCs][4] = {{110,0,0,0},{20,0,0,0}};
#endif

double rx_gain_off = 0.0;

double sample_rate=30.72e6;
double bw = 10.0e6;

static int                      tx_max_power[MAX_NUM_CCs]; /* =  {0,0}*/;

char   rf_config_file[1024];

int chain_offset=0;
int phy_test = 0;
uint8_t usim_test = 0;

uint8_t dci_Format = 0;
uint8_t agregation_Level =0xFF;

uint8_t nb_antenna_tx = 1;
uint8_t nb_antenna_rx = 1;

char ref[128] = "internal";
char channels[128] = "0";

int                      rx_input_level_dBm;

#ifdef XFORMS
extern int                      otg_enabled;
static char                     do_forms=0;
#else
int                             otg_enabled;
#endif
//int                             number_of_cards =   1;


static LTE_DL_FRAME_PARMS      *frame_parms[MAX_NUM_CCs];
uint32_t target_dl_mcs = 28; //maximum allowed mcs
uint32_t target_ul_mcs = 20;
uint32_t timing_advance = 0;
uint8_t exit_missed_slots=1;
uint64_t num_missed_slots=0; // counter for the number of missed slots


extern void reset_opp_meas(void);
extern void print_opp_meas(void);

extern PHY_VARS_UE* init_ue_vars(LTE_DL_FRAME_PARMS *frame_parms,
			  uint8_t UE_id,
			  uint8_t abstraction_flag);

extern void init_eNB_afterRU(void);
extern void init_UE_stub(int nb_inst,int,int,char*);
extern int init_timer_thread(void);

int transmission_mode=1;



/* struct for ethernet specific parameters given in eNB conf file */
eth_params_t *eth_params;

openair0_config_t openair0_cfg[MAX_CARDS];

double cpuf;

extern char uecap_xer[1024];
char uecap_xer_in=0;

int oaisim_flag=0;
threads_t threads= {-1,-1,-1,-1,-1,-1,-1};

/* see file openair2/LAYER2/MAC/main.c for why abstraction_flag is needed
 * this is very hackish - find a proper solution
 */
uint8_t abstraction_flag=0;

/*---------------------BMC: timespec helpers -----------------------------*/

struct timespec min_diff_time = { .tv_sec = 0, .tv_nsec = 0 };
struct timespec max_diff_time = { .tv_sec = 0, .tv_nsec = 0 };

struct timespec clock_difftime(struct timespec start, struct timespec end) {
  struct timespec temp;
  if ((end.tv_nsec-start.tv_nsec)<0) {
    temp.tv_sec = end.tv_sec-start.tv_sec-1;
    temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
  } else {
    temp.tv_sec = end.tv_sec-start.tv_sec;
    temp.tv_nsec = end.tv_nsec-start.tv_nsec;
  }
  return temp;
}

void print_difftimes(void) {
#ifdef DEBUG
  printf("difftimes min = %lu ns ; max = %lu ns\n", min_diff_time.tv_nsec, max_diff_time.tv_nsec);
#else
  LOG_I(HW,"difftimes min = %lu ns ; max = %lu ns\n", min_diff_time.tv_nsec, max_diff_time.tv_nsec);
#endif
}

void update_difftimes(struct timespec start, struct timespec end) {
  struct timespec diff_time = { .tv_sec = 0, .tv_nsec = 0 };
  int             changed = 0;
  diff_time = clock_difftime(start, end);
  if ((min_diff_time.tv_nsec == 0) || (diff_time.tv_nsec < min_diff_time.tv_nsec)) {
    min_diff_time.tv_nsec = diff_time.tv_nsec;
    changed = 1;
  }
  if ((max_diff_time.tv_nsec == 0) || (diff_time.tv_nsec > max_diff_time.tv_nsec)) {
    max_diff_time.tv_nsec = diff_time.tv_nsec;
    changed = 1;
  }
#if 1
  if (changed) print_difftimes();
#endif
}

/*------------------------------------------------------------------------*/

unsigned int build_rflocal(int txi, int txq, int rxi, int rxq) {
  return (txi + (txq<<6) + (rxi<<12) + (rxq<<18));
}
unsigned int build_rfdc(int dcoff_i_rxfe, int dcoff_q_rxfe) {
  return (dcoff_i_rxfe + (dcoff_q_rxfe<<8));
}

#if !defined(ENABLE_ITTI)
void signal_handler(int sig) {
  void *array[10];
  size_t size;

  if (sig==SIGSEGV) {
    // get void*'s for all entries on the stack
    size = backtrace(array, 10);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, 2);
    exit(-1);
  } else {
    printf("trying to exit gracefully...\n");
    oai_exit = 1;
  }
}
#endif
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KBLU  "\x1B[34m"
#define RESET "\033[0m"



void exit_fun(const char* s)
{
  int CC_id;
  int ru_id;

  if (s != NULL) {
    printf("%s %s() Exiting OAI softmodem: %s\n",__FILE__, __FUNCTION__, s);
  }

  oai_exit = 1;

  if (UE_flag==0) {
    for (ru_id=0; ru_id<RC.nb_RU;ru_id++) {
      if (RC.ru[ru_id]->rfdevice.trx_end_func)
	RC.ru[ru_id]->rfdevice.trx_end_func(&RC.ru[ru_id]->rfdevice);
      if (RC.ru[ru_id]->ifdevice.trx_end_func)
	RC.ru[ru_id]->ifdevice.trx_end_func(&RC.ru[ru_id]->ifdevice);
    }
  }

  for(CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {

    oai_exit = 1;

    for(CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
      if (UE_flag == 0) {
      } else {
	if (PHY_vars_UE_g[0][CC_id]->rfdevice.trx_end_func)
	  PHY_vars_UE_g[0][CC_id]->rfdevice.trx_end_func(&PHY_vars_UE_g[0][CC_id]->rfdevice);
      }
    }

#if defined(ENABLE_ITTI)
    sleep(1); //allow lte-softmodem threads to exit first
    itti_terminate_tasks (TASK_UNKNOWN);
#endif

  }

}

#ifdef XFORMS


void reset_stats(FL_OBJECT *button, long arg)
{
  int i,j,k;
  PHY_VARS_eNB *phy_vars_eNB = RC.eNB[0][0];

  for (i=0; i<NUMBER_OF_UE_MAX; i++) {
    for (k=0; k<8; k++) { //harq_processes
      for (j=0; j<phy_vars_eNB->dlsch[i][0]->Mlimit; j++) {
	phy_vars_eNB->UE_stats[i].dlsch_NAK[k][j]=0;
	phy_vars_eNB->UE_stats[i].dlsch_ACK[k][j]=0;
	phy_vars_eNB->UE_stats[i].dlsch_trials[k][j]=0;
      }

      phy_vars_eNB->UE_stats[i].dlsch_l2_errors[k]=0;
      phy_vars_eNB->UE_stats[i].ulsch_errors[k]=0;
      phy_vars_eNB->UE_stats[i].ulsch_consecutive_errors=0;


      phy_vars_eNB->UE_stats[i].dlsch_sliding_cnt=0;
      phy_vars_eNB->UE_stats[i].dlsch_NAK_round0=0;
      phy_vars_eNB->UE_stats[i].dlsch_mcs_offset=0;
    }
  }
}

static void *scope_thread(void *arg) {
  char stats_buffer[16384];
# ifdef ENABLE_XFORMS_WRITE_STATS
  FILE *UE_stats, *eNB_stats;
# endif
  struct sched_param sched_param;
  int UE_id, CC_id;
  int ue_cnt=0;

  sched_param.sched_priority = sched_get_priority_min(SCHED_FIFO)+1;
  sched_setscheduler(0, SCHED_FIFO,&sched_param);

  printf("Scope thread has priority %d\n",sched_param.sched_priority);

# ifdef ENABLE_XFORMS_WRITE_STATS

  if (UE_flag==1)
    UE_stats  = fopen("UE_stats.txt", "w");
  else
    eNB_stats = fopen("eNB_stats.txt", "w");

#endif

  while (!oai_exit) {
    if (UE_flag==1) {
      dump_ue_stats (PHY_vars_UE_g[0][0], &PHY_vars_UE_g[0][0]->proc.proc_rxtx[0],stats_buffer, 0, mode,rx_input_level_dBm);
      //fl_set_object_label(form_stats->stats_text, stats_buffer);
      fl_clear_browser(form_stats->stats_text);
      fl_add_browser_line(form_stats->stats_text, stats_buffer);

      phy_scope_UE(form_ue[0],
		   PHY_vars_UE_g[0][0],
		   0,
		   0,7);


    } else {
      /*
	if (RC.eNB[0][0]->mac_enabled==1) {
	len = dump_eNB_l2_stats (stats_buffer, 0);
	//fl_set_object_label(form_stats_l2->stats_text, stats_buffer);
	fl_clear_browser(form_stats_l2->stats_text);
	fl_add_browser_line(form_stats_l2->stats_text, stats_buffer);
	}
	len = dump_eNB_stats (RC.eNB[0][0], stats_buffer, 0);

	if (MAX_NUM_CCs>1)
	len += dump_eNB_stats (RC.eNB[0][1], &stats_buffer[len], 0);

	//fl_set_object_label(form_stats->stats_text, stats_buffer);
	fl_clear_browser(form_stats->stats_text);
	fl_add_browser_line(form_stats->stats_text, stats_buffer);
      */
      ue_cnt=0;
      for(UE_id=0; UE_id<NUMBER_OF_UE_MAX; UE_id++) {
	for(CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
	  //	  if ((RC.eNB[0][CC_id]->dlsch[UE_id][0]->rnti>0) && (ue_cnt<scope_enb_num_ue)) {
	  if ((ue_cnt<scope_enb_num_ue)) {
	    phy_scope_eNB(form_enb[CC_id][ue_cnt],
			  RC.eNB[0][CC_id],
			  UE_id);
	    ue_cnt++;
	  }
	}
      }

    }

    //printf("doing forms\n");
    //usleep(100000); // 100 ms
    sleep(1);
  }

  //  printf("%s",stats_buffer);

# ifdef ENABLE_XFORMS_WRITE_STATS

  if (UE_flag==1) {
    if (UE_stats) {
      rewind (UE_stats);
      fwrite (stats_buffer, 1, len, UE_stats);
      fclose (UE_stats);
    }
  } else {
    if (eNB_stats) {
      rewind (eNB_stats);
      fwrite (stats_buffer, 1, len, eNB_stats);
      fclose (eNB_stats);
    }
  }

# endif

  pthread_exit((void*)arg);
}
#endif




#if defined(ENABLE_ITTI)
void *l2l1_task(void *arg) {
  MessageDef *message_p = NULL;
  int         result;

  itti_set_task_real_time(TASK_L2L1);
  itti_mark_task_ready(TASK_L2L1);

  if (UE_flag == 0) {
    /* Wait for the initialize message */
    printf("Wait for the ITTI initialize message\n");
    do {
      if (message_p != NULL) {
	result = itti_free (ITTI_MSG_ORIGIN_ID(message_p), message_p);
	AssertFatal (result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
      }

      itti_receive_msg (TASK_L2L1, &message_p);

      switch (ITTI_MSG_ID(message_p)) {
      case INITIALIZE_MESSAGE:
	/* Start eNB thread */
	LOG_D(EMU, "L2L1 TASK received %s\n", ITTI_MSG_NAME(message_p));
	start_eNB = 1;
	break;

      case TERMINATE_MESSAGE:
	printf("received terminate message\n");
	oai_exit=1;
	itti_exit_task ();
	break;

      default:
	LOG_E(EMU, "Received unexpected message %s\n", ITTI_MSG_NAME(message_p));
	break;
      }
    } while (ITTI_MSG_ID(message_p) != INITIALIZE_MESSAGE);

    result = itti_free (ITTI_MSG_ORIGIN_ID(message_p), message_p);
    AssertFatal (result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
  }

  do {
    // Wait for a message
    itti_receive_msg (TASK_L2L1, &message_p);

    switch (ITTI_MSG_ID(message_p)) {
    case TERMINATE_MESSAGE:
      oai_exit=1;
      itti_exit_task ();
      break;

    case ACTIVATE_MESSAGE:
      start_UE = 1;
      break;

    case DEACTIVATE_MESSAGE:
      start_UE = 0;
      break;

    case MESSAGE_TEST:
      LOG_I(EMU, "Received %s\n", ITTI_MSG_NAME(message_p));
      break;

    default:
      LOG_E(EMU, "Received unexpected message %s\n", ITTI_MSG_NAME(message_p));
      break;
    }

    result = itti_free (ITTI_MSG_ORIGIN_ID(message_p), message_p);
    AssertFatal (result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
  } while(!oai_exit);

  return NULL;
}
#endif

void set_default_frame_parms(LTE_DL_FRAME_PARMS *frame_parms[MAX_NUM_CCs]);
void set_default_frame_parms(LTE_DL_FRAME_PARMS *frame_parms[MAX_NUM_CCs]) {

  int CC_id;
  printf("Panos: set_default_frame_parms1 %d \n",MAX_NUM_CCs);

  for (CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
    frame_parms[CC_id] = (LTE_DL_FRAME_PARMS*) malloc(sizeof(LTE_DL_FRAME_PARMS));
    /* Set some default values that may be overwritten while reading options */
    frame_parms[CC_id]->frame_type          = FDD;
    frame_parms[CC_id]->tdd_config          = 3;
    frame_parms[CC_id]->tdd_config_S        = 0;
    frame_parms[CC_id]->N_RB_DL             = 100;

    frame_parms[CC_id]->N_RB_UL             = 100;
    frame_parms[CC_id]->Ncp                 = NORMAL;
    frame_parms[CC_id]->Ncp_UL              = NORMAL;
    frame_parms[CC_id]->Nid_cell            = 0;
    frame_parms[CC_id]->num_MBSFN_config    = 0;
    frame_parms[CC_id]->nb_antenna_ports_eNB  = 1;
    frame_parms[CC_id]->nb_antennas_tx      = 1;
    frame_parms[CC_id]->nb_antennas_rx      = 1;

    frame_parms[CC_id]->nushift             = 0;

    frame_parms[CC_id]->phich_config_common.phich_resource = oneSixth;
    frame_parms[CC_id]->phich_config_common.phich_duration = normal;
    // UL RS Config
    frame_parms[CC_id]->pusch_config_common.ul_ReferenceSignalsPUSCH.cyclicShift = 0;//n_DMRS1 set to 0
    frame_parms[CC_id]->pusch_config_common.ul_ReferenceSignalsPUSCH.groupHoppingEnabled = 0;
    frame_parms[CC_id]->pusch_config_common.ul_ReferenceSignalsPUSCH.sequenceHoppingEnabled = 0;
    frame_parms[CC_id]->pusch_config_common.ul_ReferenceSignalsPUSCH.groupAssignmentPUSCH = 0;

    frame_parms[CC_id]->prach_config_common.rootSequenceIndex=22;
    frame_parms[CC_id]->prach_config_common.prach_ConfigInfo.zeroCorrelationZoneConfig=1;
    frame_parms[CC_id]->prach_config_common.prach_ConfigInfo.prach_ConfigIndex=0;
    frame_parms[CC_id]->prach_config_common.prach_ConfigInfo.highSpeedFlag=0;
    frame_parms[CC_id]->prach_config_common.prach_ConfigInfo.prach_FreqOffset=0;

    downlink_frequency[CC_id][0] = 2680000000; // Use float to avoid issue with frequency over 2^31.
    downlink_frequency[CC_id][1] = downlink_frequency[CC_id][0];
    downlink_frequency[CC_id][2] = downlink_frequency[CC_id][0];
    downlink_frequency[CC_id][3] = downlink_frequency[CC_id][0];
    //printf("Downlink for CC_id %d frequency set to %u\n", CC_id, downlink_frequency[CC_id][0]);

  }

}


static void get_options(void) {
  int CC_id;
  int tddflag;
  char *loopfile=NULL;
  int dumpframe;
  uint32_t online_log_messages;
  uint32_t glog_level, glog_verbosity;
  uint32_t start_telnetsrv;

  paramdef_t cmdline_params[] =CMDLINE_PARAMS_DESC ;
  paramdef_t cmdline_logparams[] =CMDLINE_LOGPARAMS_DESC ;


  config_process_cmdline( cmdline_params,sizeof(cmdline_params)/sizeof(paramdef_t),NULL);
  printf("get_options 0, UE_flag: %d", UE_flag);

  if (strlen(in_path) > 0) {
      opt_type = OPT_PCAP;
      opt_enabled=1;
      printf("Enabling OPT for PCAP  with the following file %s \n",in_path);
  }
  if (strlen(in_ip) > 0) {
      opt_enabled=1;
      opt_type = OPT_WIRESHARK;
      printf("Enabling OPT for wireshark for local interface");
  }
  config_process_cmdline( cmdline_logparams,sizeof(cmdline_logparams)/sizeof(paramdef_t),NULL);
  if(config_isparamset(cmdline_logparams,CMDLINE_ONLINELOG_IDX)) {
      set_glog_onlinelog(online_log_messages);
  }
  if(config_isparamset(cmdline_logparams,CMDLINE_GLOGLEVEL_IDX)) {
      set_glog(glog_level, -1);
  }
  if(config_isparamset(cmdline_logparams,CMDLINE_GLOGVERBO_IDX)) {
      set_glog(-1, glog_verbosity);
  }
  if (start_telnetsrv) {
     load_module_shlib("telnetsrv",NULL,0);
  }

  if (UE_flag > 0) {

     uint8_t n_rb_dl;

     paramdef_t cmdline_uemodeparams[] =CMDLINE_UEMODEPARAMS_DESC;
     paramdef_t cmdline_ueparams[] =CMDLINE_UEPARAMS_DESC; 

     set_default_frame_parms(frame_parms);

     emul_iface=malloc(100);

     config_process_cmdline( cmdline_uemodeparams,sizeof(cmdline_uemodeparams)/sizeof(paramdef_t),NULL);
     config_process_cmdline( cmdline_ueparams,sizeof(cmdline_ueparams)/sizeof(paramdef_t),NULL);


      if (loopfile != NULL) {
  	  printf("Input file for hardware emulation: %s",loopfile);
  	  mode=loop_through_memory;
  	  input_fd = fopen(loopfile,"r");
  	  AssertFatal(input_fd != NULL,"Please provide a valid input file\n");
      }
      if ( (cmdline_uemodeparams[CMDLINE_CALIBUERX_IDX].paramflags &  PARAMFLAG_PARAMSET) != 0) mode = rx_calib_ue;
      if ( (cmdline_uemodeparams[CMDLINE_CALIBUERXMED_IDX].paramflags &  PARAMFLAG_PARAMSET) != 0) mode = rx_calib_ue_med;
      printf("Panos: get_options 9 \n");
      if ( (cmdline_uemodeparams[CMDLINE_CALIBUERXBYP_IDX].paramflags &  PARAMFLAG_PARAMSET) != 0) mode = rx_calib_ue_byp;
      printf("Panos: get_options 10 \n");
      if ( (cmdline_uemodeparams[CMDLINE_DEBUGUEPRACH_IDX].paramflags &  PARAMFLAG_PARAMSET) != 0) mode = debug_prach;
      if ( (cmdline_uemodeparams[CMDLINE_NOL2CONNECT_IDX].paramflags &  PARAMFLAG_PARAMSET) != 0)  mode = no_L2_connect;
      if ( (cmdline_uemodeparams[CMDLINE_CALIBPRACHTX_IDX].paramflags &  PARAMFLAG_PARAMSET) != 0) mode = calib_prach_tx; 
      printf("Panos: get_options 11 \n");
      if (dumpframe  > 0)  mode = rx_dump_frame;
      if ( downlink_frequency[0][0] > 0) {
        printf("Downlink frequency set to %u\n", downlink_frequency[0][0]);
        for (CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
          frame_parms[CC_id]->dl_CarrierFreq = downlink_frequency[0][0];
        }
        UE_scan=0;
      } 
      
      if (tddflag > 0) {
	for (CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) 
	  frame_parms[CC_id]->frame_type = TDD;
      }
      
      if (n_rb_dl !=0) {
        printf("NB_RB set to %d\n",n_rb_dl);
        if ( n_rb_dl < 6 ) {
          n_rb_dl = 6;
          printf ( "%i: Invalid number of ressource blocks, adjusted to 6\n",n_rb_dl);
        }
        if ( n_rb_dl > 100 ) {
          n_rb_dl = 100;
          printf ( "%i: Invalid number of ressource blocks, adjusted to 100\n",n_rb_dl);
        }
        if ( n_rb_dl > 50 && n_rb_dl < 100 ) {
          n_rb_dl = 50;
          printf ( "%i: Invalid number of ressource blocks, adjusted to 50\n",n_rb_dl);
        }
        if ( n_rb_dl > 25 && n_rb_dl < 50 ) {
          n_rb_dl = 25;
          printf ( "%i: Invalid number of ressource blocks, adjusted to 25\n",n_rb_dl);
        }
        UE_scan = 0;
        for (CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
          frame_parms[CC_id]->N_RB_DL=n_rb_dl;
          frame_parms[CC_id]->N_RB_UL=n_rb_dl;
        }
      }
      
      
      for (CC_id=1;CC_id<MAX_NUM_CCs;CC_id++) {
  	    tx_max_power[CC_id]=tx_max_power[0];
	    rx_gain[0][CC_id] = rx_gain[0][0];
	    tx_gain[0][CC_id] = tx_gain[0][0];
      }
      printf("Emulation interface : %s (%p,%p)\n",emul_iface,emul_iface,&emul_iface);
  } /* UE_flag > 0 */
#if T_TRACER
  paramdef_t cmdline_ttraceparams[] =CMDLINE_TTRACEPARAMS_DESC ;
  config_process_cmdline( cmdline_ttraceparams,sizeof(cmdline_ttraceparams)/sizeof(paramdef_t),NULL);
#endif

  if ( !(CONFIG_ISFLAGSET(CONFIG_ABORT)) ) {
    if (UE_flag == 0) {
      memset((void*)&RC,0,sizeof(RC));
      /* Read RC configuration file */
      RCConfig();
      NB_eNB_INST = RC.nb_inst;
      NB_RU	  = RC.nb_RU;
      printf("Configuration: nb_rrc_inst %d, nb_L1_inst %d, nb_ru %d\n",NB_eNB_INST,RC.nb_L1_inst,NB_RU);
    }
  } else if (UE_flag == 1 && (CONFIG_GETCONFFILE != NULL)) {
    // Here the configuration file is the XER encoded UE capabilities
    // Read it in and store in asn1c data structures
    strcpy(uecap_xer,CONFIG_GETCONFFILE);
    uecap_xer_in=0;
    printf("Panos: get_options 7 \n");
  } /* UE with config file  */
}


#if T_TRACER
int T_nowait = 0;     /* by default we wait for the tracer */
int T_port = 2021;    /* default port to listen to to wait for the tracer */
int T_dont_fork = 0;  /* default is to fork, see 'T_init' to understand */
#endif



void init_openair0(void);

void init_openair0() {

  int card;
  int i;

  for (card=0; card<MAX_CARDS; card++) {

    openair0_cfg[card].mmapped_dma=mmapped_dma;
    openair0_cfg[card].configFilename = NULL;

    if(frame_parms[0]->N_RB_DL == 100) {
      if (frame_parms[0]->threequarter_fs) {
	openair0_cfg[card].sample_rate=23.04e6;
	openair0_cfg[card].samples_per_frame = 230400;
	openair0_cfg[card].tx_bw = 10e6;
	openair0_cfg[card].rx_bw = 10e6;
      } else {
	openair0_cfg[card].sample_rate=30.72e6;
	openair0_cfg[card].samples_per_frame = 307200;
	openair0_cfg[card].tx_bw = 10e6;
	openair0_cfg[card].rx_bw = 10e6;
      }
    } else if(frame_parms[0]->N_RB_DL == 50) {
      openair0_cfg[card].sample_rate=15.36e6;
      openair0_cfg[card].samples_per_frame = 153600;
      openair0_cfg[card].tx_bw = 5e6;
      openair0_cfg[card].rx_bw = 5e6;
    } else if (frame_parms[0]->N_RB_DL == 25) {
      openair0_cfg[card].sample_rate=7.68e6;
      openair0_cfg[card].samples_per_frame = 76800;
      openair0_cfg[card].tx_bw = 2.5e6;
      openair0_cfg[card].rx_bw = 2.5e6;
    } else if (frame_parms[0]->N_RB_DL == 6) {
      openair0_cfg[card].sample_rate=1.92e6;
      openair0_cfg[card].samples_per_frame = 19200;
      openair0_cfg[card].tx_bw = 1.5e6;
      openair0_cfg[card].rx_bw = 1.5e6;
    }




    if (frame_parms[0]->frame_type==TDD)
      openair0_cfg[card].duplex_mode = duplex_mode_TDD;
    else //FDD
      openair0_cfg[card].duplex_mode = duplex_mode_FDD;

    printf("HW: Configuring card %d, nb_antennas_tx/rx %d/%d\n",card,
	   ((UE_flag==0) ? RC.eNB[0][0]->frame_parms.nb_antennas_tx : PHY_vars_UE_g[0][0]->frame_parms.nb_antennas_tx),
	   ((UE_flag==0) ? RC.eNB[0][0]->frame_parms.nb_antennas_rx : PHY_vars_UE_g[0][0]->frame_parms.nb_antennas_rx));
    openair0_cfg[card].Mod_id = 0;

    openair0_cfg[card].num_rb_dl=frame_parms[0]->N_RB_DL;

    openair0_cfg[card].clock_source = clock_source;


    openair0_cfg[card].tx_num_channels=min(2,((UE_flag==0) ? RC.eNB[0][0]->frame_parms.nb_antennas_tx : PHY_vars_UE_g[0][0]->frame_parms.nb_antennas_tx));
    openair0_cfg[card].rx_num_channels=min(2,((UE_flag==0) ? RC.eNB[0][0]->frame_parms.nb_antennas_rx : PHY_vars_UE_g[0][0]->frame_parms.nb_antennas_rx));

    for (i=0; i<4; i++) {

      if (i<openair0_cfg[card].tx_num_channels)
	openair0_cfg[card].tx_freq[i] = (UE_flag==0) ? downlink_frequency[0][i] : downlink_frequency[0][i]+uplink_frequency_offset[0][i];
      else
	openair0_cfg[card].tx_freq[i]=0.0;

      if (i<openair0_cfg[card].rx_num_channels)
	openair0_cfg[card].rx_freq[i] = (UE_flag==0) ? downlink_frequency[0][i] + uplink_frequency_offset[0][i] : downlink_frequency[0][i];
      else
	openair0_cfg[card].rx_freq[i]=0.0;

      openair0_cfg[card].autocal[i] = 1;
      openair0_cfg[card].tx_gain[i] = tx_gain[0][i];
      if (UE_flag == 0) {
	openair0_cfg[card].rx_gain[i] = RC.eNB[0][0]->rx_total_gain_dB;
      }
      else {
	openair0_cfg[card].rx_gain[i] = PHY_vars_UE_g[0][0]->rx_total_gain_dB - rx_gain_off;
      }

      openair0_cfg[card].configFilename = rf_config_file;
      printf("Card %d, channel %d, Setting tx_gain %f, rx_gain %f, tx_freq %f, rx_freq %f\n",
	     card,i, openair0_cfg[card].tx_gain[i],
	     openair0_cfg[card].rx_gain[i],
	     openair0_cfg[card].tx_freq[i],
	     openair0_cfg[card].rx_freq[i]);
    }
  }
}


void wait_RUs(void) {

  LOG_I(PHY,"Waiting for RUs to be configured ... RC.ru_mask:%02lx\n", RC.ru_mask);

  // wait for all RUs to be configured over fronthaul
  pthread_mutex_lock(&RC.ru_mutex);



  while (RC.ru_mask>0) {
    pthread_cond_wait(&RC.ru_cond,&RC.ru_mutex);
    printf("RC.ru_mask:%02lx\n", RC.ru_mask);
  }

  LOG_I(PHY,"RUs configured\n");
}

void wait_eNBs(void) {

  int i,j;
  int waiting=1;


  while (waiting==1) {
    printf("Waiting for eNB L1 instances to all get configured ... sleeping 500ms (nb_L1_inst %d)\n",RC.nb_L1_inst);
    usleep(5000000);
    waiting=0;
    for (i=0;i<RC.nb_L1_inst;i++) {

      printf("RC.nb_L1_CC[%d]:%d\n", i, RC.nb_L1_CC[i]);

      for (j=0;j<RC.nb_L1_CC[i];j++) {
	if (RC.eNB[i][j]->configured==0) {
	  waiting=1;
	  break;
        }
      }
    }
  }
  printf("eNB L1 are configured\n");
}

static inline void wait_nfapi_init(char *thread_name) {

  printf( "waiting for NFAPI PNF connection and population of global structure (%s)\n",thread_name);
  pthread_mutex_lock( &nfapi_sync_mutex );

  while (nfapi_sync_var<0)
    pthread_cond_wait( &nfapi_sync_cond, &nfapi_sync_mutex );

  pthread_mutex_unlock(&nfapi_sync_mutex);

  printf( "NFAPI: got sync (%s)\n", thread_name);
}

int main( int argc, char **argv )
{
  int i;
#if defined (XFORMS)
  void *status;
#endif

  int CC_id;
  int ru_id;
  uint8_t  abstraction_flag=0;
  uint8_t beta_ACK=0,beta_RI=0,beta_CQI=2;

#if defined (XFORMS)
  int ret;
#endif

  start_background_system();
  if ( load_configmodule(argc,argv) == NULL) {
    exit_fun("[SOFTMODEM] Error, configuration module init failed\n");
  }

#ifdef DEBUG_CONSOLE
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#endif

  PHY_VARS_UE *UE[MAX_NUM_CCs];

  mode = normal_txrx;

  //Panos: RF-sepcific part should get dropped (next 3 lines)?
  //memset(&openair0_cfg[0],0,sizeof(openair0_config_t)*MAX_CARDS);

  //memset(tx_max_power,0,sizeof(int)*MAX_NUM_CCs);

  //set_latency_target();

  // set default parameters
  //if (UE_flag == 1) set_default_frame_parms(frame_parms);

  logInit();

  printf("Reading in command-line options\n");

  get_options ();
  if (CONFIG_ISFLAGSET(CONFIG_ABORT)) {
    if (UE_flag == 0) {
      fprintf(stderr,"Getting configuration failed\n");
      exit(-1);
    }
    else { 
      printf("Setting nfapi mode to UE_STUB_OFFNET\n");
      nfapi_mode = 4;
    }
  }
    

#if T_TRACER
  T_init(T_port, 1-T_nowait, T_dont_fork);
#endif



  //randominit (0);
  set_taus_seed (0);

  if (UE_flag==1) {
    printf("configuring for UE\n");

    set_comp_log(HW,      LOG_INFO,  LOG_HIGH, 1);
    set_comp_log(PHY,     LOG_INFO,   LOG_HIGH, 1);
    set_comp_log(MAC,     LOG_INFO,   LOG_HIGH, 1);
    set_comp_log(RLC,     LOG_INFO,   LOG_HIGH | FLAG_THREAD, 1);
    set_comp_log(PDCP,    LOG_DEBUG,   LOG_HIGH, 1);
    set_comp_log(OTG,     LOG_INFO,   LOG_HIGH, 1);
    set_comp_log(RRC,     LOG_INFO,   LOG_HIGH, 1);
#if defined(ENABLE_ITTI)
    set_comp_log(EMU,     LOG_INFO,   LOG_MED, 1);
# if defined(ENABLE_USE_MME)
    set_comp_log(NAS,     LOG_INFO,   LOG_HIGH, 1);
# endif
#endif

  } else {
    printf("configuring for RAU/RRU\n");

  }

  if (ouput_vcd) {
    if (UE_flag==1)
      VCD_SIGNAL_DUMPER_INIT("/tmp/openair_dump_UE.vcd");
    else
      VCD_SIGNAL_DUMPER_INIT("/tmp/openair_dump_eNB.vcd");
  }

  // Panos: Not required for phy_stub_UE mode
  /*if (opp_enabled ==1) {
    reset_opp_meas();
  }
  cpuf=get_cpu_freq_GHz();*/

#if defined(ENABLE_ITTI)

  if (UE_flag == 1) {
    log_set_instance_type (LOG_INSTANCE_UE);
  } else {
    log_set_instance_type (LOG_INSTANCE_ENB);
  }

  printf("ITTI init\n");
  itti_init(TASK_MAX, THREAD_MAX, MESSAGES_ID_MAX, tasks_info, messages_info, messages_definition_xml, itti_dump_file);

  // initialize mscgen log after ITTI
  MSC_INIT(MSC_E_UTRAN, THREAD_MAX+TASK_MAX);
#endif

  if (opt_type != OPT_NONE) {
    radio_type_t radio_type;

    if (frame_parms[0]->frame_type == FDD)
      radio_type = RADIO_TYPE_FDD;
    else
      radio_type = RADIO_TYPE_TDD;

    if (init_opt(in_path, in_ip, NULL, radio_type) == -1)
      LOG_E(OPT,"failed to run OPT \n");
  }

#ifdef PDCP_USE_NETLINK
  printf("PDCP netlink\n");
  netlink_init();
#if defined(PDCP_USE_NETLINK_QUEUES)
  pdcp_netlink_init();
#endif
#endif

//TTN for D2D
#ifdef Rel14
  printf ("RRC control socket\n");
  rrc_control_socket_init();
  printf ("PDCP PC5S socket\n");
  pdcp_pc5_socket_init();
#endif


#if !defined(ENABLE_ITTI)
  // to make a graceful exit when ctrl-c is pressed
  signal(SIGSEGV, signal_handler);
  signal(SIGINT, signal_handler);
#endif


  check_clock();

#ifndef PACKAGE_VERSION
#  define PACKAGE_VERSION "UNKNOWN-EXPERIMENTAL"
#endif

  LOG_I(HW, "Version: %s\n", PACKAGE_VERSION);

  // init the parameters
  // Panos: Probably don't need next lines for the stub.

  /*for (CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {

    if (UE_flag==1) {
      frame_parms[CC_id]->nb_antennas_tx     = nb_antenna_tx;
      frame_parms[CC_id]->nb_antennas_rx     = nb_antenna_rx;
      frame_parms[CC_id]->nb_antenna_ports_eNB = 1; //initial value overwritten by initial sync later
    }
  }*/



  printf("Before CC \n");

  for (CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {


    if (UE_flag==1) {
      NB_UE_INST=1;
      NB_INST=1;
      PHY_vars_UE_g = malloc(sizeof(PHY_VARS_UE**));
      PHY_vars_UE_g[0] = malloc(sizeof(PHY_VARS_UE*)*MAX_NUM_CCs);



      PHY_vars_UE_g[0][CC_id] = init_ue_vars(frame_parms[CC_id], 0,abstraction_flag);
      UE[CC_id] = PHY_vars_UE_g[0][CC_id];
      printf("PHY_vars_UE_g[0][%d] = %p\n",CC_id,UE[CC_id]);

      if (phy_test==1)
	UE[CC_id]->mac_enabled = 0;
      else
	UE[CC_id]->mac_enabled = 1;

      /*if (UE[CC_id]->mac_enabled == 0) {  //set default UL parameters for testing mode
	for (i=0; i<NUMBER_OF_CONNECTED_eNB_MAX; i++) {
	  UE[CC_id]->pusch_config_dedicated[i].betaOffset_ACK_Index = beta_ACK;
	  UE[CC_id]->pusch_config_dedicated[i].betaOffset_RI_Index  = beta_RI;
	  UE[CC_id]->pusch_config_dedicated[i].betaOffset_CQI_Index = beta_CQI;

	  UE[CC_id]->scheduling_request_config[i].sr_PUCCH_ResourceIndex = 0;
	  UE[CC_id]->scheduling_request_config[i].sr_ConfigIndex = 7+(0%3);
	  UE[CC_id]->scheduling_request_config[i].dsr_TransMax = sr_n4;
	}
      }

      UE[CC_id]->UE_scan = UE_scan;
      UE[CC_id]->UE_scan_carrier = UE_scan_carrier;
      UE[CC_id]->mode    = mode;
      printf("UE[%d]->mode = %d\n",CC_id,mode);

      if (UE[CC_id]->mac_enabled == 1) {
	UE[CC_id]->pdcch_vars[0][0]->crnti = 0x1234;
	UE[CC_id]->pdcch_vars[1][0]->crnti = 0x1234;
      }else {
	UE[CC_id]->pdcch_vars[0][0]->crnti = 0x1235;
	UE[CC_id]->pdcch_vars[1][0]->crnti = 0x1235;
      }
      UE[CC_id]->rx_total_gain_dB =  (int)rx_gain[CC_id][0] + rx_gain_off;
      UE[CC_id]->tx_power_max_dBm = tx_max_power[CC_id];

      if (frame_parms[CC_id]->frame_type==FDD) {
	UE[CC_id]->N_TA_offset = 0;
      }
      else {
	if (frame_parms[CC_id]->N_RB_DL == 100)
	  UE[CC_id]->N_TA_offset = 624;
	else if (frame_parms[CC_id]->N_RB_DL == 50)
	  UE[CC_id]->N_TA_offset = 624/2;
	else if (frame_parms[CC_id]->N_RB_DL == 25)
	  UE[CC_id]->N_TA_offset = 624/4;
      }*/

    }
  }

  // Panos: Probably don't need these lines for phy_stub
  /*printf("Runtime table\n");
  fill_modeled_runtime_table(runtime_phy_rx,runtime_phy_tx);
  cpuf=get_cpu_freq_GHz();*/



#ifndef DEADLINE_SCHEDULER

  printf("NO deadline scheduler\n");
  /* Currently we set affinity for UHD to CPU 0 for eNB/UE and only if number of CPUS >2 */

  cpu_set_t cpuset;
  int s;
  char cpu_affinity[1024];
  CPU_ZERO(&cpuset);
#ifdef CPU_AFFINITY
  if (get_nprocs() > 2) {
    CPU_SET(0, &cpuset);
    s = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (s != 0) {
      perror( "pthread_setaffinity_np");
      exit_fun("Error setting processor affinity");
    }
    LOG_I(HW, "Setting the affinity of main function to CPU 0, for device library to use CPU 0 only!\n");
  }
#endif

  /* Check the actual affinity mask assigned to the thread */
  s = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror( "pthread_getaffinity_np");
    exit_fun("Error getting processor affinity ");
  }
  memset(cpu_affinity, 0 , sizeof(cpu_affinity));
  for (int j = 0; j < CPU_SETSIZE; j++) {
    if (CPU_ISSET(j, &cpuset)) {
      char temp[1024];
      sprintf(temp, " CPU_%d ", j);
      strcat(cpu_affinity, temp);
    }
  }
  LOG_I(HW, "CPU Affinity of main() function is... %s\n", cpu_affinity);
#endif




#if defined(ENABLE_ITTI)

  printf("ITTI enabled\n");

  printf("UE_flag:%d\n", UE_flag);
  printf("RC.nb_inst:%d\n", RC.nb_inst);
  /*if ((UE_flag == 1)||
      (RC.nb_inst > 0))  {*/

  if (UE_flag == 1) {
    // don't create if node doesn't connect to RRC/S1/GTP
    //if (create_tasks(UE_flag ? 0 : 1, UE_flag ? 1 : 0) < 0) {
	  if (create_tasks_ue(1) < 0) {
      printf("cannot create ITTI tasks\n");
      exit(-1); // need a softer mode
    }
    if(nfapi_mode==3){ //Panos: Here we should add another nfapi_mode for the case of Supervised LTE-D2D
        UE_config_stub_pnf();
    }
    printf("ITTI tasks created\n");
  }
  else {
    printf("No ITTI, Initializing L1\n");
    RCconfig_L1();
  }
#endif

  /*if (phy_test==0) {
    if (UE_flag==1) {
      printf("Filling UE band info\n");
      fill_ue_band_info();
      dl_phy_sync_success (0, 0, 0, 1);
    }
  }*/

  mlockall(MCL_CURRENT | MCL_FUTURE);

  // Panos: Why we have these initializations twice if in nfapi_mode?
  pthread_cond_init(&sync_cond,NULL);
  pthread_mutex_init(&sync_mutex, NULL);

#ifdef XFORMS
  int UE_id;

  printf("XFORMS\n");

  if (do_forms==1) {
    fl_initialize (&argc, argv, NULL, 0, 0);

    if (UE_flag==0) {
      form_stats_l2 = create_form_stats_form();
      fl_show_form (form_stats_l2->stats_form, FL_PLACE_HOTSPOT, FL_FULLBORDER, "l2 stats");
      form_stats = create_form_stats_form();
      fl_show_form (form_stats->stats_form, FL_PLACE_HOTSPOT, FL_FULLBORDER, "stats");

      for(UE_id=0; UE_id<scope_enb_num_ue; UE_id++) {
	for(CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
	  form_enb[CC_id][UE_id] = create_lte_phy_scope_enb();
	  sprintf (title, "LTE UL SCOPE eNB for CC_id %d, UE %d",CC_id,UE_id);
	  fl_show_form (form_enb[CC_id][UE_id]->lte_phy_scope_enb, FL_PLACE_HOTSPOT, FL_FULLBORDER, title);

	  if (otg_enabled) {
	    fl_set_button(form_enb[CC_id][UE_id]->button_0,1);
	    fl_set_object_label(form_enb[CC_id][UE_id]->button_0,"DL Traffic ON");
	  } else {
	    fl_set_button(form_enb[CC_id][UE_id]->button_0,0);
	    fl_set_object_label(form_enb[CC_id][UE_id]->button_0,"DL Traffic OFF");
	  }
	} // CC_id
      } // UE_id
    } else {
      form_stats = create_form_stats_form();
      fl_show_form (form_stats->stats_form, FL_PLACE_HOTSPOT, FL_FULLBORDER, "stats");
      UE_id = 0;
      form_ue[UE_id] = create_lte_phy_scope_ue();
      sprintf (title, "LTE DL SCOPE UE");
      fl_show_form (form_ue[UE_id]->lte_phy_scope_ue, FL_PLACE_HOTSPOT, FL_FULLBORDER, title);

      /*
	if (openair_daq_vars.use_ia_receiver) {
	fl_set_button(form_ue[UE_id]->button_0,1);
	fl_set_object_label(form_ue[UE_id]->button_0, "IA Receiver ON");
	} else {
	fl_set_button(form_ue[UE_id]->button_0,0);
	fl_set_object_label(form_ue[UE_id]->button_0, "IA Receiver OFF");
	}*/
      fl_set_button(form_ue[UE_id]->button_0,0);
      fl_set_object_label(form_ue[UE_id]->button_0, "IA Receiver OFF");
    }

    ret = pthread_create(&forms_thread, NULL, scope_thread, NULL);

    if (ret == 0)
      pthread_setname_np( forms_thread, "xforms" );

    printf("Scope thread created, ret=%d\n",ret);
  }

#endif

  rt_sleep_ns(10*100000000ULL);

  // Panos: I have commented these lines because cond_init and mutex_init take place above in any case.
  /*if (nfapi_mode)
  {
    printf("NFAPI*** - mutex and cond created - will block shortly for completion of PNF connection\n");
    pthread_cond_init(&sync_cond,NULL);
    pthread_mutex_init(&sync_mutex, NULL);
  }*/

  const char *nfapi_mode_str = "<UNKNOWN>";

  switch(nfapi_mode)
  {
    case 0:
      nfapi_mode_str = "MONOLITHIC";
      break;
    case 1:
      nfapi_mode_str = "PNF";
      break;
    case 2:
      nfapi_mode_str = "VNF";
      break;
    case 3:
      nfapi_mode_str = "UE_STUB_PNF";
      break;
    case 4:
      nfapi_mode_str = "UE_STUB_OFFNET";
      break;
    default:
      nfapi_mode_str = "<UNKNOWN NFAPI MODE>";
      break;
  }
  printf("NFAPI MODE:%s\n", nfapi_mode_str);


  if (nfapi_mode<3) // VNF
    wait_nfapi_init("main?");

  printf("START MAIN THREADS, nfapi_mode:%d \n", nfapi_mode);

  // start the main threads
  if (UE_flag == 1) {
    int eMBMS_active = 0;
    //init_UE(1,eMBMS_active,uecap_xer_in);
    number_of_cards = 1;

    config_sync_var=0;
    if (nfapi_mode==3) // UE-STUB-PNF
    	{
    	wait_nfapi_init("main?");
    	}
    // Panos: Call init_UE_stub instead of init_UE as we are always on nfapi_mode=3
    //phy_stub_ticking = (SF_ticking*)malloc(sizeof(SF_ticking));


    // Panos: CHANGE we call init_timer_thread() from inside init_UE_stub() now
    //init_timer_thread();
    init_UE_stub(1,eMBMS_active,uecap_xer_in,emul_iface);


    /*for(CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
      PHY_vars_UE_g[0][CC_id]->rf_map.card=0;
      PHY_vars_UE_g[0][CC_id]->rf_map.chain=CC_id+chain_offset;
    }*/
  }
  // Panos: We should never enter this else case for nfapi_mode=3.
  /*else {
    number_of_cards = 1;
    printf("RC.nb_L1_inst:%d\n", RC.nb_L1_inst);
    if (RC.nb_L1_inst > 0) {
      printf("Initializing eNB threads single_thread_flag:%d wait_for_sync:%d\n", single_thread_flag,wait_for_sync);
      init_eNB(single_thread_flag,wait_for_sync);
      //      for (inst=0;inst<RC.nb_L1_inst;inst++)
      //	for (CC_id=0;CC_id<RC.nb_L1_CC[inst];CC_id++) phy_init_lte_eNB(RC.eNB[inst][CC_id],0,0);
    }

    printf("wait_eNBs()\n");
    wait_eNBs();

    printf("About to Init RU threads RC.nb_RU:%d\n", RC.nb_RU);
    if (RC.nb_RU >0) {
      printf("Initializing RU threads\n");
      init_RU(rf_config_file);
      for (ru_id=0;ru_id<RC.nb_RU;ru_id++) {
	RC.ru[ru_id]->rf_map.card=0;
	RC.ru[ru_id]->rf_map.chain=CC_id+chain_offset;
      }
    }

    config_sync_var=0;

    if (nfapi_mode==1) // PNF
    {
      wait_nfapi_init("main?");
    }

    printf("wait RUs\n");
    wait_RUs();
    printf("ALL RUs READY!\n");
    printf("RC.nb_RU:%d\n", RC.nb_RU);
    // once all RUs are ready intiailize the rest of the eNBs ((dependence on final RU parameters after configuration)
    printf("ALL RUs ready - init eNBs\n");

    if (nfapi_mode != 1 && nfapi_mode != 2)
    {
      printf("Not NFAPI mode - call init_eNB_afterRU()\n");
      init_eNB_afterRU();
    }
    else
    {
      printf("NFAPI mode - DO NOT call init_eNB_afterRU()\n");
    }

    printf("ALL RUs ready - ALL eNBs ready\n");
  }*/
  if (phy_test==0) {
      if (UE_flag==1) {
        printf("Filling UE band info\n");
        fill_ue_band_info();
        dl_phy_sync_success (0, 0, 0, 1);
      }
    }


  // connect the TX/RX buffers
  if (UE_flag==1) {

    for (CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {


#ifdef OAI_USRP
      UE[CC_id]->hw_timing_advance = timing_advance;
#else
      UE[CC_id]->hw_timing_advance = 160;
#endif
    }
    // Panos: No need to call setup_ue_buffers() for nfapi_mode=3
    /*if (setup_ue_buffers(UE,&openair0_cfg[0])!=0) {
      printf("Error setting up eNB buffer\n");
      exit(-1);
    }*/


    // Panos: Similar for nfapi_mode=3
    /*if (input_fd) {
      printf("Reading in from file to antenna buffer %d\n",0);
      if (fread(UE[0]->common_vars.rxdata[0],
		sizeof(int32_t),
		frame_parms[0]->samples_per_tti*10,
		input_fd) != frame_parms[0]->samples_per_tti*10)
	printf("error reading from file\n");
    }*/
    //p_exmimo_config->framing.tdd_config = TXRXSWITCH_TESTRX;
  } else {

    printf("eNB mode\n");

  }

  printf("Sending sync to all threads\n");

  pthread_mutex_lock(&sync_mutex);
  sync_var=0;
  pthread_cond_broadcast(&sync_cond);
  pthread_mutex_unlock(&sync_mutex);
  printf("About to call end_configmodule() from %s() %s:%d\n", __FUNCTION__, __FILE__, __LINE__);
  end_configmodule();
  printf("Called end_configmodule() from %s() %s:%d\n", __FUNCTION__, __FILE__, __LINE__);

  // wait for end of program
  printf("TYPE <CTRL-C> TO TERMINATE\n");
  //getchar();

#if defined(ENABLE_ITTI)
  printf("Entering ITTI signals handler\n");
  itti_wait_tasks_end();
  printf("Returned from ITTI signal handler\n");
  oai_exit=1;
  printf("oai_exit=%d\n",oai_exit);
#else

  while (oai_exit==0)
    rt_sleep_ns(100000000ULL);
  printf("Terminating application - oai_exit=%d\n",oai_exit);

#endif

  // stop threads
#ifdef XFORMS
  printf("waiting for XFORMS thread\n");

  if (do_forms==1) {
    pthread_join(forms_thread,&status);
    fl_hide_form(form_stats->stats_form);
    fl_free_form(form_stats->stats_form);

    if (UE_flag==1) {
      fl_hide_form(form_ue[0]->lte_phy_scope_ue);
      fl_free_form(form_ue[0]->lte_phy_scope_ue);
    } else {
      fl_hide_form(form_stats_l2->stats_form);
      fl_free_form(form_stats_l2->stats_form);

      for(UE_id=0; UE_id<scope_enb_num_ue; UE_id++) {
	for(CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
	  fl_hide_form(form_enb[CC_id][UE_id]->lte_phy_scope_enb);
	  fl_free_form(form_enb[CC_id][UE_id]->lte_phy_scope_enb);
	}
      }
    }
  }

#endif

  printf("stopping MODEM threads\n");

  // cleanup
  if (UE_flag == 1) {
  } /*else {
    stop_eNB(1);
  }*/


  pthread_cond_destroy(&sync_cond);
  pthread_mutex_destroy(&sync_mutex);

  pthread_cond_destroy(&nfapi_sync_cond);
  pthread_mutex_destroy(&nfapi_sync_mutex);


  // Panos: No RF in phy_stub mode: Exclude the following lines
  // *** Handle per CC_id openair0
  /*if (UE_flag==1) {
    if (PHY_vars_UE_g[0][0]->rfdevice.trx_end_func)
      PHY_vars_UE_g[0][0]->rfdevice.trx_end_func(&PHY_vars_UE_g[0][0]->rfdevice);
  }
  else {
    for(ru_id=0; ru_id<NB_RU; ru_id++) {
      if (RC.ru[ru_id]->rfdevice.trx_end_func)
	RC.ru[ru_id]->rfdevice.trx_end_func(&RC.ru[ru_id]->rfdevice);
      if (RC.ru[ru_id]->ifdevice.trx_end_func)
	RC.ru[ru_id]->ifdevice.trx_end_func(&RC.ru[ru_id]->ifdevice);

    }
  }*/
  if (ouput_vcd)
    VCD_SIGNAL_DUMPER_CLOSE();

  if (opt_enabled == 1)
    terminate_opt();

  logClean();

  return 0;
}