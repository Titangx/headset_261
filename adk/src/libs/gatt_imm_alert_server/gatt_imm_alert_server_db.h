/*
 * THIS FILE IS AUTOGENERATED, DO NOT EDIT!
 *
 * generated by gattdbgen from gatt_imm_alert_server/gatt_imm_alert_server_db.dbi_
 */
#ifndef __GATT_IMM_ALERT_SERVER_DB_H
#define __GATT_IMM_ALERT_SERVER_DB_H

#include <csrtypes.h>

#define HANDLE_IMM_ALERT_SERVICE        (0x0001)
#define HANDLE_IMM_ALERT_SERVICE_END    (0xffff)
#define HANDLE_IMM_ALERT_LEVEL          (0x0003)

uint16 *GattGetDatabase(uint16 *len);
uint16 GattGetDatabaseSize(void);
const uint16 *GattGetConstDatabase(uint16 *len);

#endif

/* End-of-File */