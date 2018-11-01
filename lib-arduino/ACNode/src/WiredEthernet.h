#pragma once
#include <ACNode-private.h>

// Be sure to define these prior to any inclusion
// of ETH.h or anything that may include ETH.h - as
// the latter will otherwise define these values itself.
// Upon which below is ignored and will give you an odd 
// silent hang.

#define ETH_PHY_ADDR      (1)
#define ETH_PHY_MDC       (23)
#define ETH_PHY_MDIO      (18)
#define ETH_PHY_POWER     (17)
#define ETH_PHY_TYPE      (ETH_PHY_LAN8720)

#include <ETH.h>

extern bool eth_connected();
extern void eth_setup();
