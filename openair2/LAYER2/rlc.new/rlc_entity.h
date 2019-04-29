#ifndef _RLC_ENTITY_H_
#define _RLC_ENTITY_H_

#include <stdint.h>

typedef struct {
  int status_size;
  int tx_size;
  int retx_size;
} rlc_entity_buffer_status_t;

typedef struct rlc_entity_t {
  /* functions provided by the RLC module */
  void (*recv_pdu)(struct rlc_entity_t *entity, char *buffer, int size);
  rlc_entity_buffer_status_t (*buffer_status)(
      struct rlc_entity_t *entity, int maxsize);
  int (*generate_pdu)(struct rlc_entity_t *entity, char *buffer, int size);

  void (*recv_sdu)(struct rlc_entity_t *entity, char *buffer, int size);

  /* callbacks provided to the RLC module */
  void (*deliver_sdu)(void *deliver_sdu_data, struct rlc_entity_t *entity,
                      char *buf, int size);
  void *deliver_sdu_data;

  /* set to the latest know time. Unit: ms */
  uint64_t t_current;
} rlc_entity_t;

rlc_entity_t *new_rlc_entity_am(
    int rx_maxsize,
    int tx_maxsize,
    void (*deliver_sdu)(void *deliver_sdu_data, struct rlc_entity_t *entity,
                      char *buf, int size),
    void *deliver_sdu_data,
    int t_reordering,
    int t_status_prohibit,
    int t_poll_retransmit,
    int poll_pdu,
    int poll_byte,
    int max_retx_threshold);

#endif /* _RLC_ENTITY_H_ */
