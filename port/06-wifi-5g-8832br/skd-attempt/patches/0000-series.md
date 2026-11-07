# Proposed patch series for `package/kernel/rtl8832br-rx2lpro`

`100-linux66-openwrt-wr1500.patch` from `/tmp/rebase-base`
`package/kernel/rtl8832br-wr1500/patches/` (commit `830c08cf`, 2563 lines)
applies fuzz-free to pristine TP-Link G6 GPL and is our only reproducibility
anchor: **copy it verbatim as `100-…` and never edit it in place.** Its own
ancestors (`linux66-first-api.patch`, `linux66-os-service-api.patch`) are
already folded into it — do not apply them again.

| patch | purpose | status here |
|---|---|---|
| `100-linux66-openwrt-wr1500.patch` | 4.4 → 6.6 + OpenWrt backports compat, DMA-API error propagation, key-delete path, cfg80211 callbacks, tasklet ctx, include fixes | inherited unchanged (fetch from `/tmp/rebase-base`) |
| `102-check-mmio-window.patch` | refuse the first MAC MMIO read unless BAR2 really is identity-mapped | rewritten for our board below |
| `103-pci-irq-preflight.patch` | refuse adapter creation unless INTx is routable; report the real `request_irq` error | rewritten for our board below |
| `110-linux618-compat.patch` | the 6.6 → 6.18 delta (unknown until CI builds; see `../README.md` §3) | to be authored by the first CI failure log |

Both guards exist for one reason: on 6.6 the *same* board family died in two
different places and the second death (a MIPS data-bus exception during probe)
cost a reboot with no diagnostics. Neither guard proves the hardware works;
they only make failure cheap and legible.

## `102-check-mmio-window.patch`

Config-space only, never a test read. Expect OEM-equivalent BAR2 raw
`0x19000004` (64-bit MEM, base `0x19000000`), len ≥ 1 MiB, Memory-Enable set.

```
--- a/os_dep/linux/pci_intf.c
+++ b/os_dep/linux/pci_intf.c
@@
+/* RTL8197F has a fixed outbound aperture, not a programmable PCI ATU:
+ * bus address must equal CPU physical address. If the DT ranges inherit
+ * bus address zero, Linux programs BAR2 = 0, the identity-mapped CPU access
+ * at 0x19000000 no longer matches any BAR, and the first MAC read raises an
+ * unrecoverable MIPS data-bus exception inside mac_ax_ops_init -- before any
+ * firmware message, so the boot log looks like a driver bug instead of a
+ * window bug. Check configuration space only; never probe MMIO to detect it.
+ * (Same guard proven on Cudy WR1500 R76: /tmp/rebase-base commit 3dfa5900.)
+ */
+static int rx2lpro_pci_check_mmio(struct pci_dev *pdev)
+{
+	u32 bar;
+	u16 command;
+
+	if (pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0 + 2 * 4, &bar) ||
+	    pci_read_config_word(pdev, PCI_COMMAND, &command))
+		return -EIO;
+
+	if (!(pci_resource_flags(pdev, 2) & IORESOURCE_MEM) ||
+	    pci_resource_start(pdev, 2) != 0x19000000 ||
+	    pci_resource_len(pdev, 2) < 0x100000 ||
+	    (bar & PCI_BASE_ADDRESS_SPACE_IO) ||
+	    (bar & PCI_BASE_ADDRESS_MEM_MASK) != 0x19000000 ||
+	    !(command & PCI_COMMAND_MEMORY)) {
+		dev_err(&pdev->dev,
+			"unsafe RTL8832BR BAR2: cfg=%#010x command=%#06x; require MMIO at bus/CPU 0x19000000, refusing MAC access\n",
+			bar, command);
+		return -ENXIO;
+	}
+	dev_info(&pdev->dev, "RTL8832BR BAR2 verified: cfg=%#010x command=%#06x\n",
+		 bar, command);
+	return 0;
+}
```
call it in `pci_dvobj_init()` right after `pci_enable_device()` succeeds, and
bail to the existing `disable_picdev` label.

## `103-pci-irq-preflight.patch`

```
--- a/os_dep/linux/pci_intf.c
+++ b/os_dep/linux/pci_intf.c
@@  request_irq site
 		err = request_irq(pdev->irq, &rtw_pci_interrupt, SA_SHIRQ, DRV_NAME, dvobj);
 #endif
 		if (err)
-			RTW_INFO("Error allocating IRQ %d", pdev->irq);
+			dev_err(&pdev->dev, "request_irq(%u) failed: %d\n", pdev->irq, err);
@@  rx2lpro probe entry, before adapter/timer creation
+	/* Stock RTL8197F proves the routing: /proc/interrupts shows
+	 * "5: 968696 MIPS 5 rtk_wifi6" (INTx on CPU line 5, no MSI in this
+	 * root complex). Our DT expresses that as <&intc 21 4>. A missing or
+	 * bogus INTx must reject probe BEFORE the adapter's timers/workqueues
+	 * exist, because the legacy G6 error unwind is not verified on 6.x.
+	 */
+	{
+		u8 pin = 0;
+		int pcierr = pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &pin);
+
+		if (pcierr) {
+			dev_err(&pdev->dev, "cannot read PCI INTx pin: %d\n", pcierr);
+			return -EIO;
+		}
+		dev_info(&pdev->dev, "RTL8832BR PCI INTx: pin=%u linux_irq=%u\n",
+			 pin, pdev->irq);
+		if (!pdev->irq || pdev->irq == 255 || pin < 1 || pin > 4) {
+			dev_err(&pdev->dev,
+				"invalid RTL8832BR INTx routing: pin=%u irq=%u; refusing adapter initialization\n",
+				pin, pdev->irq);
+			return -ENXIO;
+		}
+	}
```

Also in this patch (from the same series): restrict the bind to
`pdev->vendor == 0x10ec && pdev->device == 0xb832` — the stock table also
carries `b814` (RTL8852A) and `0192` (RTL8192XB, the in-SoC radio), and we want
neither on this board.

## Not carried over, on purpose

* toby's `cudy,ax1500-r76`-only DT match (our board string differs; device ID
  filter is enough).
* `SA_SHIRQ` on a single-lane point-to-point link is inherited from the OEM and
  left alone for attempt 1 (it is what the 6.6 build used).
* The optional TX descriptor cache, private NAT/bridge shortcuts, WEXT surface,
  `CONFIG_PROC_DEBUG`: all off, exactly as in the 6.6 build.
