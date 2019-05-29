#include "rlc_entity.h"

#include <stdio.h>

void deliver_sdu(void *deliver_sdu_data, struct rlc_entity_t *entity,
                 char *buf, int size)
{
  printf("got SDU size %d\n", size);
  for (int i = 0; i < size; i++) printf(" %2.2x", (unsigned char)buf[i]);
  printf("\n");
}

void successful_delivery(void *successful_delivery_data, rlc_entity_t *entity,
                         int sdu_id)
{
  printf("SDU %d was successfully delivered.\n", sdu_id);
}

void max_retx_reached(void *max_retx_reached_data, rlc_entity_t *entity)
{
  printf("max RETX reached! radio link failure!\n");
}

int main(void)
{
  char x[] = { 0x88, 0x00, 0x00, 0x22, 0x20, 0x80, 0x00, 0x01, 0x3a, 0x17, 0xce, 0xf6, 0x6c, 0xc0, 0x07, 0x07, 0x41, 0x02, 0x0b, 0xf6, 0x02, 0xf8, 0x29, 0x80, 0x00, 0x01, 0x00, 0x00, 0x00, 0xa3, 0x05, 0xf0, 0xf0, 0xc0, 0xc0, 0x0c, 0x00, 0x05, 0x02, 0x76, 0xd0, 0x11, 0xd1, 0x52, 0x02, 0xf8, 0x29, 0x00, 0x01, 0x5c, 0x08, 0x02, 0x31, 0x03, 0xf5, 0xe0, 0x34, 0x90 };
  char y[] = { 0xb0, 0x01, 0x11, 0x03, 0x57, 0x58, 0xb2, 0x5d, 0x01, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00 };

  rlc_entity_t *am;

  am = new_rlc_entity_am(100000, 100000,
                         deliver_sdu, (void*)0,
                         successful_delivery, (void*)0,
                         max_retx_reached, (void*)0,
                         35, 0, 45, -1, -1, 4);

  am->recv_pdu(am, x, sizeof(x));
  am->recv_pdu(am, y, sizeof(y));

  return 0;
}