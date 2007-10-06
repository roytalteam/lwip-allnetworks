/*
 * Copyright (c) 2001,2002 Florian Schulze.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the authors nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * test.c - This file is part of lwIP test
 *
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <conio.h>

#include "lwip/opt.h"

#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"

#include "lwip/tcp.h"
#include "lwip/udp.h"

#include "netif/loopif.h"
#include "netif/etharp.h"

#include "../../apps/httpserver_raw/httpd.h"
#include "../../apps/netio/netio.h"

#if NO_SYS
/* ... then we need information about the timer intervals: */
#include "lwip/ip_frag.h"
#include "lwip/igmp.h"
#include "lwip/dhcp.h"
#include "lwip/autoip.h"
#endif /* NO_SYS */

/* include the port-dependent configuration */
#include "lwipcfg_msvc.h"

/* some forward function definitions... */
err_t ethernetif_init(struct netif *netif);
void shutdown_adapter(void);
void update_adapter(void);

#if NO_SYS
/* port-defined functions used for timer execution */
void sys_init_timing();
u32_t sys_get_ms();
#endif /* NO_SYS*/

/* globales variables for netifs */
/* THE ethernet interface */
struct netif netif;
#if LWIP_HAVE_LOOPIF
/* THE loopback interface */
struct netif loop_netif;
#endif /* LWIP_HAVE_LOOPIF */


#if NO_SYS
/* special functions used for NO_SYS=1 only */
static void
nosys_init()
{
  sys_init_timing();
  lwip_init();
}

/* get the current time and see if any timer has expired */
static void
timers_update()
{
  /* static variables for timer execution, initialized to zero! */
  static int last_time,
    timerTcpFast, timerTcpSlow, timerArp,
    timerDhcpFine, timerDhcpCoarse, timerIpReass,
    timerAutoIP, timerIgmp;

  int cur_time;
  int time_diff;

  cur_time = sys_get_ms();
  time_diff = cur_time - last_time;
  /* the '> 0' is an easy wrap-around check: the big gap at
   * the wraparound step is simply ignored... */
  if (time_diff > 0) {
    last_time = cur_time;
    timerTcpFast += time_diff;
    timerTcpSlow += time_diff;
    timerArp += time_diff;
    timerDhcpFine += time_diff;
    timerDhcpCoarse += time_diff;
    timerIpReass += time_diff;
    timerAutoIP += time_diff;
    timerIgmp += time_diff;
  }
#if LWIP_TCP
  /* execute TCP fast timer every 250 ms */
  if (timerTcpFast > TCP_TMR_INTERVAL) {
    tcp_fasttmr();
    timerTcpFast -= TCP_TMR_INTERVAL;
  }
  /* execute TCP slow timer every 500 ms */
  if (timerTcpSlow > ((TCP_TMR_INTERVAL)*2)) {
    tcp_slowtmr();
    timerTcpSlow -= (TCP_TMR_INTERVAL)*2;
  }
#endif /* LWIP_TCP */
#if LWIP_ARP
  /* execute ARP timer */
  if (timerArp > ARP_TMR_INTERVAL) {
    etharp_tmr();
    timerArp -= ARP_TMR_INTERVAL;
  }
#endif /* LWIP_ARP */
#if LWIP_DHCP
  /* execute DHCP fine timer */
  if (timerDhcpFine > DHCP_FINE_TIMER_MSECS) {
    dhcp_fine_tmr();
    timerDhcpFine -= DHCP_FINE_TIMER_MSECS;
  }
  /* execute DHCP coarse timer */
  if (timerDhcpCoarse > ((DHCP_COARSE_TIMER_SECS)*1000)) {
    dhcp_coarse_tmr();
    timerDhcpCoarse -= DHCP_COARSE_TIMER_SECS*1000;
  }
#endif /* LWIP_DHCP */
#if IP_REASSEMBLY
  /* execute IP reassembly timer */
  if (timerIpReass > IP_TMR_INTERVAL) {
    ip_reass_tmr();
    timerIpReass -= IP_TMR_INTERVAL;
  }
#endif /* IP_REASSEMBLY*/
#if LWIP_AUTOIP
  /* execute AUTOIP timer */
  if (timerAutoIP > AUTOIP_TMR_INTERVAL) {
    autoip_tmr();
    timerAutoIP -= AUTOIP_TMR_INTERVAL;
  }
#endif /* LWIP_AUTOIP */
#if LWIP_IGMP
  /* execute IGP timer */
  if (timerIgmp > IGMP_TMR_INTERVAL) {
    igmp_tmr();
    timerIgmp -= IGMP_TMR_INTERVAL;
  }
#endif /* LWIP_IGMP */
}
#endif /* NO_SYS */

