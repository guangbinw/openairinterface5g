/*
 * basic test:
 * at time 1, eNB receives an SDU of 16000 bytes
 */

TIME, 1,
    ENB, 100000, 100000, 35, 0, 45, -1, -1, 4,
    UE, 100000, 100000, 35, 0, 45, -1, -1, 4,
    ENB_SDU, 0, 16000,
TIME, -1
