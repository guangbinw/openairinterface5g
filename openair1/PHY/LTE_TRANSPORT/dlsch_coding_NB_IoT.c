/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.0  (the "License"); you may not use this file
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

/*! \file PHY/LTE_TRANSPORT/dlsch_coding_NB_IoT.c
* \brief Top-level routines for implementing Tail-biting convolutional coding for transport channels (NPDSCH) for NB_IoT,	 TS 36-212, V13.4.0 2017-02
* \author M. KANJ
* \date 2017
* \version 0.0
* \company bcom
* \email: matthieu.kanj@b-com.com
* \note
* \warning
*/
#include <string.h>
//#include "PHY/defs.h"
//#include "PHY/defs_NB_IoT.h"
//#include "PHY/extern_NB_IoT.h"
#include "PHY/CODING/defs_NB_IoT.h"
//#include "PHY/CODING/extern.h"
//#include "PHY/CODING/lte_interleaver_inline.h"
#include "PHY/LTE_TRANSPORT/defs_NB_IoT.h"
//#include "PHY/LTE_TRANSPORT/proto_NB_IoT.h"
//#include "SCHED/defs_NB_IoT.h"
//#include "defs_nb_iot.h"
//#include "UTIL/LOG/vcd_signal_dumper.h"
#include "PHY/TOOLS/time_meas_NB_IoT.h" 

unsigned char  ccodelte_table2_NB_IoT[128];
unsigned short glte2_NB_IoT[] = { 0133, 0171, 0165 }; 

void ccode_encode_npdsch_NB_IoT (int32_t   numbits,
								 uint8_t   *inPtr,
								 uint8_t   *outPtr,
								 uint32_t  crc)
{
	uint32_t  	state;
	uint8_t  	c, out, first_bit;
	int8_t 		shiftbit = 0;
    /* The input bit is shifted in position 8 of the state.
	Shiftbit will take values between 1 and 8 */
	state 		= 0;
	first_bit   = 2;
	c 			= ((uint8_t*)&crc)[0];
	// Perform Tail-biting
	// get bits from last byte of input (or crc)
	for (shiftbit = 0 ; shiftbit <(8-first_bit) ; shiftbit++) {
		if ((c&(1<<(7-first_bit-shiftbit))) != 0)
			state |= (1<<shiftbit);
	}
	state = state & 0x3f;   			  // true initial state of Tail-biting CCode
	state<<=1;            				  // because of loop structure in CCode
		while (numbits > 0) {											// Tail-biting is applied to input bits , input 34 bits , output 102 bits
			c = *inPtr++;
			for (shiftbit = 7; (shiftbit>=0) && (numbits>0); shiftbit--,numbits--) {
				state >>= 1;
				if ((c&(1<<shiftbit)) != 0) {
					state |= 64;
				}
				out = ccodelte_table2_NB_IoT[state];

				*outPtr++ = out  & 1;
				*outPtr++ = (out>>1)&1;
				*outPtr++ = (out>>2)&1;
			}
		}
}


int dlsch_encoding_NB_IoT(unsigned char      			*a,
						  NB_IoT_eNB_DLSCH_t 			*dlsch,
						  uint8_t 			 			Nsf,		// number of subframes required for npdsch pdu transmission calculated from Isf (3GPP spec table)
						  unsigned int 		 			G) 		    // G (number of available RE) is implicitly multiplied by 2 (since only QPSK modulation)
{
	unsigned int  crc = 1;
	//unsigned char harq_pid = dlsch->current_harq_pid;  			// to check during implementation if harq_pid is required in the NB_IoT_eNB_DLSCH_t structure  in defs_NB_IoT.h
	unsigned int  A;
	uint8_t 	  RCC;

	A 							 = dlsch->harq_process.TBS;  				// 680
	dlsch->harq_process.length_e = G*Nsf;									// G*Nsf (number_of_subframes) = total number of bits to transmit G=236

	int32_t numbits = A+24;
	
	if (dlsch->harq_process.round == 0) { 	    // This is a new packet

		crc = crc24a_NB_IoT(a,A)>>8;						// CRC calculation (24 bits CRC)
												    // CRC attachment to payload
		a[A>>3]     = ((uint8_t*)&crc)[2];
		a[1+(A>>3)] = ((uint8_t*)&crc)[1];
		a[2+(A>>3)] = ((uint8_t*)&crc)[0];
		
		dlsch->harq_process.B = numbits;			// The length of table b in bits
		
		memcpy(dlsch->harq_process.b,a,numbits/8); 
		memset(dlsch->harq_process.d,LTE_NULL_NB_IoT,96);
		
		ccode_encode_npdsch_NB_IoT(numbits, dlsch->harq_process.b, dlsch->harq_process.d+96, crc);  	//   step 1 Tail-biting convolutional coding
		
		RCC = sub_block_interleaving_cc_NB_IoT(numbits,dlsch->harq_process.d+96,dlsch->harq_process.w);		//   step 2 interleaving
		
		lte_rate_matching_cc_NB_IoT(RCC,dlsch->harq_process.length_e,dlsch->harq_process.w,dlsch->harq_process.e);  // step 3 Rate Matching
				
    }
  return(0);
}

/*************************************************************************

  Functions to initialize the code tables

*************************************************************************/
/* Basic code table initialization for constraint length 7 */

/* Input in MSB, followed by state in 6 LSBs */
void ccodelte_init2_NB_IoT(void)
{
  unsigned int  i, j, k, sum;

  for (i = 0; i < 128; i++) {

    ccodelte_table2_NB_IoT[i] = 0;

    /* Compute 3 output bits */
    for (j = 0; j < 3; j++) {
      sum = 0;

      for (k = 0; k < 7; k++)
        if ((i & glte2_NB_IoT[j]) & (1 << k))
          sum++;

      /* Write the sum modulo 2 in bit j */
      ccodelte_table2_NB_IoT[i] |= (sum & 1) << j;
    }
  }
}
