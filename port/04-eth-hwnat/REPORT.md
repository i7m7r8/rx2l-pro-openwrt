# REPORT — eth + hwNAT track (WORKING DRAFT)

Recommendation: TBD (one paragraph: which of the three eth designs, why).

Needs from elsewhere: TBD.

## The VLAN tags the network config depends on, read from the driver

`etc/uci-defaults/13_switch_vlans_rx2lpro` renumbers the generated `switch_vlan`
sections to 9 (LAN) and 8 (WAN), and points `br-lan` at `eth0.9` and `wan` at
`eth0.8`. That is only correct if the SoC's Ethernet driver really tags frames
that way, so the constants were read out of the source:

```
drivers/net/ethernet/rtl819x_eth.c:54   #define RTL_NASH_VID   9
drivers/net/ethernet/rtl819x_eth.c:55   #define RTL_WAN_VID    8
```

and the driver's own debug line names the mapping it applies:

```
tx: wan(vid 8 -> port 0) %lu, lan(vid 9 -> 3,4) %lu, untagged(-> 3,4) %lu
```

So LAN traffic leaves tagged 9 and WAN tagged 8, which is exactly the pair the
uci-defaults script installs. Had these been, say, 1 and 2, the renumber script
would have produced a board with no LAN at all and the symptom would look like a
switch-driver failure.

The driver also programs its *internal* switch VLANs itself
(`rtl819x_intsw_zapis_vlan(RTL_NASH_VID, …)`), which is why the external
RTL8367B's CPU port can hand it untagged frames and still get the right tag.