/* a simple multicast test */
#if LWIP_UDP && LWIP_IGMP
static void
mcast_init(void)
{
  struct udp_pcb *pcb;
  struct pbuf* p;
  struct ip_addr remote_addr;
  char data[1024]={0};
  int size = sizeof(data);
  err_t err;

  pcb = udp_new();
  udp_bind(pcb, IP_ADDR_ANY, 0);
  
  LWIP_PORT_INIT_IPADDR(&pcb->multicast_ip);
  
  p = pbuf_alloc(PBUF_TRANSPORT, 0, PBUF_REF);
  if (p == NULL) {
    err = ERR_MEM;
  } else {
    p->payload = (void*)data;
    p->len = p->tot_len = size;
    
    remote_addr.addr = inet_addr("232.0.0.0");
    
    err = udp_sendto(pcb, p, &remote_addr, ntohs(20000));
    
    pbuf_free(p);
  }
  udp_remove(pcb);
}
#endif /* LWIP_UDP && LWIP_IGMP*/

/* This function initializes all network interfaces */
static void
msvc_netif_init()
{
  struct ip_addr ipaddr, netmask, gw;
#if LWIP_HAVE_LOOPIF
  struct ip_addr loop_ipaddr, loop_netmask, loop_gw;
#endif /* LWIP_HAVE_LOOPIF */

  LWIP_PORT_INIT_GW(&gw);
  LWIP_PORT_INIT_IPADDR(&ipaddr);
  LWIP_PORT_INIT_NETMASK(&netmask);
  printf("Starting lwIP, local interface IP is %s\n", inet_ntoa(*(struct in_addr*)&ipaddr));

#if NO_SYS
#if LWIP_ARP
  netif_set_default(netif_add(&netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, ethernet_input));
#else /* LWIP_ARP */
  netif_set_default(netif_add(&netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, ip_input));
#endif /* LWIP_ARP */
#else /* NO_SYS */
  netif_set_default(netif_add(&netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, tcpip_input));
#endif /* NO_SYS */
  netif_set_up(&netif);

#if LWIP_HAVE_LOOPIF
  IP4_ADDR(&loop_gw, 127,0,0,1);
  IP4_ADDR(&loop_ipaddr, 127,0,0,1);
  IP4_ADDR(&loop_netmask, 255,0,0,0);

  printf("Starting lwIP, loopback interface IP is %s\n", inet_ntoa(*(struct in_addr*)&loop_ipaddr));
#if NO_SYS
  netif_add(&loop_netif, &loop_ipaddr, &loop_netmask, &loop_gw, NULL, loopif_init, ip_input);
#else /* NO_SYS */
  netif_add(&loop_netif, &loop_ipaddr, &loop_netmask, &loop_gw, NULL, loopif_init, tcpip_input);
#endif /* NO_SYS */
  netif_set_up(&loop_netif);
#endif /* LWIP_HAVE_LOOPIF */
}

/* This is somewhat different to other ports: we have a main loop here:
 * a dedicated task that waits for packets to arrive. This would normally be
 * done from interrupt context with embedded hardware, but we don't get an
 * interrupt in windows for that :-) */
void main_loop()
{
#if NO_SYS
  nosys_init();
#else /* NO_SYS */
  tcpip_init(0,0);
#endif /* NO_SYS */

  msvc_netif_init();

#if LWIP_UDP && LWIP_IGMP
  mcast_init();
#endif /* LWIP_UDP && LWIP_IGMP */

#if LWIP_TCP
  httpd_init();
  netio_init();
#endif /* LWIP_TCP */

  while (!_kbhit()) {
#if NO_SYS
    /* handle timers with NO_SYS=1 */
    timers_update();
#endif /* NO_SYS */

    /* check for packets */
    update_adapter();
  }

  /* release the pcap library... */
  shutdown_adapter();
}

int main(void)
{
  /* no stdio-buffering, please! */
  setvbuf(stdout, NULL,_IONBF, 0);

  main_loop();

  return 0;
}
