/*
 *  code to generate fake control PDU:
 *  rlc_pdu_encoder_init(&e, out, 100);
 *  rlc_pdu_encoder_put_bits(&e, 0, 1);    // D/C
 *  rlc_pdu_encoder_put_bits(&e, 0, 3);    // CPT
 *  rlc_pdu_encoder_put_bits(&e, 0, 10);   // ack_sn
 *  rlc_pdu_encoder_put_bits(&e, 1, 1);    // e1
 *  rlc_pdu_encoder_put_bits(&e, 10, 10);  // nack_sn
 *  rlc_pdu_encoder_put_bits(&e, 0, 1);    // e1
 *  rlc_pdu_encoder_put_bits(&e, 0, 1);    // e2
 *  rlc_pdu_encoder_align(&e);
 */

TIME, 1,
    ENB_AM, 100000, 100000, 35, 0, 45, -1, -1, 4,
    UE_AM, 100000, 100000, 35, 0, 45, -1, -1, 4,
    ENB_SDU, 0, 30,
    ENB_RECV_FAILS, 1,
TIME, 20,
    ENB_PDU_SIZE, 14,
TIME, 60,
    ENB_RECV_FAILS, 0,
    UE_PDU, 4, 0x00, 0x0a, 0x05, 0x00,
TIME, 70,
    UE_RECV_FAILS, 0,
TIME, -1

